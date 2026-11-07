#pragma once
// The I2C bus: mux reset, the six SHT40s through the TCA9548A, and the DS3231
// clock — from soak_test.cpp and sensor_check.cpp, rebuilt as a state machine so
// nothing waits. One short transaction per loop pass; the SHT40's 10 ms
// measuring time is a state, not a delay. Every transaction has a hard timeout.
//
// At boot: RST is pulsed low (spec 5.3), then a boot sweep reads every sensor's
// serial as well as a measurement. After that, startSweep() reads the RTC and
// all six sensors.
//
// How long one step can take, at 25 kHz (one bit = 40 us, one byte + ACK = 360 us):
//   mux select / SHT40 command / RTC pointer   ~0.8 ms     mux deselect ~0.8 ms
//   SHT40 read, 6 bytes                        ~2.6 ms     RTC read, 7 bytes ~2.9 ms
//   sensor absent (NACK on the address)        ~0.4 ms
//   bus stuck (a line held low)                up to the 20 ms Wire timeout — the longest any step can be
// Waiting for the SHT40 to measure (10 ms) is a state checked on later passes, not a wait.
// begin() is BOOT ONLY.
//
// Stage 1 only reports results. Sensor health, serial checking, calibration and
// bus-fault recovery arrive at stage 3.

#include <stdint.h>
#include "wineador/types.h"
#include "wineador/decode.h"

namespace i2cbus {

struct SerialResult {
  wineador::BusResult bus = wineador::BusResult::NotRead;
  uint32_t serial = 0;
};

void begin(uint32_t nowMs);          // BOOT ONLY. RST low, Wire at 25 kHz, internal pull-ups off
void update(uint32_t nowMs);         // call every pass
bool idle();
bool startSweep();                   // false if busy
bool takeSweepDone();                // true once after each sweep completes
bool lastSweepWasBoot();

const wineador::Readings& readings();          // real bus results, raw values
const SerialResult& serial(int index);         // from the boot sweep

bool rtcOk();                                  // last RTC read succeeded
bool rtcDateTime(wineador::DateTime& dt);      // last good RTC read
bool internalPullupsOff();                     // checked from the IO_MUX registers

}  // namespace i2cbus
