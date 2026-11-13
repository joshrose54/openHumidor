#pragma once
// The circulation blowers' speed (spec 4.6b, Rev Q). Plain logic: no hardware calls. The loop
// gives it what it already has each pass and drives GPIO15 with the duty it returns.
//
// Each pass, the first of these that applies:
//   1. Updating (an OTA update, 5.2)                         -> idle, even in a fault (every load is off)
//   2. Fault (5.5's meaning: the 4.3 sensor requirement not met including its recovery hold, the
//      plenum fan stalled, or the settings failing their boot check)  -> full speed
//   3. no fresh reading (none for twice the sensor read rate, or none yet since boot) -> full speed
//   4. otherwise the cabinet spread (highest minus lowest of the healthy cabinet sensors, ch 0-3,
//      temperature and corrected RH), judged on each finished sweep:
//        - above 1.0 C or above 3 points  -> boost (full speed) at once;
//        - in boost, below 0.5 C and below 2 points on every sweep for 10 minutes -> idle;
//        - in between, the blowers stay as they are. "Above" and "below" are strict.
// After any spell at full speed or idle-for-an-update (boot, a fault ending, an update ending) the
// rule starts in BOOST, so it needs its 10 minutes of low spread before idle. The hold is timed by
// uptime (millis), never the clock. The rule runs as normal during the boot hold.
//
// The idle duty is TuningSettings::blowerIdlePct: FULL SPEED (100) until BP2 test 22 chooses it.

#include <stdint.h>
#include <stddef.h>
#include "wineador/settings.h"

namespace wineador {

enum class BlowerMode : uint8_t {
  Idle,       // the idle duty
  Boost,      // full speed: the spread was wide (or the rule has just started); waiting for 10 min of low spread
  Full,       // full speed: a fault, or no fresh reading
  Updating,   // the idle duty: an OTA update is running
};
enum class BlowerWhy : uint8_t { None, Boot, Fault, Stale, Updating };   // why Full (Boot: no reading yet since boot);
                                                                    // Updating: only as a Boost event's "after"

struct BlowerInputs {
  uint32_t nowMs = 0;
  bool updating = false;
  bool fault = false;          // 5.5's Fault
  bool fresh = false;          // a sweep has finished within twice the sensor read rate (and one since boot)
  bool sweepDone = false;      // a sweep finished this pass: judge the spread
  int cabinetOk = 0;           // healthy cabinet sensors (the spread needs two)
  float spreadC = 0;
  float spreadRhPct = 0;
};

class BlowerRule {
 public:
  enum class Event : uint8_t { None, Boost, Idle, FullStart, FullEnd };

  void begin(const TuningSettings& t);   // starts Full (Boot): GPIO15 is high from reset
  // Every pass. Returns the duty to drive, percent (100 = full speed).
  uint8_t update(const BlowerInputs& in);

  BlowerMode mode() const { return mode_; }
  BlowerWhy why() const { return why_; }
  uint8_t pct() const { return pct_; }
  bool holding() const { return holding_; }                // in boost, the low spread is being held
  uint32_t holdLeftMs(uint32_t nowMs) const;               // 0 when not holding
  float lastSpreadC() const { return spreadC_; }           // from the last sweep judged
  float lastSpreadRhPct() const { return spreadRh_; }

  // Events since the last call, oldest first (the loop logs them). The spread and the reason
  // are those at the change.
  struct Change {
    Event e = Event::None;
    BlowerWhy why = BlowerWhy::None;
    float spreadC = 0, spreadRhPct = 0;
    uint32_t heldMs = 0;     // Idle: how long the low spread was held
    BlowerWhy after = BlowerWhy::None;   // Boost on starting: what came before (boot, full speed, an update)
  };
  bool take(Change* c);

 private:
  void push(const Change& c);
  void enter(BlowerMode m, BlowerWhy w);

  TuningSettings t_;
  BlowerMode mode_ = BlowerMode::Full;
  BlowerWhy why_ = BlowerWhy::Boot;
  uint8_t pct_ = 100;
  bool holding_ = false;
  uint32_t holdSinceMs_ = 0;
  float spreadC_ = 0, spreadRh_ = 0;

  static constexpr int kQ = 8;
  Change q_[kQ];
  int qHead_ = 0, qLen_ = 0;
};

const char* blowerModeKey(BlowerMode m);   // "idle", "boost", "full", "updating" (docs/api.md)
const char* blowerWhyText(BlowerWhy w);    // "no reading yet since boot", "fault", "no fresh reading"

// The event text (spec 5.4; type "blowers"), from event_text.h's words:
//   "BLOWERS BOOST: spread 1.20 C, 2.0 points (above 1.0 C or 3 points)"
//   "BLOWERS BOOST: after full speed (fault); idle after 10 min of low spread" (or "after boot",
//   "after the update")
//   "BLOWERS IDLE: spread below 0.5 C and 2 points for 10 min; 100%"
//   "BLOWERS FULL SPEED: fault"
//   "BLOWERS FULL SPEED ENDED (fault)"
int formatBlowerEvent(char* out, size_t n, const BlowerRule::Change& c, const TuningSettings& t);

// One line for the status line, serial and the dashboard: "100% idle", "100% boost (idle in 4:10)",
// "100% FULL (fault)", "100% idle (updating)".
int formatBlowerState(char* out, size_t n, const BlowerRule& b, uint32_t nowMs);

}  // namespace wineador
