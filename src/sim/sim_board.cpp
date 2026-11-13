#include "sim_board.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "wineador/event_text.h"
#include "wineador/sensor_table.h"
#include "wineador/uk_time.h"
#include "wineador/guards.h"
#include "wineador/build_info.h"

using namespace wineador;

namespace sim {

static const char* const kStateKeys[] = {"normal", "cooling", "heat-humid", "drying", "fault-sensors",
                                         "fan-stall", "sd-fail", "clock-bad", "just-booted", "updating"};
static const char* const kStateLabels[] = {
  "Normal: the model and the real control logic",
  "Cooling (shown)",
  "Heating and humidifying at once (shown)",
  "Drying: an RH strip (shown)",
  "Fault: ch2 and ch3 unplugged (real)",
  "Fault: the plenum fan unplugged (real)",
  "SD card out (shown)",
  "Clock not trusted (shown)",
  "Just booted: boot hold, history refilling (real)",
  "Updating, from 40 % (shown)",
};
const char* stateKey(State s) { return kStateKeys[(int)s]; }
const char* stateLabel(State s) { return kStateLabels[(int)s]; }
bool stateFromKey(const char* k, State* out) {
  for (int i = 0; i < (int)State::Count; i++)
    if (strcmp(k, kStateKeys[i]) == 0) { *out = (State)i; return true; }
  return false;
}

static void iso(char* out, size_t n, int64_t t) {
  int y, m, d;
  civilFromDays(t / 86400, &y, &m, &d);
  const int64_t s = t % 86400;
  snprintf(out, n, "%04d-%02d-%02dT%02d:%02d:%02dZ", y, m, d, (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60));
}

BuildFacts Board::build() const {
  BuildFacts b;
  b.board = "rev-q";
  b.profile = Profile::Real;
  b.testBuild = false;
  b.version = version_.c_str();
  return b;
}

void Board::bootComponents() {
  const uint32_t now = nowMs();
  bootMs_ = ms_;
  ctl_.begin(s_, now);
  health_.begin(s_);
  health_.setUser(s_.user);
  gate_.begin(s_.safety.sensorRecoveryHoldMs);
  fanMon_.begin(s_.safety.fanStallRetryMs);
  tc_.begin(s_, now);
  hc_.begin(s_, now);
  tc_.setUser(s_.user);
  hc_.setUser(s_.user);
  ctl_.setUser(s_.user);
  fan_ = FanController(s_.safety.fanMinDutyPct, s_.safety.fanKickstartMs);
  fan_.command(s_.tuning.fanIdlePct, now);
  fanOut_ = fan_.output(now);
  live_ = LoadOutputs{};
  for (int i = 0; i < kNumSensors; i++) health_.serialResult(i, BusResult::Ok, kSensors[i].serial);
  ui_.begin(now, 20);
  char d[160];
  snprintf(d, sizeof(d), "%s POWERON; firmware 2.0.0; board Rev Q%sREAL; user settings from NVS; clock set from the RTC",
           kEvBootLine, kEvBootProfile);
  event(kEvTypeBoot, d);
  sweep();
}

void Board::begin(int64_t nowUtcS) {
  s_ = settingsFor(Profile::Real);
  rawStore_.resize(400);
  dayStore_.resize(1440);
  hourlyStore_.resize(744);
  recentStore_.resize(60);
  History::Storage st;
  st.raw = rawStore_.data(); st.rawCap = 400;
  st.day = dayStore_.data(); st.dayCap = 1440;
  st.hourly = hourlyStore_.data(); st.hourlyCap = 744;
  hist_.begin(st);
  recent_.begin(recentStore_.data(), 60);
  ModelRates r;
  r.roomC = 22.0f;
  r.coolVpKpaPerMin = 0.004f;   // the compressor takes water out, so the humidifier has work to do
  model_.begin(r, 20.2f, 62.0f, 22.0f, 55.0f);
  // each sensor a little different, as in a real cabinet: warmer at the top, cooler at the bottom
  const float dc[kNumSensors] = {0.35f, -0.30f, 0.05f, 0.15f, 0.0f, 0.0f};
  const float dr[kNumSensors] = {-0.6f, 0.9f, 0.2f, -0.2f, 0.0f, -8.0f};
  for (int i = 0; i < kNumSensors; i++) model_.setOffset(i, dc[i], dr[i]);
  // a month before now, then run forward in 5 s steps
  const int64_t days = 31;
  startUtc_ = nowUtcS - days * 86400;
  ms_ = 0;
  bootComponents();
  const uint64_t endMs = (uint64_t)days * 86400 * 1000;
  while (ms_ < endMs) step(5000, false);
  weekReadyAtMs_ = 0;
  printf("sim: a month of history made (%d readings points, %d day points, %d hours); now live\n",
         hist_.count(History::Ring::Raw), hist_.count(History::Ring::Day), hist_.count(History::Ring::Hourly));
}

void Board::tick(int64_t wallUtcS) {
  while (utcNow() < wallUtcS) step(1000, true);
}

void Board::sweep() {
  for (int i = 0; i < kNumSensors; i++)
    if (health_.wantsSerial(i)) health_.serialResult(i, plugged_[i] ? BusResult::Ok : BusResult::NoAck, kSensors[i].serial);
  Readings r;
  for (int i = 0; i < kNumSensors; i++) r.s[i].bus = plugged_[i] ? BusResult::Ok : BusResult::NoAck;
  applyScripted(r, model_);
  health_.update(r, nowMs());
  lastSweepMs_ = ms_;
  // hourly figures, as the firmware adds them after each sweep
  bool healthy[kNumSensors];
  float t[kNumSensors], rh[kNumSensors];
  for (int i = 0; i < kNumSensors; i++) { healthy[i] = health_.healthy(i); t[i] = health_.tempC(i); rh[i] = health_.rhPct(i); }
  hourly_.addSweep(healthy, t, rh, health_.summary(), fanOut_, (fanPlugged_ && fanSpinning_ && fanOut_ >= 20) ? (uint32_t)(700 + 18 * fanOut_) : 0);
}

void Board::event(const char* type, const char* detail) {
  const int64_t t = utcNow();
  char ts[24];
  iso(ts, sizeof(ts), t);
  char row[600];
  const int k = formatEventRow(row, sizeof(row), ts, true, ms_ - bootMs_, type, detail);
  int y, m, d;
  civilFromDays(t / 86400, &y, &m, &d);
  char path[48];
  logPath(path, sizeof(path), LogKind::Events, y, m);
  std::string& f = files_[path];
  if (f.empty()) {
    static char head[2000];
    LogHeaderInfo hi;
    hi.firmware = "2.0.0";
    hi.board = "rev-q (channel n = mux n)";
    formatEventsHeader(head, sizeof(head), hi);
    f += head;
  }
  EventRow e;
  parseEventRow(row, (size_t)k, &e);
  e.offset = f.size();
  f.append(row, (size_t)k);
  recent_.add(e);
  week_.add(t, weekKindOf(type, detail));
}

void Board::logRow() {
  const int64_t t = utcNow();
  char ts[24];
  iso(ts, sizeof(ts), t);
  ReadingsRow r;
  r.timestamp = ts;
  r.timeTrusted = true;
  for (int i = 0; i < kNumSensors; i++) {
    r.ch[i].read = plugged_[i];
    r.ch[i].tempC = health_.tempC(i);
    r.ch[i].rhPct = health_.rhPct(i);
    r.ch[i].rhRawPct = rawForCorrectedRh(i, health_.rhPct(i));
    r.ch[i].flag = health_.healthy(i) ? "ok" : exclusionName(health_.reason(i));
  }
  r.cab = health_.summary();
  r.fanPct = fanOut_;
  r.fanRpm = (fanPlugged_ && fanSpinning_ && fanOut_ >= 20) ? (uint32_t)(700 + 18 * fanOut_) : 0;
  r.cool = live_.cool;
  r.heaterDutyPct = live_.heat ? 100 : 0;
  r.humid = live_.humid;
  r.relayHeating = live_.relayHeating;
  r.heapFree = 182000;
  r.heapMin = 151000;
  r.hasV12 = true; r.v12MeanV = 12.08f; r.v12Min1sV = 12.01f;
  r.hasPsram = true; r.psramMin = 812000;
  static char line[1200];
  const int k = formatReadingsRow(line, sizeof(line), r, false);
  hist_.addReadingsLine(line, (size_t)k);
  int y, m, d;
  civilFromDays(t / 86400, &y, &m, &d);
  char path[48];
  logPath(path, sizeof(path), LogKind::Readings, y, m);
  std::string& f = files_[path];
  if (f.empty()) {
    static char head[3000];
    LogHeaderInfo hi;
    hi.firmware = "2.0.0";
    hi.board = "rev-q (channel n = mux n)";
    formatReadingsHeader(head, sizeof(head), hi);
    f += head;
  }
  f.append(line, (size_t)k);
}

void Board::hourRoll() {
  const int64_t t = utcNow();
  const int64_t key = t / 3600;
  if (key == hourKey_) return;
  if (hourKey_ >= 0) {
    hourly_.noteHeapMin(151000);
    hourly_.noteSupply(12.08f, 12.0f);
    hourly_.notePsramMin(812000);
    static char line[2000];
    const int k = hourly_.formatRow(line, sizeof(line));
    hist_.addHourlyLine(line, (size_t)k);
    char path[48];
    logPath(path, sizeof(path), LogKind::Hourly, 0, 0);
    std::string& f = files_[path];
    if (f.empty()) {
      static char head[3000];
      LogHeaderInfo hi;
      hi.firmware = "2.0.0";
      hi.board = "rev-q (channel n = mux n)";
      formatHourlyHeader(head, sizeof(head), hi);
      f += head;
    }
    f.append(line, (size_t)k);
  }
  hourKey_ = key;
  char stamp[24];
  iso(stamp, sizeof(stamp), key * 3600);
  hourly_ = HourlyStats{};
  hourly_.begin((uint32_t)key, stamp, true);
}

void Board::drainEvents() {
  char d[300];
  for (ControlEvent e; tc_.takeEvent(e);) {
    formatControlEvent(d, sizeof(d), e, s_);
    const char* type = kEvTypeIneffective;
    switch (e.type) {
      case ControlEvent::Type::StripStart: case ControlEvent::Type::StripStop: type = kEvTypeStrip; break;
      case ControlEvent::Type::HarvestStart: case ControlEvent::Type::HarvestEnd: type = kEvTypeHarvest; break;
      case ControlEvent::Type::Crossover: type = kEvTypeCrossover; break;
      default: break;
    }
    event(type, d);
  }
  for (HumidEvent e; hc_.takeEvent(e);) {
    formatHumidEvent(d, sizeof(d), e, s_);
    event(e.type == HumidEvent::Type::CallStart || e.type == HumidEvent::Type::CallEnd ? kEvTypeHumidify
                                                                                       : kEvTypeIneffective, d);
  }
  for (SensorHealth::Event e; health_.takeEvent(e);) {
    snprintf(d, sizeof(d), "ch%d %s %s%s%s", e.index, kSensors[e.index].position, e.excluded ? "excluded: " : "recovered",
             e.excluded ? exclusionName(e.reason) : "", "");
    event(kEvTypeSensor, d);
  }
  guardTrack_.update(ctl_.decisions());
  for (GuardBlockTracker::Change ch; guardTrack_.take(ch);) {
    GuardBlockTracker::format(d, sizeof(d), ch);
    event(kEvTypeGuard, d);
  }
}

void Board::step(uint32_t dtMs, bool live) {
  // the room's daily swing (made up): warmest mid-afternoon
  {
    const int64_t t = utcNow();
    const double h = (double)(t % 86400) / 3600.0;
    model_.rates().roomC = (float)(21.0 + 3.6 * sin((h - 9.0) / 24.0 * 2 * M_PI));
  }
  ms_ += dtMs;
  const uint32_t now = nowMs();
  hourRoll();
  const int cabOk = health_.healthyCabinet();
  gate_.update(health_.chamberHealthy() && cabOk >= s_.safety.minHealthyCabinet, cabOk >= s_.safety.minHealthyCabinet, now);
  const bool fanOk = fanPlugged_ && fanSpinning_ && fanOut_ >= s_.safety.fanMinDutyPct;
  PassInputs in;
  in.nowMs = now;
  in.fanOk = fanOk;
  in.settingsOk = true;
  in.readingsFresh = ms_ - lastSweepMs_ <= 2ull * s_.tuning.sensorReadMs;
  in.cab = health_.summary();
  in.sensorHoldTemp = gate_.tempHolding();
  in.sensorHoldHumid = gate_.humidHolding();
  in.live = live_;
  ControlInputs ci;
  ci.nowMs = now;
  ci.cab = in.cab;
  ci.readingsFresh = in.readingsFresh;
  ci.live = live_;
  const ControlOutput co = tc_.update(ci);
  HumidInputs hi;
  hi.nowMs = now;
  hi.cab = in.cab;
  hi.readingsFresh = in.readingsFresh;
  hi.live = live_;
  hi.stripping = co.strippingActive;
  const HumidOutput ho = hc_.update(hi);
  in.strippingActive = co.strippingActive;
  in.heaterIneffectiveHold = co.heaterIneffectiveHold;
  in.humidIneffectiveHold = ho.ineffectiveHold;
  Demand dem = co.demand;
  dem.humid = ho.demand;
  if (updatePct_ >= 0) dem = Demand{};    // 5.2: loads off during an update
  // 5.2: during an update the plenum fan runs at its idle setting
  const uint8_t wantFan = updatePct_ >= 0 ? s_.tuning.fanIdlePct : co.fanPct;
  if (wantFan != fan_.target()) fan_.command(wantFan, now);
  fanOut_ = fan_.output(now);
  if (fanOut_ == 0) fanSpinning_ = false;
  else if (fanOut_ == 100 && fanPlugged_) fanSpinning_ = true;
  const LoadOutputs was = live_;
  live_ = ctl_.pass(in, dem);
  if (updatePct_ >= 0) {   // 5.2: during an update every load is off at once; the plenum fan at its idle setting
    LoadOutputs off;
    off.relayHeating = false;
    live_ = off;
  }
  hourly_.addOnTime(dtMs, live_);
  char why[80], d[160];
  for (int L = 0; L < 3; L++) {
    const bool a = L == 0 ? was.cool : L == 1 ? was.heat : was.humid;
    const bool b = L == 0 ? live_.cool : L == 1 ? live_.heat : live_.humid;
    if (a == b) continue;
    const LoadDecision& dec = L == 0 ? ctl_.decisions().cool : L == 1 ? ctl_.decisions().heat : ctl_.decisions().humid;
    if (b) snprintf(why, sizeof(why), "%s", L == 0 ? (co.strippingActive ? "RH strip" : "temperature call")
                                                   : L == 1 ? "temperature call" : "humidify call");
    else snprintf(why, sizeof(why), "%s", dec.block != GuardBlock::None ? guardBlockName(dec.block) : "demand ended");
    formatLoadEvent(d, sizeof(d), L, b, why);
    event(loadEventType(L), d);
    if (L == 0) {
      if (b) compressorOnSinceUtc_ = utcNow();
      else { compressorStoppedUtc_ = utcNow(); compressorStopped_ = true; }
    }
  }
  if (was.relayHeating != live_.relayHeating) event(kEvTypeInterlock, live_.relayHeating ? kEvK1Heating : kEvK1Cooling);
  if (ms_ - lastModelMs_ >= 1000) { model_.step((uint32_t)(ms_ - lastModelMs_), live_); lastModelMs_ = ms_; }
  if (ms_ - lastWindowMs_ >= 5000) {
    lastWindowMs_ = ms_;
    if (fanMon_.window(now, fan_.target() >= 20, (fanPlugged_ && fanSpinning_) ? 420 : 0)) {
      const uint8_t target = fan_.target();
      fan_.command(0, now);
      fan_.output(now);
      fan_.command(target, now);
      fanOut_ = fan_.output(now);
      if (fanPlugged_) fanSpinning_ = true;
      event(kEvTypeFan, "fan tach stall: kickstart");
    }
  }
  if (ms_ - lastSweepMs_ >= s_.tuning.sensorReadMs) sweep();
  const uint64_t logEvery = (live_.cool || live_.heat) ? s_.tuning.logActiveMs : s_.tuning.logSteadyMs;
  if (ms_ - lastLogMs_ >= logEvery) { lastLogMs_ = ms_; logRow(); }
  drainEvents();
  if (live) {
    // OTA: the loop's own watch, the simulated restart, "started properly" 60 s later
    if (ota_.watch(now, ota_.bytes())) {
      char e[200];
      formatOtaLoopGaveUp(e, sizeof(e), ota_.version(), ota_.bytes(), ota_.total());
      event(kEvTypeOta, e);
      otaRec_ = otaRecordEnded(otaRec_, OtaOutcome::Cancelled, utcNow());
      updatePct_ = -1;
    }
    if (!sceneFrozen_ && ota_.restartDue(now)) {
      version_ = ota_.version();
      slot_ = 1 - slot_;
      bootMs_ = ms_;                    // a restart: the uptime starts again (the page sees it)
      ota_ = OtaLoop{};
      updatePct_ = -1;
      pending_ = true;
      pendingSinceMs_ = ms_;
      char e[200];
      snprintf(e, sizeof(e), "%s: %s, waiting to start properly (review build: a simulated restart)", kEvOtaWaiting,
               version_.c_str());
      event(kEvTypeOta, e);
    }
    if (!sceneFrozen_ && pending_ && ms_ - pendingSinceMs_ >= kStartedProperlyMs) {
      pending_ = false;
      char e[220];
      formatOtaGood(e, sizeof(e), version_.c_str());
      event(kEvTypeOta, e);
      otaRec_ = otaRecordGood(otaRec_, utcNow());
    }
    if (updatePct_ >= 0 && !ota_.updating()) {   // the shown "Updating" state
      updatePct_ += 2;
      if (updatePct_ > 100) {
        updatePct_ = -1;
        lastUpdate_ = UpdateResult::Installed;
        lastUpdateDetail_ = "2.0.0";
        lastUpdateUtc_ = utcNow();
        event(kEvTypeBoot, "OTA update done (review build: nothing was written)");
        if (state_ == State::Updating) state_ = State::Normal;
      }
    }
    // the LCD's own logic, as the board runs it each pass
    UiInputs u;
    u.nowMs = now;
    u.uptimeMs = ms_ - bootMs_;
    const CabinetSummary c = health_.summary();
    u.cabinetOk = c.cabinetOk;
    u.fresh = in.readingsFresh;
    u.meanC = c.meanC;
    u.meanRhPct = c.meanRhPct;
    u.targetC = s_.user.targetTempC;
    u.rhSetPct = s_.user.rhSetpointPct;
    u.cool = live_.cool;
    u.heat = live_.heat;
    u.humid = live_.humid;
    u.stripping = co.strippingActive;
    u.bootHold = ctl_.hold().active();
    u.bootHoldLeftMs = ctl_.hold().remainingMs();
    u.updating = updatePct_ >= 0;
    u.updatePct = (uint8_t)(updatePct_ < 0 ? 0 : updatePct_);
    u.alerts.sensorsKnown = true;
    u.alerts.sensorFault = sensorFaultFrom(health_.chamberHealthy(), c.cabinetOk, s_.safety.minHealthyCabinet,
                                           gate_.tempHolding());
    u.alerts.fanStalled = !fanOk;
    for (int i = 0; i < kNumSensors; i++) {
      if (!health_.healthy(i)) u.alerts.excludedMask |= (uint8_t)(1u << i);
      if (health_.reason(i) == Exclusion::Manual) u.alerts.manualMask |= (uint8_t)(1u << i);
    }
    u.alerts.sdFailed = state_ == State::SdFail;
    u.alerts.clockBad = state_ == State::ClockBad;
    u.clockOk = state_ != State::ClockBad;
    u.utcNowS = utcNow();
    u.wifi = true;
    switch (state_) {   // the "shown" states: what the screen would show
      case State::Cooling: u.cool = true; u.heat = u.humid = false; u.stripping = false; u.bootHold = false; break;
      case State::HeatHumid: u.cool = false; u.heat = u.humid = true; u.bootHold = false; break;
      case State::Drying: u.cool = true; u.stripping = true; u.heat = false; u.bootHold = false; break;
      default: break;
    }
    ui_.update(u);
  }
}

void Board::setState(State st, int64_t wallUtcS) {
  // undo the real ones
  plugged_[2] = plugged_[3] = true;
  fanPlugged_ = true;
  state_ = st;
  ui_.window().close();   // review build only: a new state starts with the window closed (the LCD has no cancel)
  switch (st) {
    // the shown states also move the cabinet to match, so the readings and the reasons agree
    case State::Cooling: model_.setCabinetC(20.9f); break;
    case State::HeatHumid: model_.setCabinetC(18.2f); model_.setCabinetRh(58.8f); break;
    case State::Drying: model_.setCabinetRh(68.4f); break;
    case State::FaultSensors:
      plugged_[2] = plugged_[3] = false;
      for (int i = 0; i < 70; i++) step(1000, true);    // long enough for 3 missed reads each
      break;
    case State::FanStall:
      fanPlugged_ = false;
      fanSpinning_ = false;
      for (int i = 0; i < 12; i++) step(1000, true);
      break;
    case State::JustBooted: {
      rawStore_.assign(400, HistPoint{});
      History::Storage s;
      s.raw = rawStore_.data(); s.rawCap = 400;
      s.day = dayStore_.data(); s.dayCap = 1440;
      s.hourly = hourlyStore_.data(); s.hourlyCap = 744;
      hist_.begin(s);   // after a boot the graphs start empty, then refill from the card
      recent_.begin(recentStore_.data(), 60);
      bootComponents();
      weekReadyAtMs_ = ms_ + 20000;
      for (int i = 0; i < 20; i++) step(1000, true);
      break;
    }
    case State::Updating:
      updatePct_ = 40;
      lastUpdate_ = UpdateResult::None;
      break;
    default:
      break;
  }
  (void)wallUtcS;
  if (st != State::Updating && updatePct_ >= 0 && st != State::JustBooted) updatePct_ = -1;
  sweep();   // the sensors read the moved cabinet at once
  step(0, true);
}

void Board::pressAllowUpdate() {
  ui_.window().open(nowMs());
  event(kEvTypeLcd, "LCD Allow update pressed (review build)");
}

void Board::applyUser(const UserSettings& u, const char* from) {
  char d[300];
  const int k = snprintf(d, sizeof(d), "%s (%s): ", kEvSettingsChanged, from);
  describeUserChange(d + k, sizeof(d) - (size_t)k, s_.user, u);
  s_.user = u;
  ctl_.setUser(u);
  tc_.setUser(u);
  hc_.setUser(u);
  health_.setUser(u);
  event(kEvTypeSettings, d);
}

void Board::doorTest(const char* note) {
  char d[200];
  if (note && note[0]) snprintf(d, sizeof(d), "%s: %s", kEvDoorTest, note);
  else snprintf(d, sizeof(d), "%s", kEvDoorTest);
  event(kEvTypeDoor, d);
}

uint32_t Board::checkingLeftS() const {
  if (!pending_) return 0;
  const uint64_t ran = ms_ - pendingSinceMs_;
  return ran >= kStartedProperlyMs ? 1 : (uint32_t)((kStartedProperlyMs - ran + 999) / 1000);
}

MailAnswer Board::otaMail(const MailRequest& r) {
  MailAnswer a;
  a.id = r.id;
  a.user = s_.user;
  char e[220];
  if (r.kind == MailKind::UpdateBegin) {
    sceneFrozen_ = false;
    a.result = ota_.begin(nowMs(), r.length, r.version, compressorRunning(), ui_.window(), checkingLeftS());
    if (a.result.ok()) {
      updatePct_ = 0;
      lastUpdate_ = UpdateResult::None;
      formatOtaStarted(e, sizeof(e), r.version, r.length, version_.c_str());
      event(kEvTypeOta, e);
      otaRec_ = otaRecordStarted(r.version, version_.c_str(), utcNow(), (uint8_t)(1 - slot_));
    }
  } else if (r.kind == MailKind::UpdateEnd && ota_.updating()) {
    const OtaOutcome o = (OtaOutcome)r.outcome;
    ota_.end(nowMs(), o);
    formatOtaEnded(e, sizeof(e), o, r.version);
    event(kEvTypeOta, e);
    otaRec_ = otaRecordEnded(otaRec_, o, utcNow());
    if (o != OtaOutcome::Installed) updatePct_ = -1;
    else updatePct_ = 100;
  }
  return a;
}

void Board::otaProgress(uint32_t bytes) {
  ota_.watch(nowMs(), bytes);
  updatePct_ = ota_.pct();
}

void Board::otaRefused(const ApiResult& r) {
  if (!otaLimit_.allow(0, nowMs())) return;
  char e[220];
  formatOtaRefused(e, sizeof(e), r);
  event(kEvTypeOta, e);
}

const char* const Board::kOtaScenes[] = {"closed", "open", "compressor", "writing", "restarting", "pending",
                                         "good", "rolled-back", "abandoned", "corrupt", "interrupted"};
const char* const Board::kOtaSceneLabels[] = {
  "Window closed", "Window open", "Window open, compressor running", "Writing, 43 %", "Written: restarting",
  "New image being checked (42 s left)", "Marked good", "Rolled back", "Abandoned (stalled)", "Corrupt image",
  "Interrupted by a power cut"};
const int Board::kOtaSceneCount = (int)(sizeof(kOtaScenes) / sizeof(kOtaScenes[0]));

bool Board::otaScene(const char* key) {
  int i = 0;
  while (i < kOtaSceneCount && strcmp(kOtaScenes[i], key) != 0) i++;
  if (i == kOtaSceneCount) return false;
  sceneFrozen_ = strcmp(key, "closed") != 0 && strcmp(key, "open") != 0;   // held still for the pictures
  ota_ = OtaLoop{};
  pending_ = false;
  updatePct_ = -1;
  version_ = "2.0.0";
  ui_.window().close();
  otaRec_ = OtaRecord{};
  const int64_t t = utcNow() - 1800;
  auto rec = [&](UpdateResult res, const char* detail) {
    otaRec_.result = (uint8_t)res;
    otaRec_.step = OtaRecord::Done;
    otaRec_.utcS = t;
    snprintf(otaRec_.detail, sizeof(otaRec_.detail), "%s", detail);
  };
  const std::string k = key;
  if (k == "compressor") setState(State::Cooling, utcNow());   // (closes the window: opened again below)
  else if (state_ == State::Cooling) setState(State::Normal, utcNow());
  if (k == "open" || k == "compressor") ui_.window().open(nowMs());
  if (k == "writing" || k == "restarting") {
    ui_.window().open(nowMs());
    ota_.begin(nowMs(), 1385360, "2.0.1", false, ui_.window(), 0);
    ota_.watch(nowMs(), k == "writing" ? 1385360u * 43u / 100u : 1385360u);
    if (k == "restarting") ota_.end(nowMs(), OtaOutcome::Installed);
    updatePct_ = ota_.pct();
  }
  if (k == "pending") {
    version_ = "2.0.1";
    pending_ = true;
    pendingSinceMs_ = ms_ - 18000;
    rec(UpdateResult::Installed, "2.0.1, replacing 2.0.0: restarting");
  }
  if (k == "good") { version_ = "2.0.1"; rec(UpdateResult::Installed, "2.0.1, replacing 2.0.0: started properly"); }
  if (k == "rolled-back") rec(UpdateResult::RolledBack, "from 2.0.1 to 2.0.0: it didn't start properly");
  if (k == "abandoned") rec(UpdateResult::Abandoned, "2.0.1: no data for 60 s; nothing changed");
  if (k == "corrupt") rec(UpdateResult::Failed, "2.0.1: the image's own check failed (corrupt); nothing changed");
  if (k == "interrupted") rec(UpdateResult::Failed, "2.0.1: interrupted during the write (a reset or power cut); still 2.0.0");
  return true;
}

UpdateState Board::updateState() {
  UpdateState u;
  u.windowOpen = windowOpen();
  u.windowLeftS = windowLeftS();
  u.compressorRunning = compressorRunning();
  u.updating = updatePct_ >= 0 || ota_.updating();
  u.phase = ota_.phase() == OtaLoop::Phase::Restarting ? "restarting" : u.updating ? "writing" : "idle";
  u.pct = ota_.updating() ? ota_.pct() : (uint8_t)(updatePct_ < 0 ? 0 : updatePct_);
  u.slotBytes = 3342336;
  u.running = build();
  u.pendingVerify = pending_;
  u.verifyLeftS = checkingLeftS();
  if (otaRec_.result != (uint8_t)UpdateResult::None || otaRec_.step != OtaRecord::None) {
    u.last = (UpdateResult)otaRec_.result;
    if (pending_) snprintf(u.lastDetail, sizeof(u.lastDetail), "%s: waiting to start properly", version_.c_str());
    else snprintf(u.lastDetail, sizeof(u.lastDetail), "%s", otaRec_.detail);
    u.lastUtcS = otaRec_.utcS;
  } else {
    u.last = lastUpdate_;
    snprintf(u.lastDetail, sizeof(u.lastDetail), "%s", lastUpdateDetail_.c_str());
    u.lastUtcS = lastUpdateUtc_;
  }
  return u;
}

std::vector<LogFileEntry> Board::files() const {
  std::vector<LogFileEntry> v;
  for (const auto& kv : files_) {
    LogFileEntry e;
    snprintf(e.path, sizeof(e.path), "%s", kv.first.c_str());
    e.bytes = kv.second.size();
    v.push_back(e);
  }
  sortLogFilesNewestFirst(v.data(), (int)v.size());
  return v;
}

const std::string* Board::file(const char* path) const {
  const auto it = files_.find(path);
  return it == files_.end() ? nullptr : &it->second;
}

void Board::fillStatus(StatusSnapshot* s) const {
  StatusSnapshot x;
  x.build = build();
  x.uptimeS = (ms_ - bootMs_) / 1000;
  const UiView& v = ui_.view();
  copyFromLcd(&x, ui_, ms_ - bootMs_, state_ != State::ClockBad, utcNow());
  (void)v;
  const CabinetSummary c = health_.summary();
  x.bootHold = ctl_.hold().active();
  x.bootHoldLeftS = ctl_.hold().remainingMs() / 1000;
  x.readingsOk = c.cabinetOk >= 3 && ms_ - lastSweepMs_ <= 2ull * s_.tuning.sensorReadMs;
  x.cabinetOk = c.cabinetOk;
  x.meanC = c.meanC; x.minC = c.minC; x.maxC = c.maxC; x.spreadC = c.spreadC;
  x.meanRhPct = c.meanRhPct; x.controlC = c.controlC;
  x.readingsAgeS = (uint32_t)((ms_ - lastSweepMs_) / 1000);
  x.targetC = s_.user.targetTempC;
  x.rhSetPct = s_.user.rhSetpointPct;
  x.coolingHysteresisC = s_.tuning.coolingHysteresisC;
  x.heatingDeadbandC = s_.tuning.heatingDeadbandC;
  x.cool = live_.cool; x.heat = live_.heat; x.humid = live_.humid; x.relayHeating = live_.relayHeating;
  x.stripping = tc_.stripping();
  switch (state_) {
    case State::Cooling: x.cool = true; x.heat = x.humid = false; x.stripping = false; x.bootHold = false; break;
    case State::HeatHumid: x.cool = false; x.heat = x.humid = true; x.relayHeating = true; x.bootHold = false; break;
    case State::Drying: x.cool = true; x.stripping = true; x.heat = false; x.bootHold = false; break;
    default: break;
  }
  x.heaterDutyPct = x.heat ? 100 : 0;
  x.fanPct = fanOut_;
  x.fanRpm = (fanPlugged_ && fanSpinning_ && fanOut_ >= 20) ? (uint32_t)(700 + 18 * fanOut_) : 0;
  x.blowerPct = 100;
  x.ionOn = false;
  x.compressorOnForS = x.cool ? (uint32_t)(utcNow() - (live_.cool ? compressorOnSinceUtc_ : utcNow() - 420)) : 0;
  x.compressorStoppedSinceBoot = compressorStopped_;
  x.compressorStoppedAgoS = (uint32_t)(utcNow() - compressorStoppedUtc_);
  const GuardDecisions& g = ctl_.decisions();
  const LoadDecision* ld[3] = {&g.cool, &g.heat, &g.humid};
  for (int L = 0; L < 3; L++) {
    if (!ld[L]->demanded || ld[L]->run || ld[L]->block == GuardBlock::None) continue;
    GuardHold& h = x.guards[x.nGuards++];
    h.load = (uint8_t)L;
    h.reason = guardBlockName(ld[L]->block);
    h.leftS = ld[L]->waitMs ? (int32_t)(ld[L]->waitMs / 1000) : -1;
  }
  x.weekReady = ms_ >= weekReadyAtMs_;
  week_.counts(utcNow(), &x.crossovers, &x.strips, &x.humidRuns, &x.disturbances);
  x.disturbanceDetection = s_.tuning.disturbanceDetection;
  x.windowOpen = const_cast<Board*>(this)->windowOpen();
  x.windowLeftS = const_cast<Board*>(this)->windowLeftS();
  x.updating = updatePct_ >= 0;
  x.updatePct = (uint8_t)(updatePct_ < 0 ? 0 : updatePct_);
  x.sdOk = state_ != State::SdFail;
  x.sdFreeBytes = 59200000000ULL;
  x.sdTotalBytes = 59700000000ULL;
  // the Board section (made up, near what Rev Q should read)
  x.bootReason = "POWERON";
  x.hasV12 = true;
  x.v12NowV = 12.06f + 0.03f * (float)((utcNow() / 15) % 3);
  x.v12LowTodayV = 11.94f;
  x.heapFreeBytes = 182400 - (uint32_t)((utcNow() / 30) % 7) * 512;
  x.wifiRssi = -58;
  for (int i = 0; i < kNumSensors; i++) {
    SensorStatus& o = x.sensors[i];
    const Exclusion r = health_.reason(i);
    o.state = r == Exclusion::None ? SensorState::Healthy : r == Exclusion::Manual ? SensorState::Manual
            : r == Exclusion::NotYetRead ? SensorState::NotRead : SensorState::Excluded;
    if (r != Exclusion::None) snprintf(o.reason, sizeof(o.reason), "%s", exclusionName(r));
    o.read = plugged_[i];
    o.tempC = health_.tempC(i);
    o.rhPct = health_.rhPct(i);
    o.rhRawPct = rawForCorrectedRh(i, health_.rhPct(i));
    o.ageS = plugged_[i] ? (uint32_t)((ms_ - lastSweepMs_) / 1000) : 60;
  }
  *s = x;
}

}  // namespace sim
