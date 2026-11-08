// Wineador controller firmware — stage 3.
//
// The main loop never waits. Each pass:
//   1. Minimum job (5.2): boot hold, relay exercise, runtime guards and the
//      interlock sequence decide the four outputs, from whatever readings are
//      to hand — never waiting for a sensor, the SD card or the network.
//   2. Plenum fan (kickstart, tach, stall kickstarts), sensors and RTC — each a
//      small step that returns at once. After each sweep: sensor health (4.3),
//      serial checks and calibration (5.3).
//   3. NTP (background) and the DS3231 correction; serial commands; status line;
//      a "live right now" block when a load changes.
//
// No control logic yet (stage 4), so nothing asks a load to run in the
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
#include "wineador/sensor_health.h"
#include "wineador/sensor_table.h"
#include "wineador/fan_monitor.h"
#include "wineador/clock_state.h"
#include "wineador/decode.h"
#include "drivers/outputs.h"
#include "drivers/fan.h"
#include "drivers/i2c_bus.h"
#include "drivers/sd_card.h"
#include "drivers/watchdog.h"
#include "drivers/core_log.h"
#include "drivers/reset_record.h"
#include "drivers/time_sync.h"
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
static LoadOutputs gShownLoads;          // last state printed in a live block
static uint8_t gShownFan = 0;
static LineReader gLine;
static MaxTracker gLoopMax;
static uint32_t gLastSweepStartMs = 0;
static uint32_t gLastStatusMs = 0;       // the status line has its own timer, not tied to the bus
static uint32_t gLastSweepDoneMs = 0;    // when the readings were last refreshed
static uint32_t gHeapBaseline = 0;
static bool gClockSet = false;
static SensorHealth gHealth;             // 4.3, 5.3
static RequirementGate gGate;            // 4.3 requirements + recovery hold
static FanMonitor gFanMon;               // 5.6 stall kickstarts
static ClockState gClock;                // OSF, NTP, trusted timestamps
static uint32_t gStuckSeen = 0;          // bus recoveries already reported
static bool gCheckRtc = false;           // compare the DS3231 with NTP time at the next chance
static bool gRtcWriteArmed = false;      // write the DS3231 at the next whole second
static bool gRtcWriting = false;
static int64_t gRtcErrorS = 0;
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

// "| excl ch6 absent, ch3 crc | sensor hold 12s" — empty when all six are healthy.
static void healthText(char* out, size_t n, uint32_t nowMs) {
  size_t len = 0;
  out[0] = 0;
  auto add = [&](const char* fmt, auto... a) {
    if (len + 1 >= n) return;
    int k = snprintf(out + len, n - len, fmt, a...);
    if (k > 0) len = (len + k < n) ? len + k : n - 1;
  };
  bool first = true;
  for (int i = 0; i < kNumSensors; i++) {
    if (gHealth.healthy(i)) continue;
    add("%sch%d %s", first ? "| excl " : ", ", channelOf(i), exclusionName(gHealth.reason(i)));
    first = false;
  }
  if (gGate.tempHolding() || gGate.humidHolding()) {
    const uint32_t left = gGate.tempHolding() ? gGate.tempHoldLeftMs(nowMs) : gGate.humidHoldLeftMs(nowMs);
    add(" | sensor hold %lus", (unsigned long)((left + 999) / 1000));
  }
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
  s.fanPwmReadback = (int32_t)fan::pwmReadback();
  s.fanPulses = fan::lastPulses();
  s.fanEdges = fan::lastTransitions();
  s.cab = gHealth.summary();
  char health[120];
  healthText(health, sizeof(health), millis());
  s.healthText = health;
  s.timeTrusted = gClock.trusted();
  s.rtcLostTime = gClock.osfAtBoot && !gClock.corrected;
  char net[48];
  snprintf(net, sizeof(net), "wifi %s ntp %s", timesync::wifiUp() ? "up" : "down",
           gClock.ntpSynced ? "ok" : "waiting");
  s.netText = net;
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
  if (gClockSet || gClock.ntpSynced) return;   // NTP's time wins once it has arrived
  DateTime dt;
  if (!i2cbus::rtcOk() || !i2cbus::rtcDateTime(dt)) return;
  struct timeval tv = {};
  tv.tv_sec = (time_t)toUnixSeconds(dt);
  settimeofday(&tv, nullptr);
  gClockSet = true;
}

