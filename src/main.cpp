// Wineador controller firmware — stage 6.
//
// The main loop never waits. Each pass:
//   1. Temperature control (4.5-4.7, 5.6) asks for cooling / heating and sets
//      the fan duty, from the readings to hand. Then the minimum job (5.2):
//      boot hold, relay exercise, runtime guards and the interlock sequence
//      decide the four outputs — never waiting for a sensor, the SD card or
//      the network.
//   2. Plenum fan (kickstart, tach, stall kickstarts), sensors and RTC — each a
//      small step that returns at once. After each sweep: sensor health (4.3),
//      serial checks and calibration (5.3).
//   3. NTP (background) and the DS3231 correction; serial commands; status line;
//      a "live right now" block when a load changes.
//   4. Logging (5.4): every event goes through eventlog (serial + RAM + SD queue);
//      a readings row every 60 s (15 s with the compressor or heater on); an
//      hourly row each UTC hour. The SD card itself is written by its own task
//      (src/drivers/sd_log) — the loop never waits for it.
//
// In a test build the control acts on the scripted cabinet model; a stage 2-3
// scenario (or the boot-demand build) asks for loads directly instead.
// The humidifier control (4.7b) asks for the humidifier the same way.
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
#include "wineador/temp_control.h"
#include "wineador/humid_control.h"
#include "wineador/heater_pwm.h"
#include "wineador/logbook.h"
#include "wineador/hourly.h"
#include "drivers/outputs.h"
#include "drivers/fan.h"
#include "drivers/i2c_bus.h"
#include "drivers/sd_card.h"
#include "drivers/watchdog.h"
#include "drivers/core_log.h"
#include "drivers/reset_record.h"
#include "drivers/time_sync.h"
#include "drivers/sd_log.h"
#include "drivers/user_store.h"
#include "app/console.h"
#include "app/eventlog.h"
#include "app/serial_ui.h"
#if defined(WINEADOR_TEST_BUILD)
#include "wineador/cabinet_model.h"
#include "wineador/scenario.h"
#endif

using namespace wineador;

// ---------------------------------------------------------------- state
static Settings gSettings = settingsFor(kBuildProfile);   // user tier may change (serial 'set', NVS)
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
static TemperatureControl gTc;           // deadband, harvest, fan duty, stripping, ineffective checks
static HumidControl gHc;                 // humidifier call and its ineffective check
static uint32_t gHeatOnSinceMs = 0;      // for the heater PWM cap
static bool gHeatWasOn = false;
// Logging (5.4)
static GuardBlockTracker gGuardTrack;    // guard-block events
static BusEventLimiter gBusLimit;        // CRC/timeout events: 10 per channel per hour
static HourlyStats gHourly;
static bool gHourlyStarted = false;
static MaxTracker gLoopMaxLog;           // longest pass per log interval
static uint32_t gLastRowMs = 0;
static Readings gLogicReadings;          // what the logic used last sweep (scripted in a test build)
static uint32_t gCrcCount[kNumSensors] = {}, gTimeoutCount[kNumSensors] = {};
static bool gLogStarted = false;         // boot event written, SD writing started
static uint32_t gLastPassMs = 0;
static uint32_t gHeapLast = 0;
static bool gSdFailedShown = false;
static uint32_t gLastSdCheckMs = 0;
static uint64_t gLastCompressorStopMs = 0;
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
  char control[200];
  formatControlText(control, sizeof(control), gTc, gTc.lastCab(), millis());
  s.controlText = control;
  char humid[100];
  formatHumidText(humid, sizeof(humid), gHc, millis());
  s.humidText = humid;
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
  char sd[48];
  {
    const sdlog::Stats st = sdlog::stats();
    if (sdlog::stalledMs(millis()) > 5000) snprintf(sd, sizeof(sd), "SD STALLED %lus", (unsigned long)(sdlog::stalledMs(millis()) / 1000));
    else if (st.failed) snprintf(sd, sizeof(sd), "SD FAIL (logging paused)");
    else if (!sdlog::started()) snprintf(sd, sizeof(sd), "sd waiting for clock");
    else snprintf(sd, sizeof(sd), "sd ok %lu/16", (unsigned long)st.rowsBuffered);
  }
  s.sdText = sd;
  s.loopMaxUs = gLoopMax.take();
  s.heapFree = gHeapLast ? gHeapLast : ESP.getFreeHeap();
  s.rtcOk = i2cbus::rtcOk();
  s.i2cPullupsOn = !i2cbus::internalPullupsOff();
  s.readingsAgeMs = elapsedMs(millis(), gLastSweepDoneMs);
  s.staleAfterMs = 2 * gSettings.tuning.sensorReadMs;
  char line[900];
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

