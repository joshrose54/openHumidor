#pragma once
// Rev Q (3A.7): the mux and sensors on I2C0 (GPIO35/36, 25 kHz); the DS3231 on I2C1 (GPIO21/14,
// 100 kHz), its own bus, restarted before the next RTC step if one fails. Rev N: one bus for all.
//
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
// Stage 3 adds (spec 5.6, 5.3, 5.4):
//   - STUCK BUS: after a failed sensor transaction, a follow-up to the mux on the
//     main bus. If that fails too, the bus is stuck: mux RST pulsed (a state, not a
//     delay), Wire restarted, and the channel's result is BusStuck. The sweep goes
//     on with the next sensor; the RTC and other sensors keep working.
//   - SERIAL RE-READS for any sensor the health logic asks about (5.3).
//   - The DS3231 status register at boot: OSF (oscillator stop = lost time).
//   - Writing the DS3231 time, and clearing OSF, between sweeps.
// Health decisions are in lib/core/sensor_health.h, not here.

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

// Serials: the boot sweep reads all six; later sweeps read those asked for.
void wantSerial(int index);                    // read this sensor's serial in the next sweep
bool takeSerial(int index, SerialResult& out); // true once per fresh serial read

// Stuck-bus recoveries since boot, and the sensor index selected at the last one.
uint32_t busStuckCount();
int lastStuckIndex();

// DS3231 status register, read in the boot sweep.
bool rtcStatusKnown();
bool rtcOsf();                                 // oscillator stop flag: the clock lost time

// Write the DS3231's time (between sweeps), and clear OSF afterwards if asked.
void requestRtcWrite(const wineador::DateTime& dt, bool clearOsf);
int takeRtcWriteResult();                      // 0 none yet, 1 written, -1 failed

bool rtcOk();                                  // last RTC read succeeded
bool rtcDateTime(wineador::DateTime& dt);      // last good RTC read
uint32_t rtcReadAtMs();                        // millis() when that read happened
bool internalPullupsOff();                     // checked from the IO_MUX registers (Rev Q: both buses)
uint32_t rtcBusRestarts();                     // Rev Q: times I2C1 was restarted after a failed RTC step

#if defined(WINEADOR_TEST_BUILD)
// TEST BUILDS ONLY (B3 test 24): the next measurement read of this sensor reports
// a CRC failure (CrcFail) or a timeout (ShortRead). One read only.
void injectFault(int index, wineador::BusResult r);
#endif

}  // namespace i2cbus