// nowMs: this pass's time, the same clock the scenario is then updated with.
static void handleCommand(Command c, int arg, uint32_t nowMs) {
  (void)arg;
  switch (c) {
    case Command::None: break;
    case Command::Boot: {
      console::printBanner(gSettings, gBoot);
      console::printSensorCheck();
      char clock[240];
      formatClockSummary(clock, sizeof(clock), gClock);
      Serial.printf("  CLOCK: %s\n\n", clock);
      printStatus();
      break;
    }
    case Command::Help:
      console::printHelp(kTestBuild);
      break;
    case Command::Exercise:
      if (gCtl.requestExercise(nowMs))
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
        Serial.printf("  Scenarios: %s. Nothing started.\n", ScenarioRunner::list());
      } else {
        gScenario.start(n, nowMs, gModel);
      }
      break;
    }
    case Command::Stop:
      if (gScenario.running()) gScenario.stop(nowMs, gModel);
      else Serial.println("  No scenario is running.");
      break;
    case Command::Resets:
      resetrec::print();
      break;
    case Command::ResetsClear:
      resetrec::clear();                   // SLOW: a flash write, a few ms; only when typed
      break;
#else
    case Command::Resets:
    case Command::ResetsClear:
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
#if defined(WINEADOR_TEST_BUILD)
  resetrec::begin((int)esp_reset_reason());   // BOOT ONLY: one flash write (reset record, test builds)
#endif
  gBoot.version = makeVersionString(gVersion, sizeof(gVersion), kFirmwareStage, kBuildProfile, kTestBuild);
  gBoot.settingsProblem = validateSettings(gSettings);

  setenv("TZ", "UTC0", 1);                // timestamps are UTC (5.4)
  tzset();

  gCtl.begin(gSettings, millis());       // boot hold starts now (4.4)
  gHealth.begin(gSettings);
  gGate.begin(gSettings.safety.sensorRecoveryHoldMs);
  gFanMon.begin(gSettings.safety.fanStallRetryMs);
  i2cbus::begin(millis());
  gBoot.pullupsOff = i2cbus::internalPullupsOff();

  // SLOW, BOOT ONLY: mounting the SD card. Timed, and shown in the banner.
  watchdog::feed();
  const uint32_t sdStart = millis();
  gBoot.sdOk = sdcard::begin();
  gBoot.sdMountMs = millis() - sdStart;
  gBoot.sdMB = sdcard::cardMB();
  watchdog::feed();

  // BOOT ONLY: start WiFi and SNTP. Both return at once and connect in the background.
  timesync::begin();
  watchdog::feed();

#if defined(WINEADOR_TEST_BUILD)
  gModel.begin(ModelRates{});
  gScenario.begin(gSettings);
  gLastModelMs = millis();
#endif

  console::printBanner(gSettings, gBoot);
#if defined(WINEADOR_TEST_BUILD)
  resetrec::print();
#endif
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

static void printFanEvents() {
  uint32_t k = 0;
  for (FanMonitor::Event e; (e = gFanMon.takeEvent(&k)) != FanMonitor::Event::None;) {
    switch (e) {
      case FanMonitor::Event::Stall:
        Serial.println("  FAN STALL: tach reads zero with PWM asserted. Compressor and heater held off.");
        break;
      case FanMonitor::Event::Kick:
        Serial.printf("  FAN STALL: kickstart %lu (100%% for 1 s); next in %lu s if the tach still reads zero\n",
                      (unsigned long)k, (unsigned long)(gSettings.safety.fanStallRetryMs / 1000));
        break;
      case FanMonitor::Event::Recovered:
        Serial.printf("  FAN RECOVERED: tach reads %lu rpm. Loads may resume.\n", (unsigned long)fan::rpm());
        break;
      default: break;
    }
  }
}

