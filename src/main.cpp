// Wineador controller firmware — stage 1.
//
// The main loop never waits. Each pass:
//   1. Minimum job: turn off every load whose guards are not met. In stage 1
//      there are no guards yet, so every load is simply held off.
//   2. Boot hold, plenum fan (kickstart, tach), sensors and RTC — each a small
//      step that returns at once.
//   3. Serial commands, status line, and a "live right now" block when a load changes.
//
// The logic is in lib/core (plain C++, tested on the Mac). The hardware is in
// src/drivers. All pins are in include/pins.h.

#include <Arduino.h>
#include <sys/time.h>
#include <time.h>
#include "esp_system.h"
#include "esp_timer.h"

#include "wineador/build_info.h"
#include "wineador/settings.h"
#include "wineador/boot_hold.h"
#include "wineador/fan_controller.h"
#include "wineador/commands.h"
#include "wineador/status.h"
#include "wineador/summary.h"
#include "drivers/outputs.h"
#include "drivers/fan.h"
#include "drivers/i2c_bus.h"
#include "drivers/sd_card.h"
#include "drivers/watchdog.h"
#include "drivers/core_log.h"
#include "app/console.h"
#if defined(WINEADOR_TEST_BUILD)
#include "wineador/cabinet_model.h"
#endif

using namespace wineador;

// ---------------------------------------------------------------- state
static const Settings gSettings = settingsFor(kBuildProfile);
static console::BootInfo gBoot;
static char gVersion[40];
static BootHold gHold;
static FanController gFan(gSettings.safety.fanMinDutyPct, gSettings.safety.fanKickstartMs);
static Readings gReadings;               // what the logic sees (scripted in a test build)
static LoadOutputs gShownLoads;          // last state printed in a live block
static uint8_t gShownFan = 0;
static LineReader gLine;
static MaxTracker gLoopMax;
static uint32_t gLastSweepStartMs = 0;
static uint32_t gLastStatusMs = 0;       // the status line has its own timer, not tied to the bus
static uint32_t gLastSweepDoneMs = 0;    // when the readings were last refreshed
static uint32_t gHeapBaseline = 0;
static bool gClockSet = false;
#if defined(WINEADOR_TEST_BUILD)
static CabinetModel gModel;
static uint32_t gLastModelMs = 0;
#endif

static uint64_t uptimeMs() { return (uint64_t)(esp_timer_get_time() / 1000); }