// A user setting changed: every part of the logic takes the new values, they are
// saved to NVS (a few ms; only when typed), and the change is logged (5.4).
static void applyUser(const UserSettings& was, const char* what) {
  const UserSettings& u = gSettings.user;
  gCtl.setUser(u);
  gTc.setUser(u);
  gHc.setUser(u);
  gHealth.setUser(u);
  const bool saved = userstore::save(u);
  char d[200];
  int k = snprintf(d, sizeof(d), "settings changed (%s): ", what);
  auto add = [&](const char* fmt, auto... a) { if (k > 0 && (size_t)k < sizeof(d)) k += snprintf(d + k, sizeof(d) - k, fmt, a...); };
  if (was.targetTempC != u.targetTempC) add("target %.1f -> %.1f C; ", was.targetTempC, u.targetTempC);
  if (was.rhSetpointPct != u.rhSetpointPct) add("RH setpoint %.0f -> %.0f%%; ", was.rhSetpointPct, u.rhSetpointPct);
  if (was.heaterEnable != u.heaterEnable) add("heater %s -> %s; ", was.heaterEnable ? "on" : "off", u.heaterEnable ? "on" : "off");
  if (was.humidifierEnable != u.humidifierEnable)
    add("humidifier %s -> %s; ", was.humidifierEnable ? "on" : "off", u.humidifierEnable ? "on" : "off");
  if (was.manualExcludeMask != u.manualExcludeMask) add("manual excludes 0x%02X -> 0x%02X; ", was.manualExcludeMask, u.manualExcludeMask);
  add("%s", saved ? "saved" : "NOT SAVED (NVS write failed) - lost at the next boot");
  eventlog::log("settings", "%s", d);
}

static bool sameUser(const UserSettings& a, const UserSettings& b) {
  return a.targetTempC == b.targetTempC && a.rhSetpointPct == b.rhSetpointPct && a.heaterEnable == b.heaterEnable &&
         a.humidifierEnable == b.humidifierEnable && a.manualExcludeMask == b.manualExcludeMask;
}

static serialui::View uiView() {
  static char ts[24];
  timestamp(ts, sizeof(ts));
  serialui::View v;
  v.s = &gSettings;
  v.ctl = &gCtl;
  v.tc = &gTc;
  v.hc = &gHc;
  v.health = &gHealth;
  v.clock = &gClock;
  v.bus = &gBusReadings;
  v.logic = &gLogicReadings;
  v.crc = gCrcCount;
  v.timeouts = gTimeoutCount;
  v.loads = outputs::current();
  v.fanDuty = fan::duty();
  v.fanRpm = fan::rpm();
  v.fanStalled = gFanMon.stalled();
  v.uptimeMs = uptimeMs();
  v.lastCompressorStopMs = gLastCompressorStopMs;
  v.rtcOk = i2cbus::rtcOk();
  v.testBuild = kTestBuild;
  v.version = gVersion;
  v.timestamp = ts;
  return v;
}

