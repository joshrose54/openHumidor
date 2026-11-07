#pragma once
// Shared types for the control logic.
//
// Everything in lib/core is plain C++: no Arduino, no hardware calls. The drivers
// in src/ fill these structs from the real hardware; the native tests fill them
// by hand. That is what lets the logic run on the Mac.

#include <stdint.h>
#include <stddef.h>

namespace wineador {

// ---------------------------------------------------------------- profiles
// Real = the cabinet timers (spec 5.5). Bench = short timers for B3 (spec B3 table).
enum class Profile : uint8_t { Real, Bench };

// ---------------------------------------------------------------- sensors
// Six SHT40s on mux channels 2..7. Index 0..5 everywhere in the code.
constexpr int kNumSensors      = 6;
constexpr int kFirstChannel    = 2;   // index 0 = mux channel 2
constexpr int kCabinetSensors  = 4;   // index 0..3 = ch2..ch5, the control sensors
constexpr int kChamberIndex    = 4;   // ch6, in the plenum — safety only, never control
constexpr int kAmbientIndex    = 5;   // ch7, room air — never control

inline int channelOf(int index) { return index + kFirstChannel; }

// Result of the I2C transaction for one sensor. These are the "bus checks",
// which stay real even in a test build.
enum class BusResult : uint8_t {
  NotRead,     // no read attempted yet
  Ok,
  MuxFail,     // the mux did not acknowledge the channel select
  NoAck,       // the sensor did not acknowledge (absent, unplugged)
  ShortRead,   // fewer bytes than expected (timeout)
  CrcFail,     // bytes arrived but the checksum was wrong
};
const char* busResultName(BusResult r);

struct SensorReading {
  BusResult bus  = BusResult::NotRead;
  float tempC    = 0.0f;   // as converted, uncorrected (spec 5.3)
  float rhRawPct = 0.0f;   // RAW, before calibration and before clamping (spec 4.3)
};

struct Readings {
  SensorReading s[kNumSensors];
};

// ---------------------------------------------------------------- loads
// What the four switched loads are doing. relayHeating = K1 energised.
struct LoadOutputs {
  bool cool         = false;   // P26, SSR -> compressor
  bool heat         = false;   // P27, heater pad
  bool relayHeating = false;   // P32, K1: false = cooling side fed, true = heating side fed
  bool humid        = false;   // P33, humidifier fan

  bool operator==(const LoadOutputs& o) const {
    return cool == o.cool && heat == o.heat && relayHeating == o.relayHeating && humid == o.humid;
  }
  bool operator!=(const LoadOutputs& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------- timing
// Elapsed time between two millis() values. Correct across the 49-day wrap
// as long as the span itself is under 49 days.
inline uint32_t elapsedMs(uint32_t nowMs, uint32_t sinceMs) { return nowMs - sinceMs; }

// Keeps the largest value seen since the last take(). Used for loop time.
struct MaxTracker {
  uint32_t max = 0;
  void add(uint32_t v) { if (v > max) max = v; }
  uint32_t take() { uint32_t m = max; max = 0; return m; }
};

}  // namespace wineador
