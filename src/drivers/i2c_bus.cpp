#include "i2c_bus.h"
#include <Arduino.h>
#include <Wire.h>
#include "driver/gpio.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_periph.h"
#include "pins.h"

using wineador::BusResult;
using wineador::kNumSensors;

namespace i2cbus {

constexpr uint32_t kBusHz = 25000;          // spec 2.3 / 5.3
constexpr uint16_t kWireTimeoutMs = 20;     // hard timeout on every transaction (5.2)
constexpr uint32_t kResetLowMs = 1;         // RST held low this long (datasheet needs nanoseconds)
constexpr uint32_t kResetRecoverMs = 1;
constexpr uint32_t kSht40WaitMs = 10;       // high-precision measurement takes up to 8.3 ms
constexpr uint8_t kCmdMeasure = 0xFD;
constexpr uint8_t kCmdSerial = 0x89;

enum class St : uint8_t {
  ResetLow, ResetRecover, Idle, RtcPointer, RtcRead, StatusPointer, StatusRead, Select,
  SerialCmd, SerialWait, SerialRead, MeasCmd, MeasWait, MeasRead,
  Probe, StuckResetLow, StuckRecover, Deselect, RtcWrite, StatusWrite,
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
static bool gWritePending = false, gWriteClearOsf = false;
static wineador::DateTime gWriteTime;
static int gWriteResult = 0;

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
  Wire.beginTransmission(i2caddr::kRtc);
  Wire.write((uint8_t)0x00);
  return Wire.endTransmission() == 0;
}

static void rtcRead() {
  if (Wire.requestFrom(i2caddr::kRtc, (size_t)7) != 7) {
    while (Wire.available()) Wire.read();
    return;
  }
  uint8_t r[7];
  for (int i = 0; i < 7; i++) r[i] = (uint8_t)Wire.read();
  wineador::DateTime dt;
  if (!wineador::decodeDs3231(r, dt)) return;
  gTime = dt;
  gTimeAtMs = millis();
  gHaveTime = true;
  gRtcOk = true;
}

static bool rtcStatusPointer() {
  Wire.beginTransmission(i2caddr::kRtc);
  Wire.write((uint8_t)0x0F);
  return Wire.endTransmission() == 0;
}

static void rtcStatusRead() {
  if (Wire.requestFrom(i2caddr::kRtc, (size_t)1) != 1) {
    while (Wire.available()) Wire.read();
    return;
  }
  gStatus = (uint8_t)Wire.read();
  gStatusKnown = true;
}

// Registers 0x00-0x06 in one transaction.
static bool rtcWriteTime(const wineador::DateTime& dt) {
  uint8_t r[7];
  wineador::encodeDs3231(dt, r);
  Wire.beginTransmission(i2caddr::kRtc);
  Wire.write((uint8_t)0x00);
  for (int i = 0; i < 7; i++) Wire.write(r[i]);
  return Wire.endTransmission() == 0;
}

// Clears OSF (bit 7) and leaves the other status bits as they were read.
static bool rtcClearOsf() {
  Wire.beginTransmission(i2caddr::kRtc);
  Wire.write((uint8_t)0x0F);
  Wire.write((uint8_t)(gStatus & 0x7F));
  if (Wire.endTransmission() != 0) return false;
  gStatus &= 0x7F;
  return true;
}

// Wire set up as spec 5.3: 25 kHz, hard timeout, the ESP32's internal pull-ups off.
static void wireStart() {
  Wire.begin(pins::kI2cSda, pins::kI2cScl);
  Wire.setClock(kBusHz);
  Wire.setTimeOut(kWireTimeoutMs);
  // The Arduino core turns the ESP32's internal pull-ups on. Spec 5.3: off.
  gpio_pullup_dis((gpio_num_t)pins::kI2cSda);
  gpio_pullup_dis((gpio_num_t)pins::kI2cScl);
}

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
}

bool internalPullupsOff() {
  bool sda = REG_GET_BIT(GPIO_PIN_MUX_REG[pins::kI2cSda], FUN_PU) != 0;
  bool scl = REG_GET_BIT(GPIO_PIN_MUX_REG[pins::kI2cScl], FUN_PU) != 0;
  return !sda && !scl;
}

bool idle() { return gSt == St::Idle; }

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

void update(uint32_t nowMs) {
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
      }
      break;

    case St::RtcPointer:
      gRtcOk = false;
      gIdx = 0;
      gSt = rtcPointer() ? St::RtcRead : (gBootSweep ? St::StatusPointer : St::Select);
      break;

    case St::RtcRead:
      rtcRead();                        // sets gRtcOk on a good read
      gSt = gBootSweep ? St::StatusPointer : St::Select;
      break;

    case St::StatusPointer:             // boot only: the status register, for OSF (5.6)
      gSt = rtcStatusPointer() ? St::StatusRead : St::Select;
      break;

    case St::StatusRead:
      rtcStatusRead();
      gSt = St::Select;
      break;

    case St::Select:
      if (gIdx >= kNumSensors) { gSt = St::Deselect; break; }
      gReadSerialThis = gBootSweep || gWantSerial[gIdx];
      gWantSerial[gIdx] = false;
      if (!muxWrite((uint8_t)(1u << wineador::channelOf(gIdx)))) { failed(BusResult::MuxFail); break; }
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
      if (!rtcWriteTime(gWriteTime)) { gWriteResult = -1; gSt = St::Idle; break; }
      if (gWriteClearOsf && gStatusKnown) { gSt = St::StatusWrite; break; }
      gWriteResult = 1;
      gSt = St::Idle;
      break;

    case St::StatusWrite:
      gWriteResult = rtcClearOsf() ? 1 : -1;
      gSt = St::Idle;
      break;
  }
}

}  // namespace i2cbus