// nowMs: this pass's time, the same clock the scenario is then updated with.
static void handleCommand(Command c, const CommandArgs& a, uint32_t nowMs) {
  const int arg = a.arg;
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
    // ---- the serial interface (stage 6)
    case Command::Status:   serialui::printStatus(uiView()); break;
    case Command::Sensors:  serialui::printSensors(uiView()); break;
    case Command::Events:
      if (arg == -2) Serial.println("  events, or events <n> (a number). Nothing shown.");
      else serialui::printEvents(arg < 0 ? 10 : arg);
      break;
    case Command::Log:      serialui::printLog(); break;
    case Command::Settings: serialui::printSettings(uiView()); break;
    case Command::SetTemp:
    case Command::SetRh: {
      const UserSettings was = gSettings.user;
      const bool temp = c == Command::SetTemp;
      const bool ok = arg != -2 && (temp ? setTargetTemp(gSettings.user, a.f) : setRhSetpoint(gSettings.user, a.f));
      if (!ok) {
        if (temp) Serial.printf("  REFUSED: target temperature must be %.0f-%.0f C. Still %.1f C.\n", kTargetTempMinC, kTargetTempMaxC, was.targetTempC);
        else      Serial.printf("  REFUSED: RH setpoint must be %.0f-%.0f%%. Still %.0f%%.\n", kRhSetpointMinPct, kRhSetpointMaxPct, was.rhSetpointPct);
      } else if (sameUser(was, gSettings.user)) Serial.println("  No change.");
      else applyUser(was, temp ? "set temp" : "set rh");
      break;
    }
    case Command::SetHeater:
    case Command::SetHumid: {
      if (arg < 0) { Serial.println("  set heater on|off, set humid on|off. Nothing changed."); break; }
      const UserSettings was = gSettings.user;
      (c == Command::SetHeater ? gSettings.user.heaterEnable : gSettings.user.humidifierEnable) = (arg == 1);
      if (sameUser(was, gSettings.user)) Serial.println("  No change.");
      else applyUser(was, c == Command::SetHeater ? "set heater" : "set humid");
      break;
    }
    case Command::SetDefaults: {
      const UserSettings was = gSettings.user;
      gSettings.user = UserSettings{};
      if (sameUser(was, gSettings.user)) Serial.println("  Already at the defaults.");
      else applyUser(was, "set defaults");
      break;
    }
    case Command::Exclude: {
      if (arg < 2 || arg > 7) { Serial.println("  exclude chN on|off (N = 2-7). Nothing changed."); break; }
      const UserSettings was = gSettings.user;
      const uint8_t bit = (uint8_t)(1u << (arg - kFirstChannel));
      if (a.arg2 == 1) gSettings.user.manualExcludeMask |= bit;
      else gSettings.user.manualExcludeMask &= (uint8_t)~bit;
      if (sameUser(was, gSettings.user)) Serial.println("  No change.");
      else applyUser(was, a.arg2 == 1 ? "exclude on" : "exclude off");
      break;
    }
#if defined(WINEADOR_TEST_BUILD)
    case Command::SdStress:
      if (arg == -1) { sdlog::stressStop(); Serial.println("  SD stress test: stopping after the current write."); break; }
      if (arg < 0) { Serial.println("  sdstress <kHz 400-9999> retry|noretry <writes>, or sdstress stop. Nothing started."); break; }
      if (sdlog::stressStart((uint32_t)arg, a.arg2 == 1, (uint32_t)a.arg3))
        Serial.printf("  SD STRESS TEST: %d batches of 16 rows (~7.4 KB each), back to back, at %d kHz, retry %s, into "
                      "/stress/. Progress every 100 writes.\n", a.arg3, arg, a.arg2 == 1 ? "ON" : "OFF");
      else Serial.println("  An SD stress test is already running. Nothing started.");
      break;
    case Command::InjectCrc:
    case Command::InjectTimeout:
      if (arg < 2 || arg > 7) { Serial.println("  crc chN / timeout chN (N = 2-7). Nothing done."); break; }
      i2cbus::injectFault(arg - kFirstChannel, c == Command::InjectCrc ? BusResult::CrcFail : BusResult::ShortRead);
      Serial.printf("  TEST: the next read of ch%d will %s.\n", arg, c == Command::InjectCrc ? "fail its CRC" : "time out");
      break;
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
      } else if (!sameUser(gSettings.user, UserSettings{})) {
        Serial.println("  The scenarios check against the default user settings, and these have been changed.");
        Serial.println("  Type 'set defaults' first. Nothing started.");
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
    case Command::HumidCool:
      if (arg < 0) Serial.println("  humidcool on | humidcool off. Nothing changed.");
      else {
        gCtl.setHumidifyDuringCooling(arg == 1);
        Serial.printf("  Humidify during cooling runs: %s (test build; the setting is %s)\n", arg == 1 ? "ON" : "OFF",
                      gSettings.tuning.humidifyDuringCooling ? "on" : "off");
      }
      break;
    case Command::Fan:
      if (arg == -2) Serial.println("  fan <n> (0 or 20-100) or fan auto. Nothing changed.");
      else if (gTc.setIdleFanOverride(arg)) {
        if (arg < 0) Serial.printf("  Idle fan duty back to the setting (%u%%).\n", (unsigned)gSettings.tuning.fanIdlePct);
        else         Serial.printf("  Idle fan duty now %d%% (loads and harvest keep their own duties). 'fan auto' to undo.\n", arg);
      } else {
        Serial.printf("  REFUSED: %d%% - the fan is never commanded 1-19%% (4.6), nor over 100%%. Fan unchanged at %u%%.\n",
                      arg, (unsigned)fan::duty());
      }
      break;
    case Command::ResetsClear:
      resetrec::clear();                   // SLOW: a flash write, a few ms; only when typed
      break;
#else
    case Command::Resets:
    case Command::ResetsClear:
    case Command::Fan:
    case Command::HumidCool:
    case Command::InjectCrc:
    case Command::InjectTimeout:
    case Command::SdStress:
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
  // BOOT ONLY: the saved user settings (NVS, 5.5) — before anything takes a copy of the settings.
  gBoot.userNote = userstore::load(gSettings.user);
  gBoot.nvsSpace = userstore::nvsNamespace();
  gBoot.settingsProblem = validateSettings(gSettings);

  setenv("TZ", "UTC0", 1);                // timestamps are UTC (5.4)
  tzset();

  gCtl.begin(gSettings, millis());       // boot hold starts now (4.4)
  gTc.begin(gSettings, millis());
  gHc.begin(gSettings, millis());
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
  {
    // The SD logging task (5.4). It writes nothing until the clock is set (first sweep).
    static char hr[2600], he[700], hh[2600];
    LogHeaderInfo info;
    info.firmware = gVersion;
    info.profile = kBuildProfile;
    info.testBuild = kTestBuild;
    formatReadingsHeader(hr, sizeof(hr), info);
    formatEventsHeader(he, sizeof(he), info);
    formatHourlyHeader(hh, sizeof(hh), info);
    sdlog::begin(hr, he, hh);
  }
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
        eventlog::log("exercise", "RELAY EXERCISE: started. Compressor and heater may not start; cycling once both are off.");
        break;
      case RelayExercise::Event::Energised:
        eventlog::log("exercise", "RELAY EXERCISE: cycle %u/%u, K1 energised", (unsigned)cyc, (unsigned)gCtl.exercise().cycles());
        break;
      case RelayExercise::Event::Released:
        eventlog::log("exercise", "RELAY EXERCISE: cycle %u/%u, K1 released", (unsigned)cyc, (unsigned)gCtl.exercise().cycles());
        break;
      case RelayExercise::Event::Done:
        eventlog::log("exercise", "RELAY EXERCISE: done, three cycles. K1 on the cooling side.");
        break;
      default: break;
    }
  }
}

