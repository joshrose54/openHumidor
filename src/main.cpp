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
#include "wineador/event_text.h"
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
#include "wineador/supply.h"
#include "wineador/wdi_toggle.h"
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
#include "drivers/heap.h"
#include "drivers/hw_watchdog.h"
#include "drivers/supply_sense.h"
#include "drivers/blowers.h"
#include "drivers/ion_store.h"
#include "drivers/backlight.h"
#include "drivers/lcd.h"
#include "board.h"
#include "app/console.h"
#include "app/eventlog.h"
#include "app/serial_ui.h"
#include "app/screen.h"
#include "wineador/lcd_ui.h"
#include "app/web_server.h"
#if defined(WINEADOR_BOARD_REVQ)
#include "esp_ota_ops.h"
#include "app/web_link.h"
#include "app/ota.h"
#include "app/identity.h"
#include "wineador/ota_update.h"
#include "wineador/ota_rules.h"
#include "wineador/web_mail.h"
#include "wineador/web_status.h"
#endif
#include "wineador/blower_rule.h"
#include "wineador/ion_schedule.h"
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
static BusEventLimiter gDipLimit;        // Rev Q: 12V dips, 10 an hour (index 0 only; 5.4)
static BusEventLimiter gWifiLimit;       // Rev Q: WiFi outages, 10 an hour (index 0 only; 5.4)
static bool gDipLogged = false;          // the dip in progress was logged, so its recovery is too
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
static bool gFirstSweepDone = false;     // the LCD judges sensor alerts only after it (5.5)
static bool gStepRounded = false;        // a stored setpoint was off its step: rounded at boot (5.5)
static float gWasTemp = 0, gWasRh = 0;
#if defined(WINEADOR_BOARD_REVQ)
static bool gWebOn = false;              // the web dashboard is running (5.5)
static uint32_t gSlotBytes = 0;          // the OTA app slot, read at boot
// OTA (5.2, stage Q5d): the loop's side, the record, "started properly"
static OtaLoop gOta;
static OtaRecord gOtaRecord;             // what the last update did (NVS)
static OtaBoot gOtaBoot;                 // read at boot
static bool gOtaBootLogged = false;
static StartedProperly gStarted;         // fed every pass while this image is pending
static bool gPendingVerify = false;
static uint32_t gRollBackAtMs = 0;       // the self-rollback is due (0: none)
static bool gRollbackStop = false;       // ROLLBACK TEST IMAGE: the watchdog line has been stopped
static uint32_t gRollbackStopMs = 0;
static BusEventLimiter gOtaLimit;        // refused uploads, 10 an hour (index 0 only; 5.4)
static bool otaUpdating() { return gOta.updating(); }
// The blowers (4.6b) and the ion generator (4.6c), stage Q6
static BlowerRule gBlowers;
static IonSchedule gIon;
static uint8_t gIonBlob[IonSchedule::kBlobBytes];   // the cap record: read at boot, written at each run's start and end
static size_t gIonBlobLen = 0;
#else
static bool otaUpdating() { return false; }
#endif
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

// "| excl ch4 absent, ch1 crc | sensor hold 12s" — empty when all six are healthy.
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
    add("%sch%d %s", first ? "| excl " : ", ", i, exclusionName(gHealth.reason(i)));
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
  snprintf(net, sizeof(net), "wifi %s ntp %s",
           !timesync::radioOn() ? "off (NTP only)" : timesync::wifiUp() ? "up" : "down",
           gClock.ntpSynced ? "ok" : "waiting");
  s.netText = net;
  char sd[48];
  {
    const sdlog::Stats st = sdlog::stats();
    if (sdlog::stalledMs(millis()) > 5000) snprintf(sd, sizeof(sd), "SD STALLED %lus", (unsigned long)(sdlog::stalledMs(millis()) / 1000));
    else if (st.failed) snprintf(sd, sizeof(sd), "SD FAIL (logging paused)");
    else if (!sdlog::started()) snprintf(sd, sizeof(sd), "sd waiting for clock");
    else if (st.paused) snprintf(sd, sizeof(sd), "sd paused (WiFi on) %lu/%u", (unsigned long)st.rowsBuffered,
                                 (unsigned)board::kSdRowsKept);
    else snprintf(sd, sizeof(sd), "sd ok %lu/16", (unsigned long)st.rowsBuffered);
  }
  s.sdText = sd;
  char brd[340] = "";
  {
    int k = 0;
    auto add = [&](const char* fmt, auto... a) { if (k >= 0 && (size_t)k < sizeof(brd)) k += snprintf(brd + k, sizeof(brd) - k, fmt, a...); };
    if (supplysense::fitted()) {
      if (supplysense::logic().haveAverage()) add("12V %.2f%s", supplysense::logic().lastAverage(),
                                                 supplysense::logic().inDip() ? " DIP" : "");
      else add("12V --");
    }
    if (heap::psramTotal() > 0) add("%spsram min %lu", k ? " | " : "", (unsigned long)heap::psramLowest());
    if (hwdog::fitted()) add("%shwdog %s max %lums", k ? " | " : "", hwdog::running() ? "on" : "OFF",
                             (unsigned long)hwdog::longestGapMs());
#if defined(WINEADOR_BOARD_REVQ)
    {
      char b[48], i[48];
      formatBlowerState(b, sizeof(b), gBlowers, millis());
      formatIonState(i, sizeof(i), gIon);
      add("%sblowers %s | %s", k ? " | " : "", b, i);
    }
#endif
    if (lcd::fitted()) {
      char l[100];
      screen::formatStatus(l, sizeof(l));
      add("%s%s", k ? " | " : "", l);
      screen::resetMax();                 // the worst figures are per status line
    }
  }
  s.boardText = brd;
  s.loopMaxUs = gLoopMax.take();
  s.heapFree = gHeapLast ? gHeapLast : heap::freeNow();   // internal RAM only (5.2)
  s.heapMin = heap::lowest();
  s.rtcOk = i2cbus::rtcOk();
  s.i2cPullupsOn = !i2cbus::internalPullupsOff();
  s.readingsAgeMs = elapsedMs(millis(), gLastSweepDoneMs);
  s.staleAfterMs = 2 * gSettings.tuning.sensorReadMs;
  char line[1100];
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
  int k = snprintf(d, sizeof(d), "%s (%s): ", kEvSettingsChanged, what);   // where from (5.4): "LCD" or "serial: ..."
  auto add = [&](const char* fmt, auto... a) { if (k > 0 && (size_t)k < sizeof(d)) k += snprintf(d + k, sizeof(d) - k, fmt, a...); };
  const char* to = kEvChangeArrow;   // " -> " (event_text.h)
  if (was.targetTempC != u.targetTempC) add("target %.1f%s%.1f C; ", was.targetTempC, to, u.targetTempC);
  if (was.rhSetpointPct != u.rhSetpointPct) add("RH setpoint %.0f%s%.0f%%; ", was.rhSetpointPct, to, u.rhSetpointPct);
  if (was.heaterEnable != u.heaterEnable) add("heater %s%s%s; ", was.heaterEnable ? "on" : "off", to, u.heaterEnable ? "on" : "off");
  if (was.humidifierEnable != u.humidifierEnable)
    add("humidifier %s%s%s; ", was.humidifierEnable ? "on" : "off", to, u.humidifierEnable ? "on" : "off");
  if (was.ionEnable != u.ionEnable) add("ion generator %s%s%s; ", was.ionEnable ? "on" : "off", to, u.ionEnable ? "on" : "off");
  if (was.ionRunsPerDay != u.ionRunsPerDay) add("ion runs per day %u%s%u; ", (unsigned)was.ionRunsPerDay, to, (unsigned)u.ionRunsPerDay);
  if (was.ionMinutesPerRun != u.ionMinutesPerRun)
    add("ion minutes per run %u%s%u; ", (unsigned)was.ionMinutesPerRun, to, (unsigned)u.ionMinutesPerRun);
  if (was.manualExcludeMask != u.manualExcludeMask)
    add("manual excludes 0x%02X%s0x%02X; ", was.manualExcludeMask, to, u.manualExcludeMask);
  add("%s", saved ? "saved" : "NOT SAVED (NVS write failed) - lost at the next boot");
  eventlog::log("settings", "%s", d);
}

