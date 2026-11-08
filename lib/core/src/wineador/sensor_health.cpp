#include "wineador/sensor_health.h"
#include "wineador/sensor_table.h"
#include <math.h>

namespace wineador {

const char* exclusionName(Exclusion e) {
  switch (e) {
    case Exclusion::None:        return "ok";
    case Exclusion::NotYetRead:  return "not-read";
    case Exclusion::CrcFail:     return "crc";
    case Exclusion::Timeout:     return "timeout";
    case Exclusion::Absent:      return "absent";
    case Exclusion::Implausible: return "implausible";
    case Exclusion::Divergence:  return "divergence";
    case Exclusion::BusFault:    return "bus-fault";
    case Exclusion::WrongSerial: return "wrong-serial";
    case Exclusion::Manual:      return "manual";
  }
  return "?";
}

float correctedRh(int i, float raw) {
  const float v = kSensors[i].rhGain * raw + kSensors[i].rhOffset;
  return v < 0.0f ? 0.0f : (v > 100.0f ? 100.0f : v);
}

float rawForCorrectedRh(int i, float pct) {
  return (pct - kSensors[i].rhOffset) / kSensors[i].rhGain;
}

// ---------------------------------------------------------------- SensorHealth
void SensorHealth::begin(const Settings& s) {
  set_ = s;
  for (int i = 0; i < kNumSensors; i++) s_[i] = State{};
  qHead_ = qLen_ = 0;
}

void SensorHealth::push(const Event& e) {
  if (qLen_ == kQ) { qHead_ = (qHead_ + 1) % kQ; qLen_--; }
  q_[(qHead_ + qLen_) % kQ] = e;
  qLen_++;
}

bool SensorHealth::takeEvent(Event& e) {
  if (qLen_ == 0) return false;
  e = q_[qHead_];
  qHead_ = (qHead_ + 1) % kQ;
  qLen_--;
  return true;
}

void SensorHealth::exclude(int i, Exclusion why) {
  State& st = s_[i];
  st.goodRun = 0;
  st.diverging = false;
  st.serialChecked = false;        // any recovery re-reads the serial first (5.3)
  if (st.reason == why) return;
  // A failure while already excluded keeps the first reason; a wrong serial
  // always shows, and any reason replaces "not read yet".
  if (st.reason == Exclusion::None || st.reason == Exclusion::NotYetRead || why == Exclusion::WrongSerial) {
    st.reason = why;
    push(Event{i, true, why});
  }
}

void SensorHealth::serialResult(int i, BusResult bus, uint32_t serial) {
  State& st = s_[i];
  st.wantSerial = false;
  if (bus != BusResult::Ok) {
    st.serialChecked = false;
    st.wantSerial = true;                  // try again next sweep
    return;
  }
  if (serial == kSensors[i].serial) {
    st.serialChecked = true;
  } else {
    st.serialChecked = false;
    exclude(i, Exclusion::WrongSerial);
  }
}

float SensorHealth::medianHealthyCabinet(int skip, int& count) const {
  float v[kCabinetSensors];
  count = 0;
  for (int i = 0; i < kCabinetSensors; i++)
    if (i != skip && healthy(i)) v[count++] = s_[i].tempC;
  if (count == 0) return 0;
  for (int a = 1; a < count; a++)            // insertion sort, at most 4 values
    for (int b = a; b > 0 && v[b] < v[b - 1]; b--) { float t = v[b]; v[b] = v[b - 1]; v[b - 1] = t; }
  return (count % 2) ? v[count / 2] : 0.5f * (v[count / 2 - 1] + v[count / 2]);
}

void SensorHealth::update(const Readings& r, uint32_t now) {
  const SafetySettings& f = set_.safety;
  const uint8_t manual = set_.user.manualExcludeMask;

  // Pass 1: each sensor's own read.
  bool goodNow[kNumSensors] = {};
  float tNow[kNumSensors] = {}, rhNow[kNumSensors] = {};
  for (int i = 0; i < kNumSensors; i++) {
    State& st = s_[i];
    const SensorReading& rd = r.s[i];

    if (manual & (1u << i)) {
      if (st.reason != Exclusion::Manual) { st.reason = Exclusion::None; exclude(i, Exclusion::Manual); }
      continue;
    }
    if (st.reason == Exclusion::Manual) { st.reason = Exclusion::NotYetRead; st.wantSerial = true; }

    switch (rd.bus) {
      case BusResult::NotRead:
        continue;
      case BusResult::Ok: {
        st.crcRun = st.timeoutRun = 0;
        const bool plausible = rd.tempC >= f.plausibleMinC && rd.tempC <= f.plausibleMaxC &&
                               rd.rhRawPct >= f.plausibleMinRhPct && rd.rhRawPct <= f.plausibleMaxRhPct;
        if (!plausible) { exclude(i, Exclusion::Implausible); continue; }
        goodNow[i] = true;
        tNow[i] = rd.tempC;
        rhNow[i] = correctedRh(i, rd.rhRawPct);
        break;
      }
      case BusResult::CrcFail:
        st.timeoutRun = 0;
        if (++st.crcRun >= f.crcFailLimit) exclude(i, Exclusion::CrcFail);
        else st.goodRun = 0;
        continue;
      case BusResult::ShortRead:
      case BusResult::MuxFail:
        st.crcRun = 0;
        if (++st.timeoutRun >= f.timeoutLimit) exclude(i, Exclusion::Timeout);
        else st.goodRun = 0;
        continue;
      case BusResult::NoAck:
        st.crcRun = st.timeoutRun = 0;
        exclude(i, Exclusion::Absent);
        continue;
      case BusResult::BusStuck:
        st.crcRun = st.timeoutRun = 0;
        exclude(i, Exclusion::BusFault);
        continue;
    }

    // A good, plausible read.
    if (st.reason == Exclusion::None) {
      st.tempC = tNow[i];
      st.rhPct = rhNow[i];
    }
  }

  // Pass 2: sensors not yet healthy — boot, or recovering.
  for (int i = 0; i < kNumSensors; i++) {
    State& st = s_[i];
    if (!goodNow[i] || st.reason == Exclusion::None || st.reason == Exclusion::Manual) continue;

    if (st.reason == Exclusion::NotYetRead) {
      // Boot (or after a manual exclude): healthy at once with a checked serial.
      if (st.serialChecked) {
        st.reason = Exclusion::None;
        st.tempC = tNow[i];
        st.rhPct = rhNow[i];
        st.goodRun = 0;
      } else {
        st.wantSerial = true;
      }
      continue;
    }

    // Recovering from an exclusion: 10 good reads in a row...
    bool counts = true;
    if (st.reason == Exclusion::Divergence) {
      int n = 0;
      const float med = medianHealthyCabinet(i, n);
      counts = n >= 2 && fabsf(tNow[i] - med) <= f.divergenceC;   // ...within 5 C of the median
    }
    st.goodRun = counts ? (uint8_t)(st.goodRun < 255 ? st.goodRun + 1 : 255) : 0;
    if (st.goodRun < f.recoveryGoodReads) continue;

    // ...then the serial is re-read before the recovery is accepted.
    if (!st.serialChecked) { st.wantSerial = true; continue; }
    st.reason = Exclusion::None;
    st.goodRun = 0;
    st.diverging = false;
    st.tempC = tNow[i];
    st.rhPct = rhNow[i];
    push(Event{i, false, Exclusion::None});
  }

  // Pass 3: divergence, cabinet sensors only, only while >= 3 are healthy.
  int n = 0;
  const float med = medianHealthyCabinet(-1, n);
  for (int i = 0; i < kCabinetSensors; i++) {
    State& st = s_[i];
    if (!healthy(i)) continue;
    if (n < 3 || !goodNow[i] || fabsf(tNow[i] - med) <= f.divergenceC) {
      if (n < 3 || goodNow[i]) st.diverging = false;
      continue;
    }
    if (!st.diverging) { st.diverging = true; st.divergeSinceMs = now; continue; }
    if (elapsedMs(now, st.divergeSinceMs) >= f.divergenceMs) exclude(i, Exclusion::Divergence);
  }
}

int SensorHealth::healthyCabinet() const {
  int n = 0;
  for (int i = 0; i < kCabinetSensors; i++) if (healthy(i)) n++;
  return n;
}

CabinetSummary SensorHealth::summary() const {
  CabinetSummary c;
  float sumC = 0, sumRh = 0;
  for (int i = 0; i < kNumSensors; i++) {
    if (!healthy(i)) continue;
    c.sensorsOk++;
    const float t = s_[i].tempC, rh = s_[i].rhPct;
    if (i < kCabinetSensors) {
      if (c.cabinetOk == 0) { c.minC = c.maxC = t; c.maxRhPct = rh; }
      if (t < c.minC) c.minC = t;
      if (t > c.maxC) c.maxC = t;
      if (rh > c.maxRhPct) c.maxRhPct = rh;
      sumC += t;
      sumRh += rh;
      c.cabinetOk++;
    } else if (i == kChamberIndex) {
      c.chamberOk = true;
      c.chamberC = t;
    } else {
      c.ambientOk = true;
      c.ambientC = t;
      c.ambientRhPct = rh;
    }
  }
  if (c.cabinetOk > 0) {
    c.meanC = sumC / c.cabinetOk;
    c.meanRhPct = sumRh / c.cabinetOk;
    c.spreadC = c.maxC - c.minC;
    // Control source (4.2): the mean, or the warmest healthy sensor when the spread is over 2 C.
    c.controlWarmest = c.spreadC > 2.0f;
    c.controlC = c.controlWarmest ? c.maxC : c.meanC;
  }
  return c;
}

// ---------------------------------------------------------------- RequirementGate
void RequirementGate::step(Gate& g, bool met, uint32_t now) {
  if (!met) {
    g.met = false;
    g.ok = false;
    return;
  }
  if (!g.everMet) {                 // first time at boot: no extra hold (the boot hold covers it)
    g.everMet = g.met = g.ok = true;
    return;
  }
  if (!g.met) { g.met = true; g.metSinceMs = now; }
  if (!g.ok && elapsedMs(now, g.metSinceMs) >= holdMs_) g.ok = true;
}

void RequirementGate::update(bool tempMet, bool humidMet, uint32_t now) {
  step(temp_, tempMet, now);
  step(humid_, humidMet, now);
}

uint32_t RequirementGate::left(const Gate& g, uint32_t now) const {
  if (!g.met || g.ok) return 0;
  const uint32_t had = elapsedMs(now, g.metSinceMs);
  return had >= holdMs_ ? 0 : holdMs_ - had;
}

}  // namespace wineador