static void printControlEvents() {
  char line[240];
  for (ControlEvent e; gTc.takeEvent(e);) {
    formatControlEvent(line, sizeof(line), e, gSettings);
    const char* type = "control";
    switch (e.type) {
      case ControlEvent::Type::StripStart: case ControlEvent::Type::StripStop: type = "strip"; break;
      case ControlEvent::Type::HarvestStart: case ControlEvent::Type::HarvestEnd: type = "harvest"; break;
      case ControlEvent::Type::Crossover: type = "crossover"; break;
      default: type = "ineffective"; break;     // heater / cooling ineffective or effective again
    }
    eventlog::log(type, "%s", line);
  }
}

static void printHumidEvents() {
  char line[240];
  for (HumidEvent e; gHc.takeEvent(e);) {
    formatHumidEvent(line, sizeof(line), e, gSettings);
    const bool call = e.type == HumidEvent::Type::CallStart || e.type == HumidEvent::Type::CallEnd;
    eventlog::log(call ? "humidify" : "ineffective", "%s", line);
  }
}

static void printFanEvents() {
  uint32_t k = 0;
  for (FanMonitor::Event e; (e = gFanMon.takeEvent(&k)) != FanMonitor::Event::None;) {
    switch (e) {
      case FanMonitor::Event::Stall:
        eventlog::log("fan", "FAN STALL: tach reads zero with PWM asserted. Compressor and heater held off.");
        break;
      case FanMonitor::Event::Kick:
        eventlog::log("fan", "FAN STALL: kickstart %lu (100%% for 1 s); next in %lu s if the tach still reads zero",
                      (unsigned long)k, (unsigned long)(gSettings.safety.fanStallRetryMs / 1000));
        break;
      case FanMonitor::Event::Recovered:
        eventlog::log("fan", "FAN RECOVERED: tach reads %lu rpm. Loads may resume.", (unsigned long)fan::rpm());
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
  gLogicReadings = r;
  for (int i = 0; i < kNumSensors; i++) if (gHealth.wantsSerial(i)) i2cbus::wantSerial(i);

  // CRC failures and timeouts per channel (5.4): counted always; up to 10 an hour each logged.
  for (int i = 0; i < kNumSensors; i++) {
    const BusResult b = gBusReadings.s[i].bus;
    if (b != BusResult::CrcFail && b != BusResult::ShortRead) continue;
    const bool crc = b == BusResult::CrcFail;
    const uint32_t n = crc ? ++gCrcCount[i] : ++gTimeoutCount[i];
    if (gBusLimit.allow(i, now))
      eventlog::log(crc ? "crc" : "timeout", "ch%d %s: %s (%lu since boot)", channelOf(i), kSensors[i].position,
                    crc ? "CRC failure" : "read timed out", (unsigned long)n);
  }

  if (i2cbus::busStuckCount() != gStuckSeen) {
    gStuckSeen = i2cbus::busStuckCount();
    eventlog::log("i2c", "I2C BUS STUCK with ch%d selected: ch%d excluded, mux reset pulsed, I2C restarted (%lu since boot). "
                  "RTC and other sensors carry on.",
                  channelOf(i2cbus::lastStuckIndex()), channelOf(i2cbus::lastStuckIndex()), (unsigned long)gStuckSeen);
  }
  SensorHealth::Event e;
  while (gHealth.takeEvent(e)) {
    if (e.excluded) eventlog::log("sensor", "SENSOR EXCLUDED: ch%d %s - %s", channelOf(e.index),
                                  kSensors[e.index].position, exclusionName(e.reason));
    else            eventlog::log("sensor", "SENSOR RECOVERED: ch%d %s - 10 good reads, serial re-checked",
                                  channelOf(e.index), kSensors[e.index].position);
  }

  if (i2cbus::lastSweepWasBoot()) {       // (the OSF flag itself was read before logging started)
    if (!gClock.osfKnown)
      eventlog::log("rtc", "!! RTC status register not read: lost-time check (OSF) not done this boot");
    else if (gClock.osfAtBoot) {
      Serial.println("\n  !! RTC LOST TIME: the DS3231's oscillator stopped while unpowered (OSF set).");
      Serial.println("  !! Timestamps are UNTRUSTED until NTP corrects the clock. Control is unaffected.");
      Serial.println("  !! If this repeats, replace the RTC cell (spec 2.4).\n");
      eventlog::logQuiet("rtc", "RTC LOST TIME at boot: DS3231 oscillator stopped while unpowered (OSF set); "
                                "timestamps untrusted until NTP corrects the clock");
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
    eventlog::log("ntp", "NTP SYNC %lu: system clock set to %s", (unsigned long)timesync::syncCount(), ts);
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
      eventlog::setClock(gClockSet, gClock.trusted());
      eventlog::log("ntp", "NTP: DS3231 within 2 s (%+lld s); not written", (long long)gRtcErrorS);
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
      eventlog::setClock(gClockSet, gClock.trusted());   // the correction itself carries trusted time
      char ts[24];
      timestamp(ts, sizeof(ts));
      eventlog::log("ntp", "NTP correction: DS3231 was %+lld s out; set to %s%s", (long long)gRtcErrorS, ts,
                    hadOsf ? ". OSF cleared; timestamps trusted again" : "");
    } else {
      eventlog::log("ntp", "NTP: DS3231 write FAILED; trying again after the next sweep");
      gCheckRtc = true;
    }
    printed = true;
  }
  return printed;
}

// ---------------------------------------------------------------- logging (5.4)
// The boot event, then SD writing starts — once the clock is set from the RTC (or
// after 10 s without it: rows then carry the clock's own date, time_trusted 0).
static void startLogging() {
  if (gLogStarted || (!gClockSet && uptimeMs() < 10000)) return;
  gLogStarted = true;
  const time_t t = time(nullptr);
  struct tm tmv;
  gmtime_r(&t, &tmv);
  sdlog::start(tmv.tm_year + 1900, tmv.tm_mon + 1);
  eventlog::log("boot", "BOOT: reason %s; firmware %s; profile %s%s; user settings %s; clock %s", gBoot.bootReason, gVersion,
                kBuildProfile == Profile::Bench ? "BENCH" : "REAL", kTestBuild ? " TEST-BUILD" : "", gBoot.userNote,
                !gClockSet ? "NOT SET (RTC unreadable)" : gClock.trusted() ? "set from the RTC" : "set from the RTC, LOST TIME");
}

static void logReadingsRow(uint32_t now) {
  const LoadOutputs l = outputs::current();
  const uint32_t interval = (l.cool || l.heat) ? gSettings.tuning.logActiveMs : gSettings.tuning.logSteadyMs;
  if (!gLogStarted || (gLastRowMs != 0 && elapsedMs(now, gLastRowMs) + 1000 < interval)) return;
  gLastRowMs = now;
  char ts[24];
  eventlog::isoNow(ts, sizeof(ts));
  ReadingsRow row;
  row.timestamp = ts;
  row.timeTrusted = eventlog::clockTrusted();
  for (int i = 0; i < kNumSensors; i++) {
    const SensorReading& v = gLogicReadings.s[i];
    row.ch[i].read = v.bus == BusResult::Ok;
    row.ch[i].tempC = v.tempC;
    row.ch[i].rhRawPct = v.rhRawPct;
    row.ch[i].rhPct = correctedRh(i, v.rhRawPct);
    row.ch[i].flag = gHealth.healthy(i) ? "ok" : exclusionName(gHealth.reason(i));
    const SensorReading& b = gBusReadings.s[i];
    row.real[i].read = b.bus == BusResult::Ok;
    row.real[i].tempC = b.tempC;
    row.real[i].rhRawPct = b.rhRawPct;
    row.real[i].rhPct = correctedRh(i, b.rhRawPct);
    row.real[i].flag = busResultName(b.bus);
  }
  row.cab = gHealth.summary();
  row.fanPct = fan::duty();
  row.fanRpm = fan::rpm();
  row.cool = l.cool;
  row.heaterDutyPct = l.heat ? gSettings.tuning.heaterPwmCapPct : 0;
  row.humid = l.humid;
  row.relayHeating = l.relayHeating;
  row.loopMaxUs = gLoopMaxLog.take();
  if (!sdlog::writing()) gHeapLast = ESP.getFreeHeap();   // never sampled during a card write (5.2)
  row.heapFree = gHeapLast;
  row.sdWorstMs = sdlog::takeWorstMs();
  static char line[600];   // = the SD queue line size (sd_log.cpp kLine)
  formatReadingsRow(line, sizeof(line), row, kTestBuild);
  sdlog::reading(line);
}

// After each sweep: the hourly figures; a row when the UTC hour changes.
static void logHourly() {
  const time_t t = time(nullptr);
  struct tm tmv;
  gmtime_r(&t, &tmv);
  const uint32_t key = (uint32_t)(((tmv.tm_year * 366) + tmv.tm_yday) * 24 + tmv.tm_hour);
  char stamp[24];
  strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:00:00Z", &tmv);
  if (!gHourlyStarted || key != gHourly.hourKey()) {
    if (gHourlyStarted && gHourly.samples() > 0 && gLogStarted) {
      static char row[1500];
      gHourly.formatRow(row, sizeof(row));
      sdlog::hourly(row);
    }
    gHourly.begin(key, stamp, eventlog::clockTrusted());
    gHourlyStarted = true;
  }
  bool ok[kNumSensors];
  float tc[kNumSensors], rh[kNumSensors];
  for (int i = 0; i < kNumSensors; i++) { ok[i] = gHealth.healthy(i); tc[i] = gHealth.tempC(i); rh[i] = gHealth.rhPct(i); }
  gHourly.addSweep(ok, tc, rh, gHealth.summary(), fan::duty(), fan::rpm());
}

// Load on/off and interlock events, with the reason (5.4). Quiet on serial: the NOW LIVE
// block above already shows the change; these go to the card and the 'events' list.
static void logLoadChanges(const LoadOutputs& was, const LoadOutputs& now) {
  const GuardDecisions& d = gCtl.decisions();
  bool scripted = kBootDemand;
#if defined(WINEADOR_TEST_BUILD)
  scripted = scripted || (gScenario.running() && !gScenario.usesControl());
#endif
  const CabinetSummary c = gHealth.summary();
  char why[80];
  if (was.cool != now.cool) {
    if (now.cool) {
      if (scripted) snprintf(why, sizeof(why), "scenario asks directly");
      else if (gTc.stripping()) snprintf(why, sizeof(why), "RH strip, mean RH %.1f%%", c.meanRhPct);
      else snprintf(why, sizeof(why), "temperature call, control %.2f C", c.controlC);
    } else {
      snprintf(why, sizeof(why), "%s", d.cool.block != GuardBlock::None ? guardBlockName(d.cool.block) : "demand ended");
      gLastCompressorStopMs = uptimeMs();
    }
    eventlog::logQuiet("compressor", "compressor %s (%s)", now.cool ? "ON" : "OFF", why);
  }
  if (was.heat != now.heat) {
    if (now.heat) snprintf(why, sizeof(why), "%s", scripted ? "scenario asks directly" : "temperature call");
    else snprintf(why, sizeof(why), "%s", d.heat.block != GuardBlock::None ? guardBlockName(d.heat.block) : "demand ended");
    if (now.heat && !scripted) snprintf(why, sizeof(why), "temperature call, control %.2f C", c.controlC);
    eventlog::logQuiet("heater", "heater %s (%s)", now.heat ? "ON" : "OFF", why);
  }
  if (was.humid != now.humid) {
    if (now.humid) {
      if (scripted) snprintf(why, sizeof(why), "scenario asks directly");
      else if (gHc.call()) snprintf(why, sizeof(why), "humidify call, mean RH %.1f%%", c.meanRhPct);
      else snprintf(why, sizeof(why), "demanded directly (test)");
    } else snprintf(why, sizeof(why), "%s", d.humid.block != GuardBlock::None ? guardBlockName(d.humid.block) : "demand ended");
    eventlog::logQuiet("humidifier", "humidifier %s (%s)", now.humid ? "ON" : "OFF", why);
  }
  if (was.relayHeating != now.relayHeating)
    eventlog::logQuiet("interlock", "K1 to the %s side%s", now.relayHeating ? "heating" : "cooling",
                  gCtl.exercise().cycling() ? " (relay exercise)" : "");
}

// The SD card failing or coming back (the logging task writes the details), and the
// test-build stress test's progress.
static void watchSd(uint32_t now) {
  if (elapsedMs(now, gLastSdCheckMs) < 250) return;
  gLastSdCheckMs = now;
  const sdlog::Stats st = sdlog::stats();
  {
    static uint32_t shown = 0;
    static bool wasActive = false;
    const sdlog::StressStats& x = st.stress;
    auto line = [&](const char* what) {
      eventlog::log("sd", "SD STRESS %s: %lu kHz, retry %s: %lu of %lu writes, %lu failures, %lu retries (first attempt "
                          "failed at: folder %lu, open %lu, header %lu, data %lu); worst write %lu ms",
                    what, (unsigned long)x.khz, x.retry ? "on" : "off", (unsigned long)x.writes, (unsigned long)x.target,
                    (unsigned long)x.failures, (unsigned long)x.retries, (unsigned long)x.atFolder, (unsigned long)x.atOpen,
                    (unsigned long)x.atHeader, (unsigned long)x.atData, (unsigned long)x.worstMs);
    };
    if (x.active && x.writes >= shown + 100) { shown = x.writes - x.writes % 100; line("progress"); }
    if (wasActive && !x.active) { line("DONE"); shown = 0; }
    if (!wasActive && x.active) shown = 0;
    wasActive = x.active;
  }
  if (st.failed && !gSdFailedShown && gLogStarted) {
    gSdFailedShown = true;
    eventlog::log("sd", "SD FLUSH FAILURE: %s. Logging paused (events kept in RAM, readings rows dropped); control "
                        "unaffected; remount tried every 30 s", st.lastError[0] ? st.lastError : "card not mounted");
  } else if (!st.failed && gSdFailedShown) {
    gSdFailedShown = false;
    Serial.println("  SD card back: remounted, logging resumed (details in the card's events file).");
  }
}

void loop() {
  const uint32_t t0 = micros();
  const uint32_t now = millis();
  watchdog::feed();
  eventlog::setClock(gClockSet, gClock.trusted());

  // 1. Minimum job, every pass. Uses the health and readings already to hand; waits for nothing.
  const int cabOk = gHealth.healthyCabinet();
  gGate.update(gHealth.chamberHealthy() && cabOk >= gSettings.safety.minHealthyCabinet,
               cabOk >= gSettings.safety.minHealthyCabinet, now);
  const bool fresh = elapsedMs(now, gLastSweepDoneMs) <= 2 * gSettings.tuning.sensorReadMs;
  const CabinetSummary cab = gHealth.summary();   // healthy sensors only, corrected RH (4.3, 5.3)
  ControlInputs ci;
  ci.nowMs = now;
  ci.cab = cab;
  ci.readingsFresh = fresh;
  ci.live = outputs::current();
#if defined(WINEADOR_TEST_BUILD)
  ci.scriptedDemand = (gScenario.running() && !gScenario.usesControl()) || kBootDemand;
#endif
  const ControlOutput co = gTc.update(ci);
  HumidInputs hi;
  hi.nowMs = now;
  hi.cab = cab;
  hi.readingsFresh = fresh;
  hi.live = ci.live;
  hi.stripping = co.strippingActive;
  hi.scriptedDemand = ci.scriptedDemand;
  const HumidOutput ho = gHc.update(hi);
  Demand demand = co.demand;              // the temperature and humidifier controls ask...
  demand.humid = ho.demand;
#if defined(WINEADOR_TEST_BUILD)
  if (gScenario.usesControl()) demand.humid = demand.humid || gScenario.demand().humid;   // run 21 "demands humidify"
  if (gScenario.running() && !gScenario.usesControl()) demand = gScenario.demand();   // ...or a stage 2-3 scenario
  else if (kBootDemand) { demand = Demand{}; demand.heat = true; demand.humid = true; }   // ...or the boot-demand build
#endif
  PassInputs in;
  in.nowMs = now;
  in.settingsOk = (gBoot.settingsProblem == nullptr);
  in.fanOk = fan::duty() >= gSettings.safety.fanMinDutyPct && fan::rpm() > 0;
  in.readingsFresh = fresh;
  in.cab = cab;
  in.strippingActive = co.strippingActive;
  in.heaterIneffectiveHold = co.heaterIneffectiveHold;
  in.humidIneffectiveHold = ho.ineffectiveHold;
  in.sensorHoldTemp = gGate.tempHolding();
  in.sensorHoldHumid = gGate.humidHolding();
  in.live = outputs::current();
  outputs::apply(gCtl.pass(in, demand));
  gGuardTrack.update(gCtl.decisions());
  for (GuardBlockTracker::Change ch; gGuardTrack.take(ch);) {
    char g[120];
    GuardBlockTracker::format(g, sizeof(g), ch);
    eventlog::log("guard", "%s", g);
  }
  gHourly.addOnTime(elapsedMs(now, gLastPassMs), outputs::current());
  gLastPassMs = now;
  // Heater PWM cap (starting value 100 %: full duty). Gates the pin only while the heater is on.
  {
    const bool heatOn = outputs::current().heat;
    if (heatOn && !gHeatWasOn) gHeatOnSinceMs = now;
    gHeatWasOn = heatOn;
    outputs::heaterGate(heaterPinOn(now, gHeatOnSinceMs, gSettings.tuning.heaterPwmCapPct,
                                    gSettings.tuning.heaterPwmPeriodMs));
  }
#if defined(WINEADOR_TEST_BUILD)
  resetrec::heartbeat((uint32_t)(uptimeMs() / 1000), outputs::current());   // RTC memory, cheap
#endif

  // 2. The fan: the duty the control asks for (4.6); a start from 0 % kickstarts.
  //    After each tach window, the stall check (5.6) may ask for a kickstart.
  if (co.fanPct != gFan.target()) gFan.command(co.fanPct, now);
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
    if (i2cbus::lastSweepWasBoot()) {
      // The lost-time flag (OSF), before anything is logged: until NTP corrects a
      // clock that lost time, every event and row is marked untrusted (5.4).
      gClock.osfKnown = i2cbus::rtcStatusKnown();
      gClock.osfAtBoot = i2cbus::rtcOsf();
    }
    setClockFromRtc();                    // first, so the boot event and every row after it carry the RTC's time
    eventlog::setClock(gClockSet, gClock.trusted());
    startLogging();
    afterSweep(now);
    logHourly();
    logReadingsRow(now);
    if (i2cbus::lastSweepWasBoot()) {
      console::printSensorCheck();
      printNow = true;                    // one status line straight after the boot check
    }
    if (gHeapBaseline == 0) gHeapBaseline = ESP.getFreeHeap();   // after all initialisation
  }
  if (afterNtp(now)) printNow = true;
  startLogging();                         // the 10 s fallback if the clock never came
  watchSd(now);
  {
    int idx;
    uint32_t more;
    while (gBusLimit.takeSuppressed(now, &idx, &more))
      eventlog::log("crc", "ch%d: %lu more CRC failures / timeouts in the last hour (only the first 10 logged)",
                    channelOf(idx), (unsigned long)more);
  }

  // Status line on its own 15 s timer, so a stuck bus cannot stop it.
  if (elapsedMs(now, gLastStatusMs) >= gSettings.tuning.sensorReadMs) {
    gLastStatusMs = now;
    printNow = true;
  }

  // 4. Commands — read what has arrived, never wait for more
  for (int i = 0; i < 64 && Serial.available(); i++) {
    if (gLine.feed((char)Serial.read())) {
      CommandArgs args;
      Command c = parseCommand(gLine.line(), kTestBuild, args);
      handleCommand(c, args, now);
    }
  }

  printExerciseEvents();
  printControlEvents();
  printHumidEvents();

  // 5. A load changed: show what is live now, and a status line at once
  LoadOutputs loads = outputs::current();
  uint8_t fanNow = fan::duty();
  if (loads != gShownLoads) {
    char why[120];
    describeChange(why, sizeof(why), gShownLoads, loads);
    logLoadChanges(gShownLoads, loads);
    gShownLoads = loads;
    gShownFan = fanNow;
    printLive(why);
    printNow = true;
  } else if (fanNow != gShownFan) {
    // Fan-only changes (harvest steps, kickstarts) get one short line, not the whole block.
    Serial.printf("  plenum fan %u%% -> %u%%%s\n", (unsigned)gShownFan, (unsigned)fanNow,
                  gFan.kicking() ? " (kickstart, 1 s)" : gTc.harvest() == HarvestKind::Cooling ? " (cooling harvest)" :
                  gTc.harvest() == HarvestKind::Heating ? " (heating harvest)" : "");
    gShownFan = fanNow;
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
  view.fanOut = fan::duty();
  view.fanTarget = gFan.target();
  view.hc = &gHc;
  const bool wasRunning = gScenario.running();
  gScenario.update(view, gCtl, gModel, gTc);
  // Every scenario line also goes to the card (quietly: it is printed here already), so a
  // long run's progress and its PASS / FAIL survive a lost USB link (B4).
  for (const char* l; (l = gScenario.takeLine()) != nullptr;) {
    Serial.println(l);
    while (*l == ' ') l++;
    eventlog::logQuiet("scenario", "%s", l);
  }
  if (wasRunning && !gScenario.running()) printNow = true;
#endif

#if !defined(WINEADOR_TEST_BUILD)
  (void)sweptThisPass;
#endif
  if (printNow) printStatus();
  const uint32_t passUs = micros() - t0;
  gLoopMax.add(passUs);
  gLoopMaxLog.add(passUs);
}