static bool sameUser(const UserSettings& a, const UserSettings& b) { return sameUserSettings(a, b); }

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
#if defined(WINEADOR_BOARD_REVQ)
  if (gWebOn) Serial.printf("  Web dashboard: http://wineador.local/ (at most 2 connections)%s%s\n",
                            web::problem() ? " - " : "", web::problem() ? web::problem() : "");
  else Serial.printf("  Web dashboard NOT running: %s (control is unaffected)\n", web::problem());
#endif
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
    case Command::Web: {
      char b[400];
      web::formatStatus(b, sizeof(b));
      Serial.printf("  %s\n", b);
      if (web::problem()) Serial.printf("  web: %s\n", web::problem());
      break;
    }
    case Command::Lcd: {
      if (a.arg == -2) { Serial.println("  lcd, or (test builds) lcd stress on|off. Nothing done."); break; }
      if (a.arg >= 0) {
        screen::stress(a.arg == 1);
        Serial.printf("  TEST: LCD stress %s - %s\n", a.arg == 1 ? "ON" : "OFF",
                      a.arg == 1 ? "the whole screen redrawn every second; watch 'lcd' and the loop time"
                                 : "back to redrawing only what changes");
        eventlog::log("lcd", "TEST: LCD stress %s", a.arg == 1 ? "on (whole screen every second)" : "off");
        break;
      }
      if (!screen::running()) { Serial.printf("  LCD: not running (%s)\n", screen::problem()); break; }
      const screen::Stats st = screen::stats();
      Serial.printf("  LCD %s, pieces of %u bytes (two send buffers, %u bytes of internal RAM)\n",
                    lcdOrientationName(lcd::orientation()), (unsigned)lcd::pieceBytes(), (unsigned)board::kLcdSendBytes);
      Serial.printf("  worst since the last status line: drawing %.2f ms, sending %.2f ms, touch read %.2f ms, logic %.2f ms, "
                    "the whole screen part of a pass %.2f ms (limit 50 ms)\n",
                    st.drawMaxUs / 1000.0, st.sendMaxUs / 1000.0, st.touchMaxUs / 1000.0, st.logicMaxUs / 1000.0,
                    st.passMaxUs / 1000.0);
      Serial.printf("  last frame %lu ms, worst %lu ms; %lu frames, %lu pieces sent; touch reads failed %lu%s\n",
                    (unsigned long)st.lastFrameMs, (unsigned long)st.maxFrameMs, (unsigned long)st.frames,
                    (unsigned long)lcd::piecesSent(), (unsigned long)st.touchFails, screen::stressing() ? " | STRESS ON" : "");
      break;
    }
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
        if (temp) Serial.printf("  REFUSED: target temperature must be %.0f-%.0f C in steps of %.1f C. Still %.1f C.\n",
                                kTargetTempMinC, kTargetTempMaxC, kTargetTempStepC, was.targetTempC);
        else      Serial.printf("  REFUSED: RH setpoint must be %.0f-%.0f%% in steps of %.0f point. Still %.0f%%.\n",
                                kRhSetpointMinPct, kRhSetpointMaxPct, kRhSetpointStepPct, was.rhSetpointPct);
      } else if (sameUser(was, gSettings.user)) Serial.println("  No change.");
      else applyUser(was, temp ? "serial: set temp" : "serial: set rh");
      break;
    }
    case Command::SetHeater:
    case Command::SetHumid:
    case Command::SetIon: {
      if (arg < 0) { Serial.println("  set heater on|off, set humid on|off, set ion on|off. Nothing changed."); break; }
      const UserSettings was = gSettings.user;
      (c == Command::SetHeater ? gSettings.user.heaterEnable
       : c == Command::SetHumid ? gSettings.user.humidifierEnable : gSettings.user.ionEnable) = (arg == 1);
      if (sameUser(was, gSettings.user)) Serial.println("  No change.");
      else applyUser(was, c == Command::SetHeater ? "serial: set heater" : c == Command::SetHumid ? "serial: set humid" : "serial: set ion");
      break;
    }
    case Command::SetIonRuns:
    case Command::SetIonMinutes: {
      const bool runs = c == Command::SetIonRuns;
      if (arg < 0) { Serial.println("  set ionruns <0-4>, set ionmin <0-5> (whole numbers). Nothing changed."); break; }
      const UserSettings was = gSettings.user;
      const uint8_t r = runs ? (uint8_t)arg : was.ionRunsPerDay, m = runs ? was.ionMinutesPerRun : (uint8_t)arg;
      if ((runs && arg > kIonRunsMax) || (!runs && arg > kIonMinutesMax)) {
        Serial.printf("  REFUSED: ion generator %s must be 0-%u. Still %u.\n", runs ? "runs per day" : "minutes per run",
                      (unsigned)(runs ? kIonRunsMax : kIonMinutesMax), (unsigned)(runs ? was.ionRunsPerDay : was.ionMinutesPerRun));
        break;
      }
      if (!ionRunsOk(r, m)) {
        Serial.printf("  REFUSED: %u runs of %u min is %u min a day; %s. Still %u runs of %u min.\n", (unsigned)r, (unsigned)m,
                      (unsigned)(r * m), kIonTooMuchText, (unsigned)was.ionRunsPerDay, (unsigned)was.ionMinutesPerRun);
        break;
      }
      gSettings.user.ionRunsPerDay = r;
      gSettings.user.ionMinutesPerRun = m;
      Serial.printf("  ion generator: %u runs a day of %u min\n", (unsigned)r, (unsigned)m);
      if (sameUser(was, gSettings.user)) Serial.println("  No change.");
      else applyUser(was, runs ? "serial: set ionruns" : "serial: set ionmin");
      break;
    }
    case Command::SetDefaults: {
      const UserSettings was = gSettings.user;
      gSettings.user = UserSettings{};
      if (sameUser(was, gSettings.user)) Serial.println("  Already at the defaults.");
      else applyUser(was, "serial: set defaults");
      break;
    }
    case Command::Exclude: {
      if (arg < 0 || arg > 5) {
        Serial.println("  exclude <sensor> on|off - sensor = 0-5, a position (e.g. mid_rear) or a probe label "
                       "(e.g. A1C5). Nothing changed.");
        break;
      }
      const UserSettings was = gSettings.user;
      const uint8_t bit = (uint8_t)(1u << arg);
      if (a.arg2 == 1) gSettings.user.manualExcludeMask |= bit;
      else gSettings.user.manualExcludeMask &= (uint8_t)~bit;
      // Echo what was acted on (spec 5.5): channels were 2-7 before schema 2, so an old number shows up here.
      char e[80];
      const bool same = sameUser(was, gSettings.user);
      formatChannelEcho(e, sizeof(e), arg, a.arg2 == 1 ? (same ? "already excluded, no change" : "excluded")
                                                        : (same ? "already in use, no change" : "back in use"));
      Serial.printf("  %s\n", e);
      if (!same) applyUser(was, a.arg2 == 1 ? "serial: exclude on" : "serial: exclude off");
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
    case Command::HwDog:
      // TEST BUILDS ONLY (B3 test 11's second run on Rev Q): with the hardware watchdog paused,
      // 'stall' is caught by the 5 s software watchdog instead.
      if (!hwdog::fitted()) { Serial.println("  This board has no hardware watchdog. Nothing done."); break; }
      if (arg < 0) { Serial.println("  hwdog off | hwdog on. Nothing done."); break; }
      if (arg == 0) {
        hwdog::pause();
        Serial.println("  TEST: HARDWARE WATCHDOG PAUSED - GPIO1 an input, both pulls off. 'stall' now waits for the");
        Serial.println("  5 s software watchdog. 'hwdog on' resumes it. (A reset resumes it too.)");
        eventlog::log("hwdog", "TEST: hardware watchdog paused ('hwdog off')");
      } else {
        hwdog::resume(nowMs);
        Serial.println("  TEST: hardware watchdog running again - GPIO1 toggled every pass.");
        eventlog::log("hwdog", "TEST: hardware watchdog resumed ('hwdog on')");
      }
      break;
    case Command::Rtc: {
      // TEST BUILDS ONLY. 'rtc': the DS3231's state. 'rtc eosc on|off' (B3 tests 16, 24 on Rev Q):
      // EOSC set stops the oscillator once the clock runs on its cell, so a power cut loses time.
      const uint8_t ctl = i2cbus::rtcControl();
      DateTime dt;
      char t[32] = "--";
      if (i2cbus::rtcOk() && i2cbus::rtcDateTime(dt))
        snprintf(t, sizeof(t), "%04d-%02d-%02dT%02d:%02d:%02dZ", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
      if (arg == -1) {
        Serial.printf("  RTC: %s %s | OSF %s | control 0x%02X, EOSC %s | I2C1 restarts %lu\n", t,
                      i2cbus::rtcOk() ? "(read ok)" : "(NOT READ)",
                      !i2cbus::rtcStatusKnown() ? "unknown" : i2cbus::rtcOsf() ? "SET (lost time)" : "clear", (unsigned)ctl,
                      (ctl & kDs3231Eosc) ? "SET (the clock stops on the cell)" : "clear",
                      (unsigned long)i2cbus::rtcBusRestarts());
        break;
      }
      if (arg < 0) { Serial.println("  rtc | rtc eosc on | rtc eosc off. Nothing done."); break; }
      i2cbus::requestEosc(arg == 1);
      Serial.printf("  TEST: EOSC %s requested (control now 0x%02X); the result follows.\n", arg == 1 ? "SET" : "CLEAR",
                    (unsigned)ctl);
      if (arg == 1) Serial.println("  With EOSC set, cut the 12V for a minute: the clock stops on its cell (OSF at the next boot).\n"
                                   "  Every build clears EOSC at boot (3A.11); 'rtc eosc off' clears it now.");
      break;
    }
    case Command::NtpSync:
      if (!board::kWifiOnlyForNtp) { Serial.println("  This board keeps WiFi on; NTP syncs by itself."); break; }
      if (timesync::radioOn()) { Serial.println("  The NTP radio window is already open."); break; }
      timesync::syncNow();
      Serial.println("  NTP radio window opening now (off again after the sync or 2 min).");
      break;
    case Command::InjectCrc:
    case Command::InjectTimeout:
      if (arg < 0 || arg > 5) {
        Serial.println("  crc <sensor> / timeout <sensor> - sensor = 0-5, a position or a probe label. Nothing done.");
        break;
      }
      i2cbus::injectFault(arg, c == Command::InjectCrc ? BusResult::CrcFail : BusResult::ShortRead);
      {
        char e[96];
        formatChannelEcho(e, sizeof(e), arg, c == Command::InjectCrc ? "TEST: its next read will fail its CRC"
                                                                      : "TEST: its next read will time out");
        Serial.printf("  %s\n", e);
      }
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
    case Command::NtpSync:
    case Command::HwDog:
    case Command::Rtc:
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
  hwdog::begin();          // Rev Q: GPIO1 an input, both pulls off - the hardware watchdog disabled until the loop runs
  blowers::begin();        // Rev Q: GPIO15 high - full speed, as undriven, until the rule (4.6b) has a reading
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
  gBoot.version = makeVersionString(gVersion, sizeof(gVersion), board::kFirmwareVersion, kBuildProfile, kTestBuild,
                                    kRollbackTest);
  // BOOT ONLY: the saved user settings (NVS, 5.5) — before anything takes a copy of the settings.
  backlight::begin();      // Rev Q: 5 kHz PWM, backlight OFF at once (the LCD pulls it on until driven; 3A.11)
  gBoot.userNote = userstore::load(gSettings.user);
  // 5.5: a stored setpoint off its step (saved before the steps held) is rounded to the nearest
  // step, saved, and logged as soon as logging starts.
  gStepRounded = roundSetpointsToSteps(gSettings.user, &gWasTemp, &gWasRh);
  if (gStepRounded) userstore::save(gSettings.user);
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
#if defined(WINEADOR_BOARD_REVQ)
  gBlowers.begin(gSettings.tuning);
  gIon.begin(gSettings.safety);
#endif
  i2cbus::begin(millis());
  gBoot.pullupsOff = i2cbus::internalPullupsOff();
  supplysense::begin();    // Rev Q: 12V sense on GPIO8, ADC1 at 12 dB
  // Rev Q: the LCD screens (5.5). The picture in PSRAM and the send buffers, now (before the heap
  // baseline); its own task resets the LCD and clears it to black; backlight off until the first
  // screen has been sent (3A.11).
  screen::begin(millis());

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
    info.board = board::kLogBoard;
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
#if defined(WINEADOR_BOARD_REVQ)
  // BOOT ONLY: the web dashboard (5.5): its PSRAM, the server's tasks on core 0, mDNS (up to 2 s).
  {
    const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
    gSlotBytes = slot ? (uint32_t)slot->size : 0;
    gWebOn = web::begin();
    watchdog::feed();
    // OTA (5.2): what the last update did, from the NVS record and the bootloader's own state
    // BOOT ONLY: the ion generator's cap record (4.6c)
    gIonBlobLen = ionstore::load(gIonBlob, sizeof(gIonBlob));
    ota::loadRecord(&gOtaRecord);
    gOtaBoot = readOtaBoot(gOtaRecord, ota::bootFacts(gVersion));
    if (gOtaBoot.saveRecord) ota::saveRecord(gOtaBoot.record);
    gOtaRecord = gOtaBoot.record;
    gPendingVerify = gOtaBoot.pendingVerify;
    // the image's own identity block (identity.cpp): read here, so it stays in the image
    if (strcmp(firmware::identity().version, gVersion) != 0)
      Serial.printf("  IDENTITY BLOCK MISMATCH: the block says %s, this build is %s\n", firmware::identity().version, gVersion);
    watchdog::feed();
  }
#endif

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

// ---------------------------------------------------------------- the LCD (5.5, Rev Q)
// What the screen shows, from the state the firmware already has; then one small screen step
// (src/app/screen). Then what a touch asked for: Save (applied as any settings change, "LCD"),
// the Allow update window opened. Rev N: screen::update does nothing.
static BuildTag buildTag() {
  if (kBuildProfile == Profile::Bench) return kTestBuild ? BuildTag::BenchTest : BuildTag::Bench;
  return kTestBuild ? BuildTag::Test : BuildTag::None;
}

static void updateScreen(uint32_t now) {
  if (!screen::logicRunning()) return;
  UiInputs in;
  in.nowMs = now;
  in.uptimeMs = uptimeMs();
  in.cabinetOk = gHealth.healthyCabinet();
  in.fresh = elapsedMs(now, gLastSweepDoneMs) <= 2 * gSettings.tuning.sensorReadMs;
  const CabinetSummary cab = gHealth.summary();
  in.meanC = cab.meanC;
  in.meanRhPct = cab.meanRhPct;
  in.targetC = gSettings.user.targetTempC;
  in.rhSetPct = gSettings.user.rhSetpointPct;
  const LoadOutputs l = outputs::current();   // what is running, not what is asked for (5.5)
  in.cool = l.cool;
  in.heat = l.heat;                           // the load, not the PWM pin: on through its off-phase
  in.humid = l.humid;
  in.stripping = gTc.stripping();
  in.bootHold = gCtl.hold().active();
  in.bootHoldLeftMs = gCtl.hold().remainingMs();
  in.updating = otaUpdating();                // OTA (5.2): the LCD shows Updating with its %
#if defined(WINEADOR_BOARD_REVQ)
  in.updatePct = gOta.pct();
#endif
  AlertInputs& a = in.alerts;
  a.sensorsKnown = gFirstSweepDone;
  a.sensorFault = sensorFaultFrom(gHealth.chamberHealthy(), in.cabinetOk, gSettings.safety.minHealthyCabinet,
                                  gGate.tempHolding() || gGate.humidHolding());
  a.fanStalled = gFanMon.stalled();
  a.settingsBad = gBoot.settingsProblem != nullptr;
  a.dip12v = supplysense::fitted() && supplysense::logic().inDip();
  a.heaterIneffective = gTc.heaterIneffective();
  a.coolingIneffective = gTc.coolingIneffective();
  a.humidIneffective = gHc.ineffective();
  for (int i = 0; i < kNumSensors; i++) {
    if (gHealth.healthy(i)) continue;
    a.excludedMask |= (uint8_t)(1u << i);
    if (gHealth.reason(i) == Exclusion::Manual) a.manualMask |= (uint8_t)(1u << i);
  }
  a.sdFailed = sdlog::stats().failed;
  ClockFacts cf;                              // 5.5: lib/core alerts.h
  cf.firstSweepDone = gFirstSweepDone;
  cf.clockSet = gClockSet;
  cf.trusted = gClock.trusted();
  cf.rtcOk = i2cbus::rtcOk();
  cf.ntpSynced = gClock.ntpSynced;
  a.clockBad = clockAlert(cf);
  in.clockOk = !clockBad(cf);
  in.utcNowS = (int64_t)time(nullptr);
  in.wifi = timesync::wifiUp();
  in.tag = buildTag();
  screen::update(in);

  float t, rh;
  if (screen::takeSave(&t, &rh)) {
    const UserSettings was = gSettings.user;
    const bool ok = setTargetTemp(gSettings.user, t) && setRhSetpoint(gSettings.user, rh);
    if (!ok) { gSettings.user = was; eventlog::log("settings", "LCD: settings refused (%.1f C, %.0f%%)", (double)t, (double)rh); }
    else if (!sameUser(was, gSettings.user)) applyUser(was, "LCD");
  }
  if (screen::takeWindowOpened())
    eventlog::log("lcd", "LCD: Allow update pressed - an update may be sent in the next 5 minutes (5.5)");
  uint32_t us, count;
  const char* what;
  if (screen::takeLongStep(&us, &what, &count))
    eventlog::log("lcd", "LCD STEP OVER 50 ms: the screen's part of a pass took up to %lu ms (longest part: %s), "
                  "%lu time(s) - 3A.11", (unsigned long)(us / 1000), what, (unsigned long)count);
}

// ---------------------------------------------------------------- the web dashboard (5.5, Rev Q)
// The server (app/web_server) runs on its own task and never touches the control objects. The loop
// does two things for it each pass, neither of which ever waits:
//   - the mailbox: a write or the start of an upload from the dashboard, decided here against the
//     real settings and state, applied as any other change (logged "dashboard"), answered;
//   - the state copy: about once a second (and straight after a change), what the pages show.
#if defined(WINEADOR_BOARD_REVQ)
static uint32_t gLastWebStateMs = 0;
static bool gWebStateDue = true;
static DayLow gV12Low;                   // the Board section's lowest 12V today
static SaveLimit gSaveLimit;             // 5.5: one saved dashboard change every 10 s
static uint32_t gLastV12Ms = 0;
static bool gCoolWas = false;
static uint64_t gCoolSinceMs = 0;
static int64_t gCountHour = -1;          // the hour the "this hour" bus counts started
static uint32_t gCrcHourBase[kNumSensors] = {}, gTimeoutHourBase[kNumSensors] = {};

static void fillWebState(uint32_t now) {
  weblink::State* st = weblink::staging();
  if (!st) return;
  StatusSnapshot& x = st->status;
  x = StatusSnapshot();
  x.build.board = board::kIdentityBoard;
  x.build.profile = kBuildProfile;
  x.build.testBuild = kTestBuild;
  x.build.version = gVersion;
  const uint64_t up = uptimeMs();
  x.uptimeS = up / 1000;
  ClockFacts cf;
  cf.firstSweepDone = gFirstSweepDone;
  cf.clockSet = gClockSet;
  cf.trusted = gClock.trusted();
  cf.rtcOk = i2cbus::rtcOk();
  cf.ntpSynced = gClock.ntpSynced;
  const bool clockOk = !clockBad(cf);
  x.utcNowS = (int64_t)time(nullptr);
  copyFromLcd(&x, screen::ui(), up, clockOk, x.utcNowS);     // the LCD's own modes and alerts
  x.bootHold = gCtl.hold().active();
  x.bootHoldLeftS = gCtl.hold().remainingMs() / 1000;
  const CabinetSummary c = gHealth.summary();
  const uint32_t age = elapsedMs(now, gLastSweepDoneMs);
  x.readingsOk = c.cabinetOk >= gSettings.safety.minHealthyCabinet && age <= 2 * gSettings.tuning.sensorReadMs;
  x.cabinetOk = c.cabinetOk;
  x.meanC = c.meanC; x.minC = c.minC; x.maxC = c.maxC; x.spreadC = c.spreadC;
  x.meanRhPct = c.meanRhPct; x.controlC = c.controlC;
  x.readingsAgeS = age / 1000;
  x.targetC = gSettings.user.targetTempC;
  x.rhSetPct = gSettings.user.rhSetpointPct;
  x.coolingHysteresisC = gSettings.tuning.coolingHysteresisC;
  x.heatingDeadbandC = gSettings.tuning.heatingDeadbandC;
  const LoadOutputs l = outputs::current();
  x.cool = l.cool; x.heat = l.heat; x.humid = l.humid; x.relayHeating = l.relayHeating;
  x.stripping = gTc.stripping();
  x.heaterDutyPct = l.heat ? gSettings.tuning.heaterPwmCapPct : 0;
  x.fanPct = fan::duty();
  x.fanRpm = fan::rpm();
  fillBlowerIon(&x, gBlowers, gIon, gSettings.tuning, now);   // 4.6b, 4.6c
  if (l.cool && !gCoolWas) gCoolSinceMs = up;
  gCoolWas = l.cool;
  x.compressorOnForS = l.cool ? (uint32_t)((up - gCoolSinceMs) / 1000) : 0;
  x.compressorStoppedSinceBoot = gLastCompressorStopMs != 0;
  x.compressorStoppedAgoS = gLastCompressorStopMs ? (uint32_t)((up - gLastCompressorStopMs) / 1000) : 0;
  const GuardDecisions& g = gCtl.decisions();
  const LoadDecision* ld[3] = {&g.cool, &g.heat, &g.humid};
  for (int L = 0; L < 3; L++) {
    if (!ld[L]->demanded || ld[L]->run || ld[L]->block == GuardBlock::None) continue;
    GuardHold& h = x.guards[x.nGuards++];
    h.load = (uint8_t)L;
    h.reason = guardBlockName(ld[L]->block);
    h.leftS = ld[L]->waitMs ? (int32_t)(ld[L]->waitMs / 1000) : -1;
  }
  x.disturbanceDetection = gSettings.tuning.disturbanceDetection;   // the week's counts: added by the server
  x.windowOpen = screen::window().active(now);
  x.windowLeftS = x.windowOpen ? screen::window().remainingMs(now) / 1000 : 0;
  x.updating = gOta.updating();
  x.updatePct = gOta.pct();
  const sdlog::Stats sd = sdlog::stats();
  x.sdOk = sd.mounted && !sd.failed;
  x.sdFreeBytes = sd.freeBytes;
  x.sdTotalBytes = sd.totalBytes;
  // the WiFi signal is read by the server itself (the WiFi driver's call can wait for its lock)
  x.bootReason = gBoot.bootReason;
  x.hasV12 = supplysense::fitted() && supplysense::logic().haveAverage() && gV12Low.have();
  x.v12NowV = supplysense::logic().lastAverage();
  x.v12LowTodayV = gV12Low.low();
  x.heapFreeBytes = heap::freeNow();
  // "this hour": the bus counts since the hour began (UTC hours; uptime hours while the clock is bad)
  const int64_t hour = clockOk ? x.utcNowS / 3600 : -2 - (int64_t)(up / 3600000);
  if (hour != gCountHour) {
    gCountHour = hour;
    memcpy(gCrcHourBase, gCrcCount, sizeof(gCrcHourBase));
    memcpy(gTimeoutHourBase, gTimeoutCount, sizeof(gTimeoutHourBase));
  }
  for (int i = 0; i < kNumSensors; i++) {
    SensorStatus& o = x.sensors[i];
    const Exclusion r = gHealth.reason(i);
    o.state = r == Exclusion::None ? SensorState::Healthy : r == Exclusion::Manual ? SensorState::Manual
            : r == Exclusion::NotYetRead ? SensorState::NotRead : SensorState::Excluded;
    if (r != Exclusion::None) snprintf(o.reason, sizeof(o.reason), "%s", exclusionName(r));
    const SensorReading& v = gLogicReadings.s[i];
    o.read = gFirstSweepDone && v.bus == BusResult::Ok;
    o.tempC = v.tempC;
    o.rhRawPct = v.rhRawPct;
    o.rhPct = correctedRh(i, v.rhRawPct);
    o.ageS = age / 1000;
    o.crcHour = (uint16_t)(gCrcCount[i] - gCrcHourBase[i]);
    o.timeoutsHour = (uint16_t)(gTimeoutCount[i] - gTimeoutHourBase[i]);
  }
  st->user = gSettings.user;
  st->tuning = gSettings.tuning;
  UpdateState& u = st->update;
  u = UpdateState();
  u.windowOpen = x.windowOpen;
  u.windowLeftS = x.windowLeftS;
  u.compressorRunning = l.cool;
  u.updating = gOta.updating();
  u.phase = gOta.phase() == OtaLoop::Phase::Writing ? "writing" : gOta.phase() == OtaLoop::Phase::Restarting ? "restarting" : "idle";
  u.pct = gOta.pct();
  u.slotBytes = gSlotBytes;
  u.running = x.build;
  u.pendingVerify = gPendingVerify;
  u.verifyLeftS = gPendingVerify ? gStarted.leftS(now) : 0;
  u.last = (UpdateResult)gOtaRecord.result;
  snprintf(u.lastDetail, sizeof(u.lastDetail), "%s", gPendingVerify ? gOtaBoot.lastDetail : gOtaRecord.detail);
  u.lastUtcS = gOtaRecord.utcS;
}

// ---------------------------------------------------------------- OTA, the loop's side (5.2)
static void otaEvent(const char* text) { eventlog::log(kEvTypeOta, "%s", text); }

static void saveOta(const OtaRecord& r) {
  gOtaRecord = r;
  ota::saveRecord(r);                    // an NVS write, a few ms: only at the update's steps
}

// The writer asks to start, or says how it ended. Answers in the same pass.
static MailAnswer otaMail(const MailRequest& r, uint32_t now) {
  MailAnswer a;
  a.id = r.id;
  a.user = gSettings.user;
  char e[220];
  const int64_t utc = gClockSet ? (int64_t)time(nullptr) : 0;
  if (r.kind == MailKind::UpdateBegin) {
    a.result = gOta.begin(now, r.length, r.version, outputs::current().cool, screen::window(),
                          gPendingVerify ? gStarted.leftS(now) : 0);
    if (a.result.ok()) {
      formatOtaStarted(e, sizeof(e), r.version, r.length, gVersion);
      otaEvent(e);
      saveOta(otaRecordStarted(r.version, gVersion, utc, ota::nextSlot()));
    }
  } else {   // UpdateEnd
    const OtaOutcome o = (OtaOutcome)r.outcome;
    if (gOta.updating()) {               // (if the loop had already ended it, that was logged then)
      gOta.end(now, o);
      formatOtaEnded(e, sizeof(e), o, r.version);
      otaEvent(e);
      saveOta(otaRecordEnded(gOtaRecord, o, utc));
    }
  }
  return a;
}

// Every pass: the writer's progress (the loop's own 70 s watch), the restart, the refused uploads
// (logged, 10 an hour), "started properly" and its 4 min 30 s deadline, the rollback test image.
static void otaPass(uint32_t now) {
  char e[220];
  const int64_t utc = gClockSet ? (int64_t)time(nullptr) : 0;
  if (gOta.watch(now, ota::bytesWritten())) {
    ota::cancel();
    formatOtaLoopGaveUp(e, sizeof(e), gOta.version(), gOta.bytes(), gOta.total());
    otaEvent(e);
    saveOta(otaRecordEnded(gOtaRecord, OtaOutcome::Cancelled, utc));
  }
  if (gOta.restartDue(now)) {
    Serial.println("  OTA: installed; restarting now (loads are off)");
    Serial.flush();
    esp_restart();
  }
  if (gLogStarted && !gOtaBootLogged) {   // the boot's own OTA event once events carry the clock
    gOtaBootLogged = true;
    if (gOtaBoot.logEvent) otaEvent(gOtaBoot.event);
  }
  for (ApiResult r; weblink::takeRefused(&r);) {
    if (!gOtaLimit.allow(0, now)) continue;
    formatOtaRefused(e, sizeof(e), r);
    otaEvent(e);
  }
  int idx;
  uint32_t more;
  while (gOtaLimit.takeSuppressed(now, &idx, &more))
    eventlog::log(kEvTypeOta, "%lu %s in the last hour (only the first 10 logged)", (unsigned long)more, kEvOtaRefusedMore);

  if (kRollbackTest) {
    // B7 test 6: never marked good. Sent by OTA (pending), it stops the watchdog line at 20 s so the
    // board resets and the bootloader rolls back; flashed over USB, there is nothing to roll back to,
    // so it holds every load off and says so.
    static bool said = false;
    if (!said) {
      said = true;
      if (gPendingVerify)
        snprintf(e, sizeof(e), "%s: every load held off; at 20 s the hardware watchdog line stops, so the board resets "
                 "and rolls back to the image before it", kEvOtaRollbackTest);
      else
        snprintf(e, sizeof(e), "%s: not sent by OTA, so there is nothing to roll back to: every load held off. Flash a "
                 "normal build", kEvOtaRollbackTest);
      otaEvent(e);
      Serial.printf("  %s\n", e);
    }
    if (gPendingVerify && !gRollbackStop && uptimeMs() >= 20000) { gRollbackStop = true; gRollbackStopMs = now; }
    if (gRollbackStop && now - gRollbackStopMs >= 10000) esp_restart();   // U5 didn't: a software reset rolls back too
    return;
  }
  if (gPendingVerify) {
    gStarted.pass(now, gBoot.settingsProblem == nullptr, hwdog::running(), gFirstSweepDone);
    if (gStarted.good()) {
      gPendingVerify = false;
      if (ota::markGood()) {
        formatOtaGood(e, sizeof(e), gVersion);
        otaEvent(e);
        saveOta(otaRecordGood(gOtaRecord, utc));
      } else {
        otaEvent("OTA: marking this image good FAILED (esp_ota_mark_app_valid_cancel_rollback)");
      }
    } else if (gStarted.overdue(now) && gRollBackAtMs == 0) {
      formatOtaSelfRollback(e, sizeof(e), gVersion);
      otaEvent(e);
      saveOta(otaRecordSelfRollback(gOtaRecord, utc));
      gRollBackAtMs = now + 2000;        // the event reaches the card first
      if (gRollBackAtMs == 0) gRollBackAtMs = 1;
    }
  }
  if (gRollBackAtMs && (int32_t)(now - gRollBackAtMs) >= 0) ota::rollBackNow();
}

// Each pass: the mailbox, then (about once a second) the state copy. Never waits.
static void serveWeb(uint32_t now) {
  if (!gWebOn) return;
  MailRequest r;
  if (weblink::takeMail(&r) && (r.kind == MailKind::UpdateBegin || r.kind == MailKind::UpdateEnd)) {
    weblink::answerMail(otaMail(r, now));
    gWebStateDue = true;
  } else if (r.id != 0) {
    MailContext c;
    c.user = gSettings.user;
    c.updating = gOta.updating();        // 5.5: settings and excludes refused while Updating
    c.checkingLeftS = gPendingVerify ? gStarted.leftS(now) : 0;
    c.windowOpen = screen::window().active(now);
    c.compressorRunning = outputs::current().cool;
    c.slotBytes = gSlotBytes;
    c.nowMs = now;
    c.limit = &gSaveLimit;
    const MailAnswer a = decideMail(r, c);
    if (a.result.ok()) {
      if (r.kind == MailKind::Settings || r.kind == MailKind::Exclude) {
        const UserSettings was = gSettings.user;
        gSettings.user = a.user;
        if (!sameUser(was, gSettings.user)) {
          applyUser(was, "dashboard");
          gSaveLimit.saved(now);
        }
      } else if (r.kind == MailKind::DoorTest) {
        if (a.note[0]) eventlog::log(kEvTypeDoor, "%s: %s", kEvDoorTest, a.note);
        else eventlog::log(kEvTypeDoor, "%s", kEvDoorTest);
      }
    }
    weblink::answerMail(a);
    gWebStateDue = true;                 // the pages see the change at once
  }
  if (elapsedMs(now, gLastV12Ms) >= 1000) {
    gLastV12Ms = now;
    if (supplysense::fitted() && supplysense::logic().haveAverage())
      gV12Low.add(supplysense::logic().lastAverage(), (int64_t)time(nullptr), gClockSet && gClock.trusted());
  }
  if (gWebStateDue || elapsedMs(now, gLastWebStateMs) >= 1000) {
    fillWebState(now);
    if (weblink::publish()) {            // the server was copying: try again next pass
      gLastWebStateMs = now;
      gWebStateDue = false;
    }
  }
}
#endif

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
      eventlog::log(crc ? "crc" : "timeout", "ch%d %s: %s (%lu since boot)", i, kSensors[i].position,
                    crc ? "CRC failure" : "read timed out", (unsigned long)n);
  }

  if (i2cbus::busStuckCount() != gStuckSeen) {
    gStuckSeen = i2cbus::busStuckCount();
    eventlog::log("i2c", "I2C BUS STUCK with ch%d selected: ch%d excluded, mux reset pulsed, I2C restarted (%lu since boot). "
                  "RTC and other sensors carry on.",
                  i2cbus::lastStuckIndex(), i2cbus::lastStuckIndex(), (unsigned long)gStuckSeen);
  }
  SensorHealth::Event e;
  while (gHealth.takeEvent(e)) {
    if (e.excluded) eventlog::log("sensor", "SENSOR EXCLUDED: ch%d %s - %s", e.index,
                                  kSensors[e.index].position, exclusionName(e.reason));
    else            eventlog::log("sensor", "SENSOR RECOVERED: ch%d %s - 10 good reads, serial re-checked",
                                  e.index, kSensors[e.index].position);
  }

  if (i2cbus::takeEoscCleared())         // 3A.11: a test left EOSC set; cleared at boot
    eventlog::log("rtc", "DS3231 EOSC was SET at boot (the clock would stop on its cell at every power cut): cleared");
  if (i2cbus::lastSweepWasBoot()) {       // (the OSF flag itself was read before logging started)
    if (!gClock.osfKnown)
      eventlog::log("rtc", "!! RTC status register not read: lost-time check (OSF) not done this boot");
    else if (gClock.osfAtBoot) {
      Serial.println("\n  !! RTC LOST TIME: the DS3231's oscillator stopped while unpowered (OSF set).");
      Serial.println("  !! Timestamps are UNTRUSTED until NTP corrects the clock. Control is unaffected.");
      Serial.println("  !! If this repeats, replace the RTC cell (spec 2.4).\n");
      eventlog::logQuiet(kEvTypeRtc, "%s at boot: DS3231 oscillator stopped while unpowered (OSF set); "
                                     "timestamps untrusted until NTP corrects the clock", kEvRtcLostTime);
    }
  }
}

