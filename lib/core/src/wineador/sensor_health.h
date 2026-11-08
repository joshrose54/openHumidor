#pragma once
// Sensor health (spec 4.3), the serial check (5.3) and calibration (2.3, 5.3).
// Plain logic: fed the result of each sensor sweep, it decides which sensors
// are healthy, and works out the figures the control logic and guards use.
//
// A sensor is EXCLUDED on any of (4.3):
//   3 CRC failures in a row · 3 timeouts in a row · no response (absent) ·
//   an implausible RAW value (outside -10..50 C or 0..100 %RH, before clamping) ·
//   divergence (cabinet sensors only, >5 C from the median of the healthy cabinet
//   sensors for 10 min, only while >= 3 are healthy) · a bus fault on its channel ·
//   a wrong serial (5.3) · the user's manual exclude.
// It RECOVERS after 10 good, plausible reads in a row (after a divergence
// exclusion, all 10 within 5 C of the median), and then only once its serial
// has been re-read and matches (5.3: plugs may have been swapped while it was out).
// At boot a sensor is healthy from its first good, plausible read with the right
// serial.
//
// A healthy sensor whose read failed this sweep (1 or 2 CRC failures or
// timeouts) stays healthy; its last good value is used.
//
// In a test build the VALUE checks (implausible, divergence) see the scripted
// values; the BUS checks (CRC, timeout, absent, serial, bus fault) stay real.

#include "wineador/types.h"
#include "wineador/settings.h"
#include "wineador/summary.h"

namespace wineador {

enum class Exclusion : uint8_t {
  None,          // healthy
  NotYetRead,    // no good read with a checked serial yet (boot, or serial not read)
  CrcFail,
  Timeout,
  Absent,
  Implausible,
  Divergence,
  BusFault,
  WrongSerial,
  Manual,
};
const char* exclusionName(Exclusion e);   // short, e.g. "crc", "absent"

// Corrected RH for sensor index i (per its serial's coefficients), clamped 0..100.
float correctedRh(int index, float rawPct);
// The raw value that corrects to `pct` (test builds script corrected RH, 2.3).
float rawForCorrectedRh(int index, float pct);

class SensorHealth {
 public:
  struct Event {
    int index = -1;
    bool excluded = false;       // true = excluded now; false = recovered
    Exclusion reason = Exclusion::None;
  };

  void begin(const Settings& s);

  // The serials read at boot (or re-read for a recovery). bus = how the read went.
  void serialResult(int index, BusResult bus, uint32_t serial);

  // After each sweep. busFault: the bus locked with this channel selected
  // (the reading's bus result is BusStuck too).
  void update(const Readings& r, uint32_t nowMs);

  // The driver should re-read this sensor's serial in the next sweep.
  bool wantsSerial(int index) const { return s_[index].wantSerial; }

  bool healthy(int i) const { return s_[i].reason == Exclusion::None; }
  Exclusion reason(int i) const { return s_[i].reason; }
  uint8_t goodRun(int i) const { return s_[i].goodRun; }
  bool diverging(int i) const { return s_[i].diverging; }
  // Last good values (meaningful while healthy).
  float tempC(int i) const { return s_[i].tempC; }
  float rhPct(int i) const { return s_[i].rhPct; }   // corrected

  int healthyCabinet() const;
  bool chamberHealthy() const { return healthy(kChamberIndex); }

  // Figures from the healthy sensors' last good values (corrected RH).
  CabinetSummary summary() const;

  bool takeEvent(Event& e);

 private:
  struct State {
    Exclusion reason = Exclusion::NotYetRead;
    bool serialChecked = false;   // the serial on this channel has been read and matches
    bool wantSerial = false;      // ask the driver to (re-)read the serial
    uint8_t crcRun = 0, timeoutRun = 0, goodRun = 0;
    bool diverging = false;
    uint32_t divergeSinceMs = 0;
    float tempC = 0, rhPct = 0;
  };
  void exclude(int i, Exclusion why);
  void push(const Event& e);
  float medianHealthyCabinet(int skip, int& count) const;

  Settings set_;
  State s_[kNumSensors];
  static constexpr int kQ = 16;
  Event q_[kQ];
  int qHead_ = 0, qLen_ = 0;
};

// The 4.3 requirements with the sensor recovery hold:
//   temperature loads (compressor, heater): chamber healthy AND >= 3 of 4 cabinet
//   humidifier: >= 3 of 4 cabinet
// When one fails, its loads stop at once. When it is met again they wait the
// recovery hold (5 min real, 30 s bench). Before it has ever been met (boot)
// there is no extra hold: the boot hold covers that.
class RequirementGate {
 public:
  void begin(uint32_t holdMs) { holdMs_ = holdMs; temp_ = Gate{}; humid_ = Gate{}; }
  void update(bool tempMet, bool humidMet, uint32_t nowMs);
  bool tempOk() const { return temp_.ok; }
  bool humidOk() const { return humid_.ok; }
  bool tempHolding() const { return temp_.met && !temp_.ok; }
  bool humidHolding() const { return humid_.met && !humid_.ok; }
  uint32_t tempHoldLeftMs(uint32_t nowMs) const { return left(temp_, nowMs); }
  uint32_t humidHoldLeftMs(uint32_t nowMs) const { return left(humid_, nowMs); }

 private:
  struct Gate {
    bool everMet = false, met = false, ok = false, holding = false;
    uint32_t metSinceMs = 0;
  };
  void step(Gate& g, bool met, uint32_t nowMs);
  uint32_t left(const Gate& g, uint32_t nowMs) const;
  uint32_t holdMs_ = 0;
  Gate temp_, humid_;
};

}  // namespace wineador
