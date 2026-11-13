// =====================================================================
// BP2 test 28 — the sensors and the spare channels (Rev Q).
//
// Every 5 s it reads all eight mux channels on I2C0 (25 kHz): the SHT40's serial and a
// measurement, each with its CRC checked, and says which of the six known probes it is (label
// and position, from the firmware's sensor table). Channels 0-5 are J3-J8; 6 and 7 are the
// spare connectors J17 and J18. One short I2C step per loop pass; the SHT40's 10 ms measuring
// time is a wait in a state, not a delay. Switches nothing.
// =====================================================================
#include <Arduino.h>
#include <Wire.h>
#include "driver/gpio.h"
#include "bp2/common/bp2_ui.h"
#include "wineador/decode.h"
#include "wineador/sensor_table.h"
#include "wineador/sensor_health.h"
#include "pins.h"

namespace {

const char* const kBanner[] = {
  "1. All six sensors on J3-J8 (channels 0-5, by their roles): every serial on its own channel, CRC good.",
  "2. Move one sensor to J17: it reads on channel 6 (its old channel now empty).",
  "3. Move it on to J18: channel 7. Then back to its own connector.",
  "Switches nothing. Record: which serial on which channel, and any CRC failure.",
  nullptr,
};

constexpr int kChannels = 8;
enum class S : uint8_t { Idle, Select, SerialCmd, SerialWait, SerialRead, MeasCmd, MeasWait, MeasRead, Report };
S gS = S::Idle;
int gCh = 0;
uint32_t gT = 0, gLastScanMs = 0;
struct Result { bool present = false; bool serialOk = false; bool measOk = false; uint32_t serial = 0; float t = 0, rh = 0; };
Result gR[kChannels];

void pullupsOff() {
  gpio_pullup_dis((gpio_num_t)pins::kI2cSda);
  gpio_pullup_dis((gpio_num_t)pins::kI2cScl);
}

bool mux(uint8_t mask) {
  Wire.beginTransmission(i2caddr::kMux);
  Wire.write(mask);
  return Wire.endTransmission() == 0;
}

bool cmd(uint8_t c) {
  Wire.beginTransmission(i2caddr::kSht40);
  Wire.write(c);
  return Wire.endTransmission() == 0;
}

bool read6(uint8_t b[6]) {
  if (Wire.requestFrom(i2caddr::kSht40, (size_t)6) != 6) { while (Wire.available()) Wire.read(); return false; }
  for (int i = 0; i < 6; i++) b[i] = (uint8_t)Wire.read();
  return true;
}

void report() {
  Serial.printf("\n  uptime %lu s - mux channels (J3-J8 = 0-5, J17 = 6, J18 = 7):\n", (unsigned long)(millis() / 1000));
  for (int c = 0; c < kChannels; c++) {
    const Result& r = gR[c];
    if (!r.present) { Serial.printf("    ch%d  %-4s  -- nothing answers\n", c, c < 6 ? "" : "spare"); continue; }
    const int k = r.serialOk ? wineador::sensorIndexForSerial(r.serial) : -1;
    char who[48];
    if (k >= 0) snprintf(who, sizeof(who), "probe %s %s%s", wineador::kSensors[k].label, wineador::kSensors[k].position,
                         k == c ? "" : "  <-- not its own channel");
    else snprintf(who, sizeof(who), "%s", r.serialOk ? "UNKNOWN serial" : "serial CRC FAIL");
    if (r.measOk)
      Serial.printf("    ch%d  serial 0x%08lX  %s | %.2f C, RH raw %.1f%% (CRC good)\n", c, (unsigned long)r.serial, who,
                    (double)r.t, (double)r.rh);
    else
      Serial.printf("    ch%d  serial 0x%08lX  %s | measurement FAILED (CRC or no answer)\n", c, (unsigned long)r.serial, who);
  }
}

void step(uint32_t now) {
  uint8_t b[6];
  switch (gS) {
    case S::Idle:
      if (now - gLastScanMs >= 5000) { gLastScanMs = now; gCh = 0; gS = S::Select; }
      break;
    case S::Select:
      if (gCh >= kChannels) { mux(0); gS = S::Report; break; }
      gR[gCh] = Result{};
      gS = mux((uint8_t)(1u << gCh)) ? S::SerialCmd : S::Report;   // no mux: report what we have
      break;
    case S::SerialCmd:
      if (!cmd(0x89)) { gCh++; gS = S::Select; break; }          // nothing on this channel
      gR[gCh].present = true;
      gT = now;
      gS = S::SerialWait;
      break;
    case S::SerialWait:
      if (now - gT >= 10) gS = S::SerialRead;
      break;
    case S::SerialRead:
      gR[gCh].serialOk = read6(b) && wineador::decodeSht40Serial(b, gR[gCh].serial) == wineador::BusResult::Ok;
      gS = S::MeasCmd;
      break;
    case S::MeasCmd:
      if (!cmd(0xFD)) { gCh++; gS = S::Select; break; }
      gT = now;
      gS = S::MeasWait;
      break;
    case S::MeasWait:
      if (now - gT >= 10) gS = S::MeasRead;
      break;
    case S::MeasRead:
      gR[gCh].measOk = read6(b) && wineador::decodeSht40Measurement(b, gR[gCh].t, gR[gCh].rh) == wineador::BusResult::Ok;
      gCh++;
      gS = S::Select;
      break;
    case S::Report:
      report();
      gS = S::Idle;
      break;
  }
  pullupsOff();                                  // after every bus step (5.3)
}

}  // namespace

void setup() {
  bp2::begin("BP2 TEST 28: SENSORS AND SPARE CHANNELS", kBanner);
  pinMode(pins::kMuxReset, OUTPUT);
  digitalWrite(pins::kMuxReset, LOW);            // the mux keeps its selection across a reset: pulse RST (5.3)
  const uint32_t t = micros();
  while (micros() - t < 50) {}                   // 50 us (the datasheet needs nanoseconds)
  digitalWrite(pins::kMuxReset, HIGH);
  Wire.begin(pins::kI2cSda, pins::kI2cScl);
  Wire.setClock(25000);
  Wire.setTimeOut(20);
  pullupsOff();
  gLastScanMs = millis() - 5000;                 // the first scan at once
}

void loop() { step(millis()); }