// The heap floor (5.2): an event the first time the lowest free heap since boot
// (ESP-IDF's own low-water mark) goes below it.
// Rev Q events (5.4): 12V dips and their recovery, WiFi lost and back, a long gap in the
// hardware watchdog's toggling. Dips and WiFi outages: up to 10 an hour each, then one
// "N more this hour" line; a dip's recovery (and an outage's "back") is logged when its
// start was. Rev N has none of these, so nothing is ever logged there.
static void logBoardEvents(uint32_t now) {
  SupplySense::Event e;
  while (supplysense::logic().takeEvent(e)) {
    if (e.kind == SupplySense::Event::Kind::Dip) {
      gDipLogged = gDipLimit.allow(0, now);
      if (gDipLogged) eventlog::log(kEvTypeSupply12v, "%s: below %.1f V for 1 s (%.2f V now)", kEvDip12v,
                                    (double)SupplySense::kDipBelowV, (double)e.lowestV);
    } else if (gDipLogged) {
      char d[120];
      formatRecovered12v(d, sizeof(d), SupplySense::kDipBelowV, e.durationMs, e.lowestV);
      eventlog::log(kEvTypeSupply12v, "%s", d);
      gDipLogged = false;
    }
  }
  static bool wifiLogged = false;
  uint32_t outMs = 0;
  const int w = timesync::takeWifiEvent(&outMs);
  if (w < 0) {
    wifiLogged = gWifiLimit.allow(0, now);
    if (wifiLogged) eventlog::log(kEvTypeWifi, "%s", kEvWifiLost);
  } else if (w > 0 && wifiLogged) {
    char d[48];
    formatWifiBack(d, sizeof(d), (outMs + 500) / 1000);
    eventlog::log(kEvTypeWifi, "%s", d);
    wifiLogged = false;
  }
  int idx;
  uint32_t more;
  while (gDipLimit.takeSuppressed(now, &idx, &more))
    eventlog::log("12v", "%lu more 12V dips in the last hour (only the first 10 logged)", (unsigned long)more);
  while (gWifiLimit.takeSuppressed(now, &idx, &more))
    eventlog::log("wifi", "%lu more WiFi outages in the last hour (only the first 10 logged)", (unsigned long)more);
  uint32_t gap;
  if (hwdog::takeLongGap(&gap))
    eventlog::log("hwdog", "HARDWARE WATCHDOG: %lu ms between toggles (the limit is %lu ms; U5 resets at 1.12 s minimum)",
                  (unsigned long)gap, (unsigned long)WdiToggle::kMaxGapMs);
}

