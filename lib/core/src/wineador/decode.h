#pragma once
// Turning raw bytes from the bus into values. Plain logic, so the native tests
// can check it against the datasheets. The I2C transactions themselves are in
// src/drivers/i2c_bus.cpp.

#include "wineador/types.h"

namespace wineador {

// SHT40 CRC-8: polynomial 0x31, init 0xFF (datasheet: 0xBEEF -> 0x92).
uint8_t sht40Crc(const uint8_t* data, int len);

// Six bytes from a 0xFD measurement: T(2) CRC T-RH(2) CRC.
// Returns Ok or CrcFail. RH is RAW — not clamped, not calibrated (spec 4.3).
BusResult decodeSht40Measurement(const uint8_t b[6], float& tempC, float& rhRawPct);

// Six bytes from a 0x89 serial read. Returns Ok or CrcFail.
BusResult decodeSht40Serial(const uint8_t b[6], uint32_t& serial);

struct DateTime {
  uint16_t year = 2000;
  uint8_t month = 1, day = 1, hour = 0, minute = 0, second = 0;
};

// Seven bytes from DS3231 registers 0x00-0x06. Returns false if any field is out of range.
bool decodeDs3231(const uint8_t r[7], DateTime& dt);

// Same, written as "2026-11-07T13:00:15Z".
bool formatDs3231Time(const uint8_t r[7], char* out, size_t n);

// Seconds since 1970-01-01 UTC, for setting the ESP32's system clock (spec 5.4).
int64_t toUnixSeconds(const DateTime& dt);

}  // namespace wineador
