// Wineador controller firmware — stage 2.
//
// The main loop never waits. Each pass:
//   1. Minimum job (5.2): boot hold, relay exercise, runtime guards and the
//      interlock sequence decide the four outputs, from whatever readings are
//      to hand — never waiting for a sensor, the SD card or the network.
//   2. Plenum fan (kickstart, tach), sensors and RTC — each a small step that
//      returns at once.
//   3. Serial commands, status line, and a "live right now" block when a load changes.
//
// Stage 2 has no control logic yet, so nothing asks a load to run in the
// cabinet and bench builds: every load stays off. In a test build a scenario
// (run <n>) or the boot-demand build asks, and the guards decide.
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
#include "wineador/controller.h"
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
#include "wineador/scenario.h"
#endif

using namespace wineador;

// ---------------------------------------------------------------- state
static const Settings gSettings = settingsFor(kBuildProfile);
static console::BootInfo gBoot;
static char gVersion[40];
static Controller gCtl;                  // boot hold, guards, interlock, relay exercise
static FanController gFan(gSettings.safety.fanMinDutyPct, gSettings.safety.fanKickstartMs);
static Readings gBusReadings;            // the last sensor sweep, as read
static Readings gReadings;               // what the logic sees (values scripted in a test build)
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
static ScenarioRunner gScenario;
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
  s.bootDemand = kBootDemand;
  s.bootReason = gBoot.bootReason;
  s.holdActive = gCtl.hold().active();
  s.holdRemainingMs = gCtl.hold().remainingMs();
  s.holdLengthMs = gCtl.hold().lengthMs();
  s.loads = outputs::current();
  char guards[160];
  formatGuardText(guards, sizeof(guards), gCtl);
  s.guardText = guards;
  s.fanDutyPct = fan::duty();
  s.fanRpm = fan::rpm();
  s.cab = summarise(gReadings);
  s.loopMaxUs = gLoopMax.take();
  s.heapFree = ESP.getFreeHeap();
  s.rtcOk = i2cbus::rtcOk();
  s.i2cPullupsOn = !i2cbus::internalPullupsOff();
  s.readingsAgeMs = elapsedMs(millis(), gLastSweepDoneMs);
  s.staleAfterMs = 2 * gSettings.tuning.sensorReadMs;
  char line[600];
  formatStatusLine(line, sizeof(line), s);
  Serial.println(line);
}