static void checkHeapFloor() {
  static bool logged = false;
  if (logged) return;
  const uint32_t lowest = heap::lowest();         // internal RAM only (5.2)
  if (lowest >= wineador::kHeapFloorBytes) return;
  logged = true;
  eventlog::log(kEvTypeHeap, "%s %lu KB FLOOR: lowest since boot %lu bytes; now %lu, largest block %lu",
                kEvHeapFloor, (unsigned long)(wineador::kHeapFloorBytes / 1024), (unsigned long)lowest, (unsigned long)heap::freeNow(),
                (unsigned long)heap::largestBlock());
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
      eventlog::log(kEvTypeNtp, "%s: DS3231 was %+lld s out; set to %s%s", kEvNtpCorrection, (long long)gRtcErrorS, ts,
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
  eventlog::log(kEvTypeBoot, "%s %s; firmware %s; board %s%s%s%s; user settings %s; clock %s", kEvBootLine,
                gBoot.bootReason, gVersion, board::kName, kEvBootProfile,
                kBuildProfile == Profile::Bench ? "BENCH" : "REAL", kTestBuild ? " TEST-BUILD" : "", gBoot.userNote,
                !gClockSet ? "NOT SET (RTC unreadable)" : gClock.trusted() ? "set from the RTC" : "set from the RTC, LOST TIME");
  if (gStepRounded)
    eventlog::log(kEvTypeSettings, "%s (boot): stored setpoints off their steps rounded (5.5): target %.2f%s%.1f C; "
                  "RH setpoint %.2f%s%.0f%%; saved", kEvSettingsChanged, (double)gWasTemp, kEvChangeArrow,
                  (double)gSettings.user.targetTempC, (double)gWasRh, kEvChangeArrow,
                  (double)gSettings.user.rhSetpointPct);
  if (!screen::running() && lcd::fitted())
    eventlog::log("lcd", "LCD SCREENS OFF: %s. Control is unaffected.", screen::problem());
  if (sdlog::stats().psramMissing)
    eventlog::log("psram", "NO PSRAM FOUND at boot: the SD outage buffers are Rev N's sizes in internal RAM "
                  "(%u events, %u readings rows, %u hourly rows) instead of Rev Q's",
                  (unsigned)board_revn::kSdEventsKept, (unsigned)board_revn::kSdRowsKept, (unsigned)board_revn::kSdHourlyKept);
}

static void logReadingsRow(uint32_t now) {
  const LoadOutputs l = outputs::current();
  const uint32_t interval = (l.cool || l.heat) ? gSettings.tuning.logActiveMs : gSettings.tuning.logSteadyMs;
  if (!gLogStarted || (gLastRowMs != 0 && elapsedMs(now, gLastRowMs) + 1000 < interval)) return;
  if (otaUpdating()) return;              // 5.2: no rows while Updating (no sweeps): a break, the events say why
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
  if (!sdlog::writing()) gHeapLast = heap::freeNow();   // never sampled during a card write (5.2)
  row.heapFree = gHeapLast;
  row.heapMin = heap::lowest();                        // internal RAM only (5.2)
  row.sdWorstMs = sdlog::takeWorstMs();
  // Rev Q (5.4): the 12V mean and lowest 1 s average in this interval; the lowest-ever free
  // PSRAM. Empty where the board has neither (Rev N).
  row.hasV12 = supplysense::fitted() && supplysense::logic().takeInterval(&row.v12MeanV, &row.v12Min1sV);
  row.hasPsram = heap::psramTotal() > 0;
  row.psramMin = row.hasPsram ? heap::psramLowest() : 0;
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
      gHourly.noteHeapMin(heap::lowest());
      float mean, low;                     // Rev Q (5.4): the hour's 12V and the lowest free PSRAM
      if (supplysense::fitted() && supplysense::logic().takeHour(&mean, &low)) gHourly.noteSupply(mean, low);
      if (heap::psramTotal() > 0) gHourly.notePsramMin(heap::psramLowest());
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
  char line[120];
  if (was.cool != now.cool) {
    if (now.cool) {
      if (scripted) snprintf(why, sizeof(why), "scenario asks directly");
      else if (gTc.stripping()) snprintf(why, sizeof(why), "RH strip, mean RH %.1f%%", c.meanRhPct);
      else snprintf(why, sizeof(why), "temperature call, control %.2f C", c.controlC);
    } else {
      snprintf(why, sizeof(why), "%s", d.cool.block != GuardBlock::None ? guardBlockName(d.cool.block) : "demand ended");
      gLastCompressorStopMs = uptimeMs();
    }
    formatLoadEvent(line, sizeof(line), 0, now.cool, why);
    eventlog::logQuiet(loadEventType(0), "%s", line);
  }
  if (was.heat != now.heat) {
    if (now.heat) snprintf(why, sizeof(why), "%s", scripted ? "scenario asks directly" : "temperature call");
    else snprintf(why, sizeof(why), "%s", d.heat.block != GuardBlock::None ? guardBlockName(d.heat.block) : "demand ended");
    if (now.heat && !scripted) snprintf(why, sizeof(why), "temperature call, control %.2f C", c.controlC);
    formatLoadEvent(line, sizeof(line), 1, now.heat, why);
    eventlog::logQuiet(loadEventType(1), "%s", line);
  }
  if (was.humid != now.humid) {
    if (now.humid) {
      if (scripted) snprintf(why, sizeof(why), "scenario asks directly");
      else if (gHc.call()) snprintf(why, sizeof(why), "humidify call, mean RH %.1f%%", c.meanRhPct);
      else snprintf(why, sizeof(why), "demanded directly (test)");
    } else snprintf(why, sizeof(why), "%s", d.humid.block != GuardBlock::None ? guardBlockName(d.humid.block) : "demand ended");
    formatLoadEvent(line, sizeof(line), 2, now.humid, why);
    eventlog::logQuiet(loadEventType(2), "%s", line);
  }
  if (was.relayHeating != now.relayHeating)
    eventlog::logQuiet(kEvTypeInterlock, "%s%s", now.relayHeating ? kEvK1Heating : kEvK1Cooling,
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
      SdStressFigures f;
      f.khz = x.khz; f.retry = x.retry; f.writes = x.writes; f.target = x.target;
      f.failures = x.failures; f.retries = x.retries; f.atFolder = x.atFolder; f.atOpen = x.atOpen;
      f.atHeader = x.atHeader; f.atData = x.atData; f.atVerify = x.atVerify; f.worstMs = x.worstMs;
      char d[300];
      formatSdStressLine(d, sizeof(d), strcmp(what, "DONE") == 0, f);
      eventlog::log(kEvTypeSd, "%s", d);
    };
    if (x.active && x.writes >= shown + 100) { shown = x.writes - x.writes % 100; line("progress"); }
    if (wasActive && !x.active) {
      line("DONE");
      shown = 0;
      char d[240];
      formatSdStressChecks(d, sizeof(d), x.sizeMismatch, x.heapMin, x.heapMinLargest, x.heapMinEver, x.pausedMs / 1000);
      eventlog::log(kEvTypeSd, "%s", d);
    }
    if (!wasActive && x.active) {
      shown = 0;
      eventlog::log(kEvTypeSd, "%s: %lu batches at %lu kHz, retry %s", kEvSdStressStart, (unsigned long)x.target, (unsigned long)x.khz,
                    x.retry ? "on" : "off");   // where the run begins, for check_logs.py --stress (BP2 test 38)
    }
    wasActive = x.active;
  }
  // Every failed write attempt (stress test or logging), one event each.
  sdlog::setWifiUp(timesync::wifiUp());
  sdlog::FailRecord r;
  for (int k = 0; k < 4 && sdlog::takeFail(&r); k++) {
    char where[32];
    if (r.stress) snprintf(where, sizeof(where), "stress write %lu", (unsigned long)r.writeNo);
    else snprintf(where, sizeof(where), "logging");
    eventlog::log(kEvTypeSd, "%s%lu %s at %s, %s; took %lu ms; offset %lu +%lu B; heap %lu, largest %lu; wifi %s",
                  kEvSdFailNo, (unsigned long)r.seq, where, r.stage, r.recovered ? "retry OK" : r.retried ? "retry FAILED" : "no retry",
                  (unsigned long)r.attemptMs, (unsigned long)r.fileOffset, (unsigned long)r.len, (unsigned long)r.heapFree,
                  (unsigned long)r.heapLargest, r.wifiUp ? "up" : "down");
  }
  if (st.failed && !gSdFailedShown && gLogStarted) {
    gSdFailedShown = true;
    eventlog::log(kEvTypeSd, "%s: %s. Logging paused (events, readings and hourly rows kept in RAM); control "
                             "unaffected; remount tried every 30 s", kEvSdFlushFailure, st.lastError[0] ? st.lastError : "card not mounted");
  } else if (!st.failed && gSdFailedShown) {
    gSdFailedShown = false;
    Serial.println("  SD card back: remounted, logging resumed (details in the card's events file).");
  }
}

