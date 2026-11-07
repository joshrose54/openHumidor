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
  ResetLow, ResetRecover, Idle, Rtc, Select,
  SerialCmd, SerialWait, SerialRead, MeasCmd, MeasWait, MeasRead, Deselect,
};

static St gSt = St::ResetLow;
static uint32_t gT = 0;               // time the current wait started
static int gIdx = 0;                  // sensor index 0..5 in this sweep
static bool gBootSweep = true;        // the first sweep also reads serials
static bool gSweepRequested = false;
static bool gDone = false;
static bool gLastWasBoot = false;

static wineador::Readings gWork, gPublished;
static SerialResult gSerials[kNumSensors];
static bool gRtcOk = false;
static bool gHaveTime = false;
static wineador::DateTime gTime;

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

static void readRtc() {
  gRtcOk = false;
  Wire.beginTransmission(i2caddr::kRtc);
  Wire.write((uint8_t)0x00);
  if (Wire.endTransmission() != 0) return;
  if (Wire.requestFrom(i2caddr::kRtc, (size_t)7) != 7) {
    while (Wire.available()) Wire.read();
    return;
  }
  uint8_t r[7];
  for (int i = 0; i < 7; i++) r[i] = (uint8_t)Wire.read();
  wineador::DateTime dt;
  if (!wineador::decodeDs3231(r, dt)) return;
  gTime = dt;
  gHaveTime = true;
  gRtcOk = true;
}

// ---------------------------------------------------------------- public
void begin(uint32_t nowMs) {
  // Pulse the mux reset before the first I2C access: the mux keeps its channel
  // selection across an ESP32 reset (5.3). Released in update() — no delay.
  pinMode(pins::kMuxReset, OUTPUT);
  digitalWrite(pins::kMuxReset, LOW);
  gT = nowMs;
  gSt = St::ResetLow;

  Wire.begin(pins::kI2cSda, pins::kI2cScl);
  Wire.setClock(kBusHz);
  Wire.setTimeOut(kWireTimeoutMs);
  // The Arduino core turns the ESP32's internal pull-ups on. Spec 5.3: off.
  gpio_pullup_dis((gpio_num_t)pins::kI2cSda);
  gpio_pullup_dis((gpio_num_t)pins::kI2cScl);
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
        gSt = St::Rtc;
      }
      break;

    case St::Rtc:
      readRtc();
      gIdx = 0;
      gSt = St::Select;
      break;

    case St::Select:
      if (gIdx >= kNumSensors) { gSt = St::Deselect; break; }
      if (!muxWrite((uint8_t)(1u << wineador::channelOf(gIdx)))) {
        gWork.s[gIdx].bus = BusResult::MuxFail;
        if (gBootSweep) gSerials[gIdx].bus = BusResult::MuxFail;
        gIdx++;
        break;
      }
      gSt = gBootSweep ? St::SerialCmd : St::MeasCmd;
      break;

    case St::SerialCmd:
      if (!shtCommand(kCmdSerial)) {
        gSerials[gIdx].bus = BusResult::NoAck;
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
      gSt = St::MeasCmd;
      break;
    }

    case St::MeasCmd:
      if (!shtCommand(kCmdMeasure)) {
        gWork.s[gIdx].bus = BusResult::NoAck;
        gIdx++;
        gSt = St::Select;
        break;
      }
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
      if (r == BusResult::Ok) r = wineador::decodeSht40Measurement(b, s.tempC, s.rhRawPct);
      s.bus = r;
      gIdx++;
      gSt = St::Select;
      break;
    }

    case St::Deselect:
      muxWrite(0);                      // no channel left selected between sweeps
      gPublished = gWork;
      gLastWasBoot = gBootSweep;
      gBootSweep = false;
      gDone = true;
      gSt = St::Idle;
      break;
  }
}

}  // namespace i2cbus
