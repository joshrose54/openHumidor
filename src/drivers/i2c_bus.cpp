#include "i2c_bus.h"
#include <Arduino.h>
#include <Wire.h>
#include "driver/gpio.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_periph.h"
#include "pins.h"
#include "board.h"

static_assert(sizeof(board::kSensorMuxChannel) == wineador::kNumSensors, "one mux channel per sensor");

using wineador::BusResult;
using wineador::kNumSensors;

namespace i2cbus {

#if defined(WINEADOR_TEST_BUILD)
static BusResult gInject[kNumSensors] = {BusResult::Ok, BusResult::Ok, BusResult::Ok, BusResult::Ok, BusResult::Ok, BusResult::Ok};
#endif

constexpr uint32_t kBusHz = 25000;          // spec 2.3 / 5.3 (Rev Q: I2C0, the mux)
constexpr uint32_t kRtcBusHz = 100000;      // Rev Q: I2C1, RTC and touch, 100 kHz (3A.7)
constexpr uint16_t kWireTimeoutMs = 20;     // hard timeout on every transaction (5.2)
constexpr uint32_t kResetLowMs = 1;         // RST held low this long (datasheet needs nanoseconds)
constexpr uint32_t kResetRecoverMs = 1;
constexpr uint32_t kSht40WaitMs = 10;       // high-precision measurement takes up to 8.3 ms
constexpr uint8_t kCmdMeasure = 0xFD;
constexpr uint8_t kCmdSerial = 0x89;

enum class St : uint8_t {
  ResetLow, ResetRecover, Idle, RtcPointer, RtcRead, StatusPointer, StatusRead, Select,
  SerialCmd, SerialWait, SerialRead, MeasCmd, MeasWait, MeasRead,
  Probe, StuckResetLow, StuckRecover, Deselect, RtcWrite, StatusWrite, EoscClear, EoscWrite,
};

static St gSt = St::ResetLow;
static uint32_t gT = 0;               // time the current wait started
static int gIdx = 0;                  // sensor index 0..5 in this sweep
static bool gBootSweep = true;        // the first sweep also reads every serial and the RTC status
static bool gSweepRequested = false;
static bool gDone = false;
static bool gLastWasBoot = false;

static wineador::Readings gWork, gPublished;
static SerialResult gSerials[kNumSensors];
static bool gSerialFresh[kNumSensors] = {};
static bool gWantSerial[kNumSensors] = {};
static bool gReadSerialThis = false;  // reading this sensor's serial in this sweep
static bool gRtcOk = false;
static bool gHaveTime = false;
static wineador::DateTime gTime;
static uint32_t gTimeAtMs = 0;       // millis() when gTime was read

// Stuck bus (5.6)
static BusResult gPendingFail = BusResult::NotRead;
static uint32_t gStuckCount = 0;
static int gLastStuck = -1;

// DS3231 status register and writes
static bool gStatusKnown = false;
static uint8_t gStatus = 0;
static uint8_t gControl = 0;          // the control register (0x0E), read with the status at boot
static bool gEoscCleared = false;     // EOSC was found set at boot and cleared (3A.11): log it
static bool gEoscPending = false, gEoscWant = false;   // TEST BUILDS: "rtc eosc on|off"
static int gEoscResult = 0;
static bool gWritePending = false, gWriteClearOsf = false;
static wineador::DateTime gWriteTime;
static int gWriteResult = 0;

// The RTC's bus. Rev N: the one bus, shared with the mux, as through B3 and B4. Rev Q: I2C1
// (GPIO21 SDA / GPIO14 SCL, 100 kHz) with the touch controller, so a sensor holding I2C0
// can't stop the clock being read (3A.7). The RTC steps are the same on both.
#if defined(WINEADOR_BOARD_REVQ)
static TwoWire& rtcWire() { return Wire1; }
static bool gRtcRestart = false;      // an RTC step failed: restart I2C1 before the next one
static uint32_t gRtcRestarts = 0;
#else
static TwoWire& rtcWire() { return Wire; }
#endif

// ---------------------------------------------------------------- transactions
static bool muxWrite(uint8_t mask) {
  Wire.beginTransmission(i2caddr::kMux);
  Wire.write(mask);
  return Wire.endTransmission() == 0;
}

static bool shtCommand(uint8_t cmd) {
  Wire.beginTransmission(i2caddr::kSht40);
  Wire.write(cmd);
  return Wire.endTransmission() == 0;
}

// Reads 6 bytes. Returns Ok, ShortRead; caller decodes CRC.
static BusResult shtRead6(uint8_t b[6]) {
  size_t got = Wire.requestFrom(i2caddr::kSht40, (size_t)6);
  if (got != 6) {
    while (Wire.available()) Wire.read();
    return BusResult::ShortRead;
  }
  for (int i = 0; i < 6; i++) b[i] = (uint8_t)Wire.read();
  return BusResult::Ok;
}

// RTC in two steps, one transaction each: point at register 0x00, then read 7 bytes.
static bool rtcPointer() {
  rtcWire().beginTransmission(i2caddr::kRtc);
  rtcWire().write((uint8_t)0x00);
  return rtcWire().endTransmission() == 0;
}

static void rtcRead() {
  if (rtcWire().requestFrom(i2caddr::kRtc, (size_t)7) != 7) {
    while (rtcWire().available()) rtcWire().read();
    return;
  }
  uint8_t r[7];
  for (int i = 0; i < 7; i++) r[i] = (uint8_t)rtcWire().read();
  wineador::DateTime dt;
  if (!wineador::decodeDs3231(r, dt)) return;
  gTime = dt;
  gTimeAtMs = millis();
  gHaveTime = true;
  gRtcOk = true;
}

// Boot: the control register (0x0E) and the status register (0x0F) in one read.
static bool rtcStatusPointer() {
  rtcWire().beginTransmission(i2caddr::kRtc);
  rtcWire().write(wineador::kDs3231ControlReg);
  return rtcWire().endTransmission() == 0;
}

static void rtcStatusRead() {
  if (rtcWire().requestFrom(i2caddr::kRtc, (size_t)2) != 2) {
    while (rtcWire().available()) rtcWire().read();
    return;
  }
  gControl = (uint8_t)rtcWire().read();
  gStatus = (uint8_t)rtcWire().read();
  gStatusKnown = true;
}

// The control register with EOSC set or cleared, the other bits as read.
static bool rtcWriteControl(bool eosc) {
  const uint8_t v = wineador::ds3231WithEosc(gControl, eosc);
  rtcWire().beginTransmission(i2caddr::kRtc);
  rtcWire().write(wineador::kDs3231ControlReg);
  rtcWire().write(v);
  if (rtcWire().endTransmission() != 0) return false;
  gControl = v;
  return true;
}

// Registers 0x00-0x06 in one transaction.
static bool rtcWriteTime(const wineador::DateTime& dt) {
  uint8_t r[7];
  wineador::encodeDs3231(dt, r);
  rtcWire().beginTransmission(i2caddr::kRtc);
  rtcWire().write((uint8_t)0x00);
  for (int i = 0; i < 7; i++) rtcWire().write(r[i]);
  return rtcWire().endTransmission() == 0;
}

// Clears OSF (bit 7) and leaves the other status bits as they were read.
static bool rtcClearOsf() {
  rtcWire().beginTransmission(i2caddr::kRtc);
  rtcWire().write((uint8_t)0x0F);
  rtcWire().write((uint8_t)(gStatus & 0x7F));
  if (rtcWire().endTransmission() != 0) return false;
  gStatus &= 0x7F;
  return true;
}

// The ESP32's internal pull-ups off (5.3). The Arduino core creates the bus with
// enable_internal_pullup set, and ESP-IDF's driver turns them back on whenever it
// resets the bus after a timeout (seen on the bench, B3 test 15: on during a
// stuck-bus recovery). So this runs after every Wire start AND at the end of every
// bus step — two register writes.
static void pullupsOff() {
  gpio_pullup_dis((gpio_num_t)pins::kI2cSda);
  gpio_pullup_dis((gpio_num_t)pins::kI2cScl);
#if defined(WINEADOR_BOARD_REVQ)
  gpio_pullup_dis((gpio_num_t)pins::kI2c1Sda);
  gpio_pullup_dis((gpio_num_t)pins::kI2c1Scl);
#endif
}

// Wire set up as spec 5.3: 25 kHz, hard timeout, the ESP32's internal pull-ups off.
static void wireStart() {
  Wire.begin(pins::kI2cSda, pins::kI2cScl);
  Wire.setClock(kBusHz);
  Wire.setTimeOut(kWireTimeoutMs);
  pullupsOff();
}

#if defined(WINEADOR_BOARD_REVQ)
// I2C1 set up the same way: 100 kHz, the same hard timeout, internal pull-ups off.
static void rtcWireStart() {
  Wire1.begin(pins::kI2c1Sda, pins::kI2c1Scl);
  Wire1.setClock(kRtcBusHz);
  Wire1.setTimeOut(kWireTimeoutMs);
  pullupsOff();
}
// Before an RTC step: if the last one failed, restart I2C1 (its driver frees a stuck line).
static void rtcBusCheck() {
  if (!gRtcRestart) return;
  gRtcRestart = false;
  gRtcRestarts++;
  Wire1.end();
  rtcWireStart();
}
static void rtcFailed() { gRtcRestart = true; }
#else
static void rtcBusCheck() {}
static void rtcFailed() {}
#endif

// A sensor transaction failed: check the main bus before blaming the sensor.
static void failed(BusResult r) {
  gPendingFail = r;
  gSt = St::Probe;
}

// ---------------------------------------------------------------- public
void begin(uint32_t nowMs) {
  // Pulse the mux reset before the first I2C access: the mux keeps its channel
  // selection across an ESP32 reset (5.3). Released in update() — no delay.
  pinMode(pins::kMuxReset, OUTPUT);
  digitalWrite(pins::kMuxReset, LOW);
  gT = nowMs;
  gSt = St::ResetLow;

  wireStart();
#if defined(WINEADOR_BOARD_REVQ)
  rtcWireStart();
#endif
}

bool internalPullupsOff() {
  bool sda = REG_GET_BIT(GPIO_PIN_MUX_REG[pins::kI2cSda], FUN_PU) != 0;
  bool scl = REG_GET_BIT(GPIO_PIN_MUX_REG[pins::kI2cScl], FUN_PU) != 0;
#if defined(WINEADOR_BOARD_REVQ)
  sda = sda || REG_GET_BIT(GPIO_PIN_MUX_REG[pins::kI2c1Sda], FUN_PU) != 0;
  scl = scl || REG_GET_BIT(GPIO_PIN_MUX_REG[pins::kI2c1Scl], FUN_PU) != 0;
#endif
  return !sda && !scl;
}

uint8_t rtcControl() { return gControl; }
bool takeEoscCleared() { bool c = gEoscCleared; gEoscCleared = false; return c; }
void requestEosc(bool on) { gEoscWant = on; gEoscPending = true; gEoscResult = 0; }
int takeEoscResult() { int r = gEoscResult; gEoscResult = 0; return r; }

uint32_t rtcBusRestarts() {
#if defined(WINEADOR_BOARD_REVQ)
  return gRtcRestarts;
#else
  return 0;
#endif
}

bool idle() { return gSt == St::Idle; }

#if defined(WINEADOR_TEST_BUILD)
void injectFault(int index, BusResult r) {
  if (index >= 0 && index < kNumSensors && (r == BusResult::CrcFail || r == BusResult::ShortRead)) gInject[index] = r;
}
#endif

bool startSweep() {
  if (gSt != St::Idle) return false;
  gSweepRequested = true;
  return true;
}

bool takeSweepDone() {
  bool d = gDone;
  gDone = false;
  return d;
}

bool lastSweepWasBoot() { return gLastWasBoot; }
const wineador::Readings& readings() { return gPublished; }
const SerialResult& serial(int i) { return gSerials[(i >= 0 && i < kNumSensors) ? i : 0]; }
bool rtcOk() { return gRtcOk; }

void wantSerial(int i) { if (i >= 0 && i < kNumSensors) gWantSerial[i] = true; }
bool takeSerial(int i, SerialResult& out) {
  if (i < 0 || i >= kNumSensors || !gSerialFresh[i]) return false;
  gSerialFresh[i] = false;
  out = gSerials[i];
  return true;
}
uint32_t busStuckCount() { return gStuckCount; }
int lastStuckIndex() { return gLastStuck; }
uint32_t rtcReadAtMs() { return gTimeAtMs; }
bool rtcStatusKnown() { return gStatusKnown; }
bool rtcOsf() { return gStatusKnown && (gStatus & 0x80); }
void requestRtcWrite(const wineador::DateTime& dt, bool clearOsf) {
  gWriteTime = dt;
  gWriteClearOsf = clearOsf;
  gWritePending = true;
}
int takeRtcWriteResult() { int r = gWriteResult; gWriteResult = 0; return r; }
bool rtcDateTime(wineador::DateTime& dt) {
  if (!gHaveTime) return false;
  dt = gTime;
  return true;
}

static void step(uint32_t nowMs);

void update(uint32_t nowMs) {
  const St before = gSt;
  step(nowMs);
  // Any state that may have run a transaction: the driver may have reset the bus
  // and switched the pull-ups back on (see pullupsOff).
  if (before != St::Idle && before != St::ResetLow && before != St::ResetRecover &&
      before != St::SerialWait && before != St::MeasWait && before != St::StuckResetLow &&
      before != St::StuckRecover) {
    pullupsOff();
  }
}

static void step(uint32_t nowMs) {
  switch (gSt) {
    case St::ResetLow:
      if (nowMs - gT >= kResetLowMs) {
        digitalWrite(pins::kMuxReset, HIGH);
        gT = nowMs;
        gSt = St::ResetRecover;
      }
      break;

    case St::ResetRecover:
      if (nowMs - gT >= kResetRecoverMs) {
        gSt = St::Idle;
        gSweepRequested = true;      // the boot sweep starts by itself
      }
      break;

    case St::Idle:
      if (gSweepRequested) {
        gSweepRequested = false;
        gWork = wineador::Readings{};
        gSt = St::RtcPointer;
      } else if (gWritePending) {
        gWritePending = false;
        gSt = St::RtcWrite;
      } else if (gEoscPending) {
        gEoscPending = false;
        gSt = St::EoscWrite;
      }
      break;

    case St::RtcPointer:
      gRtcOk = false;
      gIdx = 0;
      rtcBusCheck();                    // Rev Q: restart I2C1 first if the last RTC step failed
      if (rtcPointer()) gSt = St::RtcRead;
      else { rtcFailed(); gSt = gBootSweep ? St::StatusPointer : St::Select; }
      break;

    case St::RtcRead:
      rtcRead();                        // sets gRtcOk on a good read
      if (!gRtcOk) rtcFailed();
      gSt = gBootSweep ? St::StatusPointer : St::Select;
      break;

    case St::StatusPointer:             // boot only: the status register, for OSF (5.6)
      rtcBusCheck();
      if (rtcStatusPointer()) gSt = St::StatusRead;
      else { rtcFailed(); gSt = St::Select; }
      break;

    case St::StatusRead:
      rtcStatusRead();
      if (!gStatusKnown) rtcFailed();
      // 3A.11: EOSC set at boot (a test left it) would stop the clock at every power cut: clear it.
      gSt = (gStatusKnown && (gControl & wineador::kDs3231Eosc)) ? St::EoscClear : St::Select;
      break;

    case St::EoscClear:
      if (rtcWriteControl(false)) gEoscCleared = true;
      else rtcFailed();
      gSt = St::Select;
      break;

    case St::Select:
      if (gIdx >= kNumSensors) { gSt = St::Deselect; break; }
      gReadSerialThis = gBootSweep || gWantSerial[gIdx];
      gWantSerial[gIdx] = false;
      if (!muxWrite((uint8_t)(1u << board::kSensorMuxChannel[gIdx]))) { failed(BusResult::MuxFail); break; }
      gSt = gReadSerialThis ? St::SerialCmd : St::MeasCmd;
      break;

    case St::SerialCmd:
      if (!shtCommand(kCmdSerial)) {
        gSerials[gIdx].bus = BusResult::NoAck;
        gSerialFresh[gIdx] = true;
        gSt = St::MeasCmd;              // still try the measurement
        break;
      }
      gT = nowMs;
      gSt = St::SerialWait;
      break;

    case St::SerialWait:
      if (nowMs - gT >= kSht40WaitMs) gSt = St::SerialRead;
      break;

    case St::SerialRead: {
      uint8_t b[6];
      BusResult r = shtRead6(b);
      if (r == BusResult::Ok) r = wineador::decodeSht40Serial(b, gSerials[gIdx].serial);
      gSerials[gIdx].bus = r;
      gSerialFresh[gIdx] = true;
      gSt = St::MeasCmd;
      break;
    }

    case St::MeasCmd:
      if (!shtCommand(kCmdMeasure)) { failed(BusResult::NoAck); break; }
      gT = nowMs;
      gSt = St::MeasWait;
      break;

    case St::MeasWait:
      if (nowMs - gT >= kSht40WaitMs) gSt = St::MeasRead;
      break;

    case St::MeasRead: {
      uint8_t b[6];
      wineador::SensorReading& s = gWork.s[gIdx];
      BusResult r = shtRead6(b);
      if (r == BusResult::ShortRead) { failed(r); break; }
      if (r == BusResult::Ok) r = wineador::decodeSht40Measurement(b, s.tempC, s.rhRawPct);
      s.bus = r;                        // Ok or CrcFail: the bus itself worked
#if defined(WINEADOR_TEST_BUILD)
      // B3 test 24: a test-build command turns this one read into a CRC failure or a timeout.
      if (gInject[gIdx] != BusResult::Ok) { s.bus = gInject[gIdx]; gInject[gIdx] = BusResult::Ok; }
#endif
      gIdx++;
      gSt = St::Select;
      break;
    }

    case St::Probe:
      // Follow-up on the main bus (5.6): deselect every channel. If the mux answers,
      // it was this sensor; if not, the bus is stuck.
      if (muxWrite(0)) {
        gWork.s[gIdx].bus = gPendingFail;
        gIdx++;
        gSt = St::Select;
        break;
      }
      gWork.s[gIdx].bus = BusResult::BusStuck;
      if (gReadSerialThis) { gSerials[gIdx].bus = BusResult::BusStuck; gSerialFresh[gIdx] = true; }
      gStuckCount++;
      gLastStuck = gIdx;
      // Pulse mux RST (disconnects every channel) and restart the I2C driver.
      digitalWrite(pins::kMuxReset, LOW);
      Wire.end();
      gT = nowMs;
      gSt = St::StuckResetLow;
      break;

    case St::StuckResetLow:
      if (nowMs - gT >= kResetLowMs) {
        wireStart();
        digitalWrite(pins::kMuxReset, HIGH);
        gT = nowMs;
        gSt = St::StuckRecover;
      }
      break;

    case St::StuckRecover:
      if (nowMs - gT >= kResetRecoverMs) {
        gIdx++;                         // on with the next sensor
        gSt = St::Select;
      }
      break;

    case St::Deselect:
      muxWrite(0);                      // no channel left selected between sweeps
      gPublished = gWork;
      gLastWasBoot = gBootSweep;
      gBootSweep = false;
      gDone = true;
      gSt = St::Idle;
      break;

    case St::RtcWrite:
      rtcBusCheck();
      if (!rtcWriteTime(gWriteTime)) { rtcFailed(); gWriteResult = -1; gSt = St::Idle; break; }
      if (gWriteClearOsf && gStatusKnown) { gSt = St::StatusWrite; break; }
      gWriteResult = 1;
      gSt = St::Idle;
      break;

    case St::EoscWrite:                 // TEST BUILDS: "rtc eosc on|off"
      rtcBusCheck();
      gEoscResult = (gStatusKnown && rtcWriteControl(gEoscWant)) ? 1 : -1;
      if (gEoscResult < 0) rtcFailed();
      gSt = St::Idle;
      break;

    case St::StatusWrite:
      gWriteResult = rtcClearOsf() ? 1 : -1;
      if (gWriteResult < 0) rtcFailed();
      gSt = St::Idle;
      break;
  }
}

}  // namespace i2cbus