#if defined(WINEADOR_BOARD_REVQ)
// The blowers (4.6b) and the ion generator (4.6c), every pass after the sweep step (stage Q6). Uses
// what the loop already has; waits for nothing (the cap record's NVS write, a few ms, only at a
// run's start and end).
static void passBlowersIon(uint32_t now, bool swept) {
  const bool updating = otaUpdating() || kRollbackTest;   // 5.2: blowers idle, ion off (the rollback test image: always)
  // Fault, 5.5's meaning - the LCD's Fault icon: the 4.3 requirement (with its recovery hold), the
  // plenum fan stalled, or the settings failing their boot check.
  const bool sensorFault = gFirstSweepDone &&
      sensorFaultFrom(gHealth.chamberHealthy(), gHealth.healthyCabinet(), gSettings.safety.minHealthyCabinet,
                      gGate.tempHolding() || gGate.humidHolding()) != SensorFault::None;
  const bool fault = sensorFault || gFanMon.stalled() || gBoot.settingsProblem != nullptr;
  const CabinetSummary cab = gHealth.summary();
  BlowerInputs bi;
  bi.nowMs = now;
  bi.updating = updating;
  bi.fault = fault;
  bi.fresh = gFirstSweepDone && elapsedMs(now, gLastSweepDoneMs) <= 2 * gSettings.tuning.sensorReadMs;
  bi.sweepDone = swept;
  bi.cabinetOk = cab.cabinetOk;
  bi.spreadC = cab.spreadC;
  bi.spreadRhPct = cab.spreadRhPct;
  const uint8_t pct = gBlowers.update(bi);
  if (pct != blowers::pct()) blowers::setPct(pct);
  for (BlowerRule::Change c; gBlowers.take(&c);) {
    char t[140];
    formatBlowerEvent(t, sizeof(t), c, gSettings.tuning);
    eventlog::log(kEvTypeBlowers, "%s", t);
  }

  const uint64_t up = uptimeMs();
  const bool trusted = gClockSet && gClock.trusted();
  const int64_t utc = gClockSet ? (int64_t)time(nullptr) : 0;
  // The cap record (never placed by the clock, 4.6c): restored on the first pass.
  if (!gIon.restored()) gIon.restore(gIonBlob, gIonBlobLen);
  IonInputs ii;
  ii.upMs = up;
  ii.clockTrusted = trusted;
  ii.utcS = utc;
  ii.block = updating ? IonBlock::Updating : fault ? IonBlock::Fault : gCtl.hold().active() ? IonBlock::BootHold : IonBlock::None;
  ii.enabled = gSettings.user.ionEnable;
  ii.runsPerDay = gSettings.user.ionRunsPerDay;
  ii.minutesPerRun = gSettings.user.ionMinutesPerRun;
  gIon.update(ii);
  if (gIon.saveWanted()) {
    const size_t n = gIon.saveBlob(gIonBlob, sizeof(gIonBlob), up);
    gIonBlobLen = n;
    // 4.6c: if it can't be saved, no new run starts until a save works again (retried every minute)
    gIon.saveResult(ionstore::save(gIonBlob, n), up);
  }
  outputs::setIon(gIon.on() && !updating);   // after the save: a run switches on only once its record is saved
  for (IonSchedule::Change c; gIon.take(&c);) {
    char t[140];
    formatIonEvent(t, sizeof(t), c);
    eventlog::log(kEvTypeIon, "%s", t);
  }
}
#endif