// After each sweep: serial results, sensor health, calibration, bus faults, OSF.
static void afterSweep(uint32_t now) {
  for (int i = 0; i < kNumSensors; i++) {
    i2cbus::SerialResult sr;
    if (i2cbus::takeSerial(i, sr)) gHealth.serialResult(i, sr.bus, sr.serial);
  }
  // Health sees the values the logic sees: scripted in a test build (bus results stay real).
  Readings r = gBusReadings;
#if defined(WINEADOR_TEST_BUILD)
  applyScripted(r, gModel);
#endif
  gHealth.update(r, now);
  for (int i = 0; i < kNumSensors; i++) if (gHealth.wantsSerial(i)) i2cbus::wantSerial(i);

  if (i2cbus::busStuckCount() != gStuckSeen) {
    gStuckSeen = i2cbus::busStuckCount();
    Serial.printf("  I2C BUS STUCK with ch%d selected: mux reset pulsed, I2C restarted (%lu since boot). "
                  "RTC and other sensors carry on.\n",
                  channelOf(i2cbus::lastStuckIndex()), (unsigned long)gStuckSeen);
  }
  SensorHealth::Event e;
  while (gHealth.takeEvent(e)) {
    if (e.excluded) Serial.printf("  SENSOR EXCLUDED: ch%d %s - %s\n", channelOf(e.index),
                                  kSensors[e.index].position, exclusionName(e.reason));
    else            Serial.printf("  SENSOR RECOVERED: ch%d %s - 10 good reads, serial re-checked\n",
                                  channelOf(e.index), kSensors[e.index].position);
  }

  if (i2cbus::lastSweepWasBoot()) {
    gClock.osfKnown = i2cbus::rtcStatusKnown();
    gClock.osfAtBoot = i2cbus::rtcOsf();
    if (!gClock.osfKnown)
      Serial.println("  !! RTC status register not read: lost-time check (OSF) not done this boot");
    else if (gClock.osfAtBoot) {
      Serial.println("\n  !! RTC LOST TIME: the DS3231's oscillator stopped while unpowered (OSF set).");
      Serial.println("  !! Timestamps are UNTRUSTED until NTP corrects the clock. Control is unaffected.");
      Serial.println("  !! If this repeats, replace the RTC cell (spec 2.4).\n");
    }
  }
}

// NTP (5.4): after each sync, check the DS3231 and correct it if more than 2 s out,
// writing on a whole-second boundary. Returns true when something was printed.
static bool afterNtp(uint32_t now) {
  bool printed = false;
  if (timesync::takeSync()) {
    gClock.ntpSynced = true;
    gClock.syncs++;
    if (gClock.firstSyncAtMs == 0) gClock.firstSyncAtMs = uptimeMs();
    gClockSet = true;                     // the system clock now has NTP's time
    char ts[24];
    timestamp(ts, sizeof(ts));
    Serial.printf("  NTP SYNC %lu: system clock set to %s\n", (unsigned long)timesync::syncCount(), ts);
    gCheckRtc = true;
    printed = true;
  }
  DateTime rtc;
  if (gCheckRtc && !gRtcWriting && i2cbus::rtcOk() && i2cbus::rtcDateTime(rtc)) {
    gCheckRtc = false;
    const int64_t rtcNow = toUnixSeconds(rtc) + (int64_t)(elapsedMs(now, i2cbus::rtcReadAtMs()) / 1000);
    const bool out = rtcNeedsWrite(rtcNow, (int64_t)time(nullptr), &gRtcErrorS);
    if (out || (gClock.osfAtBoot && !gClock.corrected)) {
      gRtcWriteArmed = true;              // written at the next whole second
    } else {
      gClock.corrected = true;
      gClock.lastCorrectionS = gRtcErrorS;
      gClock.correctedAtMs = uptimeMs();
      Serial.printf("  NTP: DS3231 within 2 s (%+lld s); not written\n", (long long)gRtcErrorS);
      printed = true;
    }
  }
  if (gRtcWriteArmed) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    if (tv.tv_usec < 100000) {            // just after a whole second
      i2cbus::requestRtcWrite(fromUnixSeconds((int64_t)tv.tv_sec), i2cbus::rtcOsf());
      gRtcWriteArmed = false;
      gRtcWriting = true;
    }
  }
  const int wr = i2cbus::takeRtcWriteResult();
  if (wr != 0) {
    gRtcWriting = false;
    if (wr > 0) {
      const bool hadOsf = gClock.osfAtBoot && !gClock.corrected;
      gClock.corrected = true;
      gClock.lastCorrectionS = gRtcErrorS;
      gClock.rtcWritten = true;
      gClock.correctedAtMs = uptimeMs();
      char ts[24];
      timestamp(ts, sizeof(ts));
      Serial.printf("  NTP: DS3231 was %+lld s out; set to %s%s\n", (long long)gRtcErrorS, ts,
                    hadOsf ? ". OSF cleared; timestamps trusted again" : "");
    } else {
      Serial.println("  NTP: DS3231 write FAILED; trying again after the next sweep");
      gCheckRtc = true;
    }
    printed = true;
  }
  return printed;
}