// ---------------------------------------------------------------- output
static void timestamp(char* out, size_t n) {
  if (!gClockSet) { snprintf(out, n, "time-not-set"); return; }
  time_t t = time(nullptr);
  struct tm tmv;
  gmtime_r(&t, &tmv);
  strftime(out, n, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

static void printStatus() {
  char ts[24];
  timestamp(ts, sizeof(ts));
  StatusInfo s;
  s.timestamp = ts;
  s.uptimeMs = uptimeMs();
  s.profile = gSettings.profile;
  s.testBuild = kTestBuild;
  s.bootReason = gBoot.bootReason;
  s.holdActive = gHold.active();
  s.holdRemainingMs = gHold.remainingMs();
  s.holdLengthMs = gHold.lengthMs();
  s.loads = outputs::current();
  s.fanDutyPct = fan::duty();
  s.fanRpm = fan::rpm();
  s.cab = summarise(gReadings);
  s.loopMaxUs = gLoopMax.take();
  s.heapFree = ESP.getFreeHeap();
  s.rtcOk = i2cbus::rtcOk();
  s.i2cPullupsOn = !i2cbus::internalPullupsOff();
  s.readingsAgeMs = elapsedMs(millis(), gLastSweepDoneMs);
  s.staleAfterMs = 2 * gSettings.tuning.sensorReadMs;
  char line[420];
  formatStatusLine(line, sizeof(line), s);
  Serial.println(line);
}

static void printLive(const char* why) {
  char block[640];
  formatLiveBlock(block, sizeof(block), uptimeMs(), why, outputs::current(), fan::duty());
  Serial.println(block);
}

// Sets the ESP32's own clock from the RTC (spec 5.4), once, at the first good read.
static void setClockFromRtc() {
  if (gClockSet) return;
  DateTime dt;
  if (!i2cbus::rtcOk() || !i2cbus::rtcDateTime(dt)) return;
  struct timeval tv = {};
  tv.tv_sec = (time_t)toUnixSeconds(dt);
  settimeofday(&tv, nullptr);
  gClockSet = true;
}

static void handleCommand(Command c) {
  switch (c) {
    case Command::None: break;
    case Command::Boot:
      console::printBanner(gSettings, gBoot);
      console::printSensorCheck();
      printStatus();
      break;
    case Command::Help:
      console::printHelp(kTestBuild);
      break;
#if defined(WINEADOR_TEST_BUILD)
    case Command::Stall: {
      // TEST BUILDS ONLY (B3 test 11). Stops feeding the watchdog on purpose.
      // Not compiled into the cabinet or bench builds at all.
      Serial.println("\n!! STALL: the main loop is now locked up on purpose.");
      Serial.println("!! The watchdog should reset the board within 5 s. All loads are off.");
      Serial.flush();
      volatile uint32_t spin = 0;
      for (;;) { spin = spin + 1; }
    }
#else
    case Command::Stall:     // never returned outside a test build; treated as not allowed
#endif
    case Command::StallNotAllowed:
      Serial.println("  'stall' exists only in test builds. Nothing done.");
      break;
    case Command::Unknown:
      Serial.printf("  Unknown command '%s'. Type help.\n", gLine.line());
      break;
  }
}

// ---------------------------------------------------------------- setup
void setup() {
  // Loads off from the first instruction, then the fan kickstart — before
  // any other startup step (4.6).
  outputs::begin();
  fan::begin();
  gFan.command(gSettings.tuning.fanIdlePct, millis());
  fan::setDuty(gFan.output(millis()));

  // The core's own error messages must never hold up the loop (core_log.h).
  corelog::silence();

  // Serial output goes into this buffer and the UART sends it in the background.
  // A print only waits if the buffer is full. The biggest burst (the 'boot'
  // command: banner + sensor check + status, ~3.5 KB) fits twice over.
  Serial.setTxBufferSize(8192);
  Serial.begin(115200);

  gBoot.watchdogOk = watchdog::begin(gSettings.safety.watchdogMs);
  gBoot.bootReason = console::resetReasonName((int)esp_reset_reason());
  gBoot.version = makeVersionString(gVersion, sizeof(gVersion), kFirmwareStage, kBuildProfile, kTestBuild);
  gBoot.settingsProblem = validateSettings(gSettings);

  setenv("TZ", "UTC0", 1);                // timestamps are UTC (5.4)
  tzset();

  gHold.begin(millis(), gSettings.safety.bootHoldMs);
  i2cbus::begin(millis());
  gBoot.pullupsOff = i2cbus::internalPullupsOff();

  // SLOW, BOOT ONLY: mounting the SD card. Timed, and shown in the banner.
  watchdog::feed();
  const uint32_t sdStart = millis();
  gBoot.sdOk = sdcard::begin();
  gBoot.sdMountMs = millis() - sdStart;
  gBoot.sdMB = sdcard::cardMB();
  watchdog::feed();

#if defined(WINEADOR_TEST_BUILD)
  gModel.begin(ModelRates{});
  gLastModelMs = millis();
#endif

  console::printBanner(gSettings, gBoot);
  gShownLoads = outputs::current();
  gShownFan = fan::duty();
  printLive("at boot: all loads off, fan kickstart");
  gLastSweepStartMs = millis();           // the boot sweep starts itself after the mux reset
  gLastSweepDoneMs = millis();
  gLastStatusMs = millis();
}

// ---------------------------------------------------------------- loop
void loop() {
  const uint32_t t0 = micros();
  const uint32_t now = millis();
  watchdog::feed();

  // 1. Minimum job, every pass, depends on nothing else.
  outputs::allOff();

  // 2. Timers and the fan
  gHold.update(now);
  fan::setDuty(gFan.output(now));
  fan::update(now, gSettings.safety.fanTachWindowMs);

  // 3. Sensors and RTC — one short step per pass
  if (i2cbus::idle() && elapsedMs(now, gLastSweepStartMs) >= gSettings.tuning.sensorReadMs) {
    if (i2cbus::startSweep()) gLastSweepStartMs = now;
  }
  i2cbus::update(now);

#if defined(WINEADOR_TEST_BUILD)
  // The model moves on a 1 s tick (finer steps would be lost in float rounding).
  if (elapsedMs(now, gLastModelMs) >= 1000) {
    gModel.step(elapsedMs(now, gLastModelMs), outputs::current());
    gLastModelMs = now;
  }
#endif

  bool printNow = false;
  if (i2cbus::takeSweepDone()) {
    gReadings = i2cbus::readings();
    gLastSweepDoneMs = now;
    setClockFromRtc();
#if defined(WINEADOR_TEST_BUILD)
    applyScripted(gReadings, gModel);     // values scripted; bus results stay real
#endif
    if (i2cbus::lastSweepWasBoot()) {
      console::printSensorCheck();
      printNow = true;                    // one status line straight after the boot check
    }
    if (gHeapBaseline == 0) gHeapBaseline = ESP.getFreeHeap();   // after all initialisation
  }

  // Status line on its own 15 s timer, so a stuck bus cannot stop it.
  if (elapsedMs(now, gLastStatusMs) >= gSettings.tuning.sensorReadMs) {
    gLastStatusMs = now;
    printNow = true;
  }

  // 4. Commands — read what has arrived, never wait for more
  for (int i = 0; i < 64 && Serial.available(); i++) {
    if (gLine.feed((char)Serial.read())) handleCommand(parseCommand(gLine.line(), kTestBuild));
  }

  // 5. A load changed: show what is live now, and a status line at once
  LoadOutputs loads = outputs::current();
  uint8_t fanNow = fan::duty();
  if (loads != gShownLoads || fanNow != gShownFan) {
    char why[48];
    if (loads == gShownLoads) snprintf(why, sizeof(why), "plenum fan %u%% -> %u%%", (unsigned)gShownFan, (unsigned)fanNow);
    else                      snprintf(why, sizeof(why), "loads changed");
    gShownLoads = loads;
    gShownFan = fanNow;
    printLive(why);
    printNow = true;
  }

  if (printNow) printStatus();
  gLoopMax.add(micros() - t0);
}