void loop() {
  const uint32_t t0 = micros();
  const uint32_t now = millis();
  watchdog::feed();
  // Rev Q's hardware watchdog (3A.11): an input until now; from the first pass its level
  // changes every pass, here in the loop. Rev N: nothing.
  static bool firstPass = true;
  if (firstPass) { hwdog::start(now); firstPass = false; }
#if defined(WINEADOR_BOARD_REVQ)
  else if (!gRollbackStop) hwdog::kick(now);   // (the rollback test image stops it on purpose)
#else
  else hwdog::kick(now);
#endif
  supplysense::update(now);               // Rev Q: one 12V reading every 100 ms
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
  in.updating = otaUpdating() || kRollbackTest;   // 5.2: every load off (the rollback test image: always)
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
  const uint8_t fanWant = otaUpdating() ? gSettings.tuning.fanIdlePct : co.fanPct;   // 5.2: idle while updating
  if (fanWant != gFan.target()) gFan.command(fanWant, now);
  fan::setDuty(gFan.output(now));
  if (fan::update(now, gSettings.safety.fanTachWindowMs)) {
    // 5.2: no fan-stall check while updating (the flash pauses can lose tach pulses)
    if (!otaUpdating() && gFanMon.window(now, gFan.target() >= gSettings.safety.fanMinDutyPct, fan::rpm())) {
      const uint8_t target = gFan.target();
      gFan.command(0, now);
      gFan.output(now);                   // counts as stopped (not driven), so the next command kicks
      gFan.command(target, now);
      fan::setDuty(gFan.output(now));
    }
    printFanEvents();
  }

  // 3. Sensors and RTC — one short step per pass
  // 5.2: no sensor sweeps while updating (a flash pause mid-read would time out); resumed after
  if (!otaUpdating() && i2cbus::idle() && elapsedMs(now, gLastSweepStartMs) >= gSettings.tuning.sensorReadMs) {
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
    gFirstSweepDone = true;
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
    if (gHeapBaseline == 0) gHeapBaseline = heap::freeNow();   // after all initialisation
  }
  // The 4.3 gate brought up to date with this pass's sweep (step 1 updated it before the sweep).
  // Everything after reads Fault from it - the blowers, the ion generator, the LCD's Fault icon and
  // so the dashboard - so they can never disagree, even for one pass: without this, on the pass a
  // sensor recovers its recovery hold would not have started yet.
  if (sweptThisPass) {
    const int cabOk = gHealth.healthyCabinet();
    gGate.update(gHealth.chamberHealthy() && cabOk >= gSettings.safety.minHealthyCabinet,
                 cabOk >= gSettings.safety.minHealthyCabinet, now);
  }
#if defined(WINEADOR_BOARD_REVQ)
  // 3b. The blowers and the ion generator (4.6b, 4.6c): after the sweep, so a new spread is judged at once.
  passBlowersIon(now, sweptThisPass);
#endif
  // Rev N: the radio on only for the NTP sync. The first window waits for the clock
  // (from the RTC, or 10 s without one), so its event isn't dated 1970.
  if (gClockSet || now >= 10000) timesync::update(now);
  sdlog::setPaused(board::kSdPauseWhileWifi && timesync::radioOn());   // Rev N: no card writes meanwhile
  {
    char note[120];
    if (timesync::takeNote(note, sizeof(note))) eventlog::log("ntp", "%s", note);
  }
  checkHeapFloor();
  logBoardEvents(now);
  {
    const int er = i2cbus::takeEoscResult();   // TEST BUILDS: 'rtc eosc on|off' done between sweeps
    if (er != 0) {
      const bool set = (i2cbus::rtcControl() & kDs3231Eosc) != 0;
      Serial.printf("  RTC: EOSC write %s - EOSC now %s (control 0x%02X)\n", er > 0 ? "OK" : "FAILED", set ? "SET" : "clear",
                    (unsigned)i2cbus::rtcControl());
      eventlog::log("rtc", "TEST: EOSC %s (%s)", set ? "set" : "cleared", er > 0 ? "written" : "write FAILED");
    }
  }
  if (afterNtp(now)) printNow = true;
  startLogging();                         // the 10 s fallback if the clock never came
  watchSd(now);
  {
    int idx;
    uint32_t more;
    while (gBusLimit.takeSuppressed(now, &idx, &more))
      eventlog::log("crc", "ch%d: %lu more CRC failures / timeouts in the last hour (only the first 10 logged)",
                    idx, (unsigned long)more);
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
#if defined(WINEADOR_BOARD_REVQ)
  view.blowers = &gBlowers;
#endif
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
  // 7. The LCD (Rev Q): one small step; never waits (3A.11).
  updateScreen(now);
#if defined(WINEADOR_BOARD_REVQ)
  // 8. OTA (5.2), every pass from boot whether or not the dashboard runs: "started properly", its
  //    4 min 30 s deadline, the rollback test image, the writer's watch and the restart. Then the web
  //    dashboard: the mailbox and the state copy (never waits, 5.2, 5.5). Then the pass is complete:
  //    the OTA writer may take its next piece.
  otaPass(now);
  serveWeb(now);
  ota::loopPass();
#endif
  if (printNow) printStatus();
  const uint32_t passUs = micros() - t0;
  gLoopMax.add(passUs);
  gLoopMaxLog.add(passUs);
}