static void printLive(const char* why) {
  char block[720];
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

static void handleCommand(Command c, int arg) {
  (void)arg;
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
    case Command::Exercise:
      if (gCtl.requestExercise(millis()))
        Serial.println("  Relay exercise requested: compressor and heater held off, 3 cycles of K1, ends on the cooling side.");
      else
        Serial.println("  A relay exercise is already under way. Nothing more done.");
      break;
#if defined(WINEADOR_TEST_BUILD)
    case Command::Stall: {
      // TEST BUILDS ONLY (B3 test 11). Stops feeding the watchdog on purpose.
      // Not compiled into the cabinet or bench builds at all. The outputs are
      // left as they are, like a real lock-up: the watchdog reset turns them off.
      const LoadOutputs l = outputs::current();
      Serial.println("\n!! STALL: the main loop is now locked up on purpose.");
      Serial.printf ("!! Live now: cooling %s, heater %s, humidifier %s. They stay as they are until the\n",
                     l.cool ? "ON" : "off", l.heat ? "ON" : "off", l.humid ? "ON" : "off");
      Serial.println("!! watchdog resets the board (within 5 s); the reset turns every load off.");
      Serial.flush();
      volatile uint32_t spin = 0;
      for (;;) { spin = spin + 1; }
    }
    case Command::Run: {
      const int n = arg;
      if (gScenario.running()) {
        Serial.printf("  run %d is still going. Type stop first.\n", gScenario.number());
      } else if (!ScenarioRunner::exists(n)) {
        Serial.println("  Stage 2 scenarios: run 1, run 2, run 3, run 13. Nothing started.");
      } else {
        gScenario.start(n, millis(), gModel);
      }
      break;
    }
    case Command::Stop:
      if (gScenario.running()) gScenario.stop(millis(), gModel);
      else Serial.println("  No scenario is running.");
      break;
#else
    case Command::Stall:     // never returned outside a test build; treated as not allowed
    case Command::Run:
    case Command::Stop:
#endif
    case Command::TestOnly:
    case Command::StallNotAllowed:
      Serial.println("  That command exists only in test builds. Nothing done.");
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

  gCtl.begin(gSettings, millis());       // boot hold starts now (4.4)
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
  gScenario.begin(gSettings);
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
static const char* loadWord(int L) { return L == 0 ? "cooling" : L == 1 ? "heater" : "humidifier"; }

// "heater ON; humidifier OFF (max-run)" — what changed, and why it went off.
static void describeChange(char* out, size_t n, const LoadOutputs& was, const LoadOutputs& now) {
  const GuardDecisions& d = gCtl.decisions();
  const bool w[3] = {was.cool, was.heat, was.humid}, v[3] = {now.cool, now.heat, now.humid};
  const LoadDecision* ld[3] = {&d.cool, &d.heat, &d.humid};
  size_t len = 0;
  out[0] = 0;
  for (int L = 0; L < 3; L++) {
    if (w[L] == v[L]) continue;
    const char* why = "";
    if (!v[L]) why = ld[L]->block != GuardBlock::None ? guardBlockName(ld[L]->block) : "demand ended";
    int k = snprintf(out + len, n - len, "%s%s %s%s%s%s", len ? "; " : "", loadWord(L), v[L] ? "ON" : "OFF",
                     *why ? " (" : "", why, *why ? ")" : "");
    if (k > 0) len = (len + k < n) ? len + k : n - 1;
  }
  if (was.relayHeating != now.relayHeating) {
    int k = snprintf(out + len, n - len, "%sK1 to the %s side", len ? "; " : "", now.relayHeating ? "heating" : "cooling");
    if (k > 0) len = (len + k < n) ? len + k : n - 1;
  }
}

static void printExerciseEvents() {
  uint8_t cyc = 0;
  for (RelayExercise::Event e; (e = gCtl.exercise().takeEvent(&cyc)) != RelayExercise::Event::None;) {
    switch (e) {
      case RelayExercise::Event::Waiting:
        Serial.println("  RELAY EXERCISE: started. Compressor and heater may not start; cycling once both are off.");
        break;
      case RelayExercise::Event::Energised:
        Serial.printf("  RELAY EXERCISE: cycle %u/%u, K1 energised\n", (unsigned)cyc, (unsigned)gCtl.exercise().cycles());
        break;
      case RelayExercise::Event::Released:
        Serial.printf("  RELAY EXERCISE: cycle %u/%u, K1 released\n", (unsigned)cyc, (unsigned)gCtl.exercise().cycles());
        break;
      case RelayExercise::Event::Done:
        Serial.println("  RELAY EXERCISE: done, three cycles. K1 on the cooling side.");
        break;
      default: break;
    }
  }
}

void loop() {
  const uint32_t t0 = micros();
  const uint32_t now = millis();
  watchdog::feed();

  // 1. Minimum job, every pass. Uses the readings already to hand; waits for nothing.
#if defined(WINEADOR_TEST_BUILD)
  gReadings = gBusReadings;
  applyScripted(gReadings, gModel);       // values scripted; bus results stay real
#else
  gReadings = gBusReadings;
#endif
  Demand demand;                          // stage 2: no control logic, so nothing is asked for...
#if defined(WINEADOR_TEST_BUILD)
  if (gScenario.running()) demand = gScenario.demand();          // ...except by a scenario
  else if (kBootDemand) { demand.heat = true; demand.humid = true; }   // ...or the boot-demand build
#endif
  PassInputs in;
  in.nowMs = now;
  in.settingsOk = (gBoot.settingsProblem == nullptr);
  in.fanOk = fan::duty() >= gSettings.safety.fanMinDutyPct && fan::rpm() > 0;
  in.readingsFresh = elapsedMs(now, gLastSweepDoneMs) <= 2 * gSettings.tuning.sensorReadMs;
  in.cab = summarise(gReadings);
  in.live = outputs::current();
  outputs::apply(gCtl.pass(in, demand));

  // 2. The fan
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
    gBusReadings = i2cbus::readings();
    gLastSweepDoneMs = now;
    setClockFromRtc();
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
    if (gLine.feed((char)Serial.read())) {
      int arg = -1;
      Command c = parseCommand(gLine.line(), kTestBuild, &arg);
      handleCommand(c, arg);
    }
  }

  printExerciseEvents();

  // 5. A load changed: show what is live now, and a status line at once
  LoadOutputs loads = outputs::current();
  uint8_t fanNow = fan::duty();
  if (loads != gShownLoads || fanNow != gShownFan) {
    char why[120];
    if (loads == gShownLoads) snprintf(why, sizeof(why), "plenum fan %u%% -> %u%%", (unsigned)gShownFan, (unsigned)fanNow);
    else                      describeChange(why, sizeof(why), gShownLoads, loads);
    gShownLoads = loads;
    gShownFan = fanNow;
    printLive(why);
    printNow = true;
  }

#if defined(WINEADOR_TEST_BUILD)
  // 6. The scenario watches what is live now and decides what to ask for next pass.
  ScenarioView view;
  view.nowMs = now;
  view.live = loads;
  view.fanOk = fan::duty() >= gSettings.safety.fanMinDutyPct && fan::rpm() > 0;
  const bool wasRunning = gScenario.running();
  gScenario.update(view, gCtl, gModel);
  if (gScenario.takeFanKick()) {          // run 2 scaffolding, see scenario.h
    gFan.command(0, now);
    gFan.output(now);                     // counts as stopped (not driven), so the next command kicks
    gFan.command(gSettings.tuning.fanIdlePct, now);
  }
  for (const char* l; (l = gScenario.takeLine()) != nullptr;) Serial.println(l);
  if (wasRunning && !gScenario.running()) printNow = true;
#endif

  if (printNow) printStatus();
  gLoopMax.add(micros() - t0);
}
