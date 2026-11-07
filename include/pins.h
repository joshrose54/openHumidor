#pragma once
// ALL pins, in one place — spec 3.1. Board Rev N (same pins as Rev P).
// For the next board (ESP32-S3) this file and the drivers change; lib/core does not.

#include <stdint.h>

namespace pins {

// I2C: mux (0x70) and RTC (0x68) on the main bus
constexpr int kI2cSda = 21;
constexpr int kI2cScl = 22;
constexpr int kMuxReset = 16;     // TCA9548A RST, active low; module pull-up holds it high at boot

// Plenum fan
constexpr int kFanPwm = 25;       // 25 kHz. Undriven = full speed (safe), so untouched until ledcAttach
constexpr int kFanTach = 34;      // input only; external 1k pull-up and 1uF; pinMode INPUT mandatory

// SD card, VSPI
constexpr int kSdCs = 17;
constexpr int kSdSck = 18;
constexpr int kSdMiso = 19;
constexpr int kSdMosi = 23;

// The four load signals — each through U9 (74HCT08) to an IRLZ44N, 10k pull-down on the pin
constexpr int kCoolingSsr = 26;   // Q2, SSR -A2. Live only when K1 de-energised
constexpr int kHeater = 27;       // Q3, heater -. Live only when K1 energised
constexpr int kRelayK1 = 32;      // Q5, K1 coil. HIGH = energised = heating side
constexpr int kHumidifier = 33;   // Q4, humidifier fan -. Software guards only

}  // namespace pins

namespace i2caddr {
constexpr uint8_t kMux = 0x70;
constexpr uint8_t kSht40 = 0x44;
constexpr uint8_t kRtc = 0x68;
}  // namespace i2caddr