void loop() {
  const uint32_t t0 = micros();
  const uint32_t now = millis();
  watchdog::feed();

  // 1. Minimum job, every pass. Uses the health and readings already to hand; waits for nothing.
  const int cabOk = gHealth.healthyCabinet();
  gGate.update(gHealth.chamberHealthy() && cabOk >= gSettings.safety.minHealthyCabinet,
               cabOk >= gSettings.safety.minHealthyCabinet, now);
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
  in.cab = gHealth.summary();             // healthy sensors only, corrected RH (4.3, 5.3)
  in.sensorHoldTemp = gGate.tempHolding();
  in.sensorHoldHumid = gGate.humidHolding();
  in.live = outputs::current();
  outputs::apply(gCtl.pass(in, demand));
#if defined(WINEADOR_TEST_BUILD)
  resetrec::heartbeat((uint32_t)(uptimeMs() / 1000), outputs::current());   // RTC memory, cheap
#endif

  // 2. The fan. After each tach window, the stall check (5.6) may ask for a kickstart.
  fan::setDuty(gFan.output(now));
  if (fan::update(now, gSettings.safety.fanTachWindowMs)) {
    if (gFanMon.window(now, gFan.target() >= gSettings.safety.fanMinDutyPct, fan::rpm())) {
      const uint8_t target = gFan.target();
      gFan.command(0, now);
      gFan.output(now);                   // counts as stopped (not driven), so the next command kicks
      gFan.command(target, now);
      fan::setDuty(gFan.output(now));
    }
    printFanEvents();
  }

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
  bool sweptThisPass = false;
  if (i2cbus::takeSweepDone()) {
    sweptThisPass = true;
    gBusReadings = i2cbus::readings();
    gLastSweepDoneMs = now;
    afterSweep(now);
    setClockFromRtc();
    if (i2cbus::lastSweepWasBoot()) {
      console::printSensorCheck();
      printNow = true;                    // one status line straight after the boot check
    }
    if (gHeapBaseline == 0) gHeapBaseline = ESP.getFreeHeap();   // after all initialisation
  }
  if (afterNtp(now)) printNow = true;

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
      handleCommand(c, arg, now);
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
  view.sweepDone = sweptThisPass;
  view.health = &gHealth;
  view.fan = &gFanMon;
  view.busStuck = i2cbus::busStuckCount();
  view.rtcOk = i2cbus::rtcOk();
  view.pullupsOn = !i2cbus::internalPullupsOff();
  const bool wasRunning = gScenario.running();
  gScenario.update(view, gCtl, gModel);
  for (const char* l; (l = gScenario.takeLine()) != nullptr;) Serial.println(l);
  if (wasRunning && !gScenario.running()) printNow = true;
#endif

#if !defined(WINEADOR_TEST_BUILD)
  (void)sweptThisPass;
#endif
  if (printNow) printStatus();
  gLoopMax.add(micros() - t0);
}
