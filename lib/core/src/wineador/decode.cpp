#include "wineador/decode.h"
#include <stdio.h>

namespace wineador {

uint8_t sht40Crc(const uint8_t* d, int n) {
  uint8_t c = 0xFF;
  for (int i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
  }
  return c;
}

BusResult decodeSht40Measurement(const uint8_t b[6], float& tempC, float& rhRawPct) {
  if (sht40Crc(b, 2) != b[2] || sht40Crc(b + 3, 2) != b[5]) return BusResult::CrcFail;
  uint16_t tt = (uint16_t)((b[0] << 8) | b[1]);
  uint16_t ht = (uint16_t)((b[3] << 8) | b[4]);
  tempC    = -45.0f + 175.0f * tt / 65535.0f;
  rhRawPct = -6.0f + 125.0f * ht / 65535.0f;   // may be outside 0-100: that is checked later
  return BusResult::Ok;
}

BusResult decodeSht40Serial(const uint8_t b[6], uint32_t& serial) {
  if (sht40Crc(b, 2) != b[2] || sht40Crc(b + 3, 2) != b[5]) return BusResult::CrcFail;
  serial = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[3] << 8) | b[4];
  return BusResult::Ok;
}

namespace {
uint8_t bcd(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
}

bool decodeDs3231(const uint8_t r[7], DateTime& dt) {
  uint8_t s  = bcd(r[0] & 0x7F);
  uint8_t mi = bcd(r[1] & 0x7F);
  uint8_t h  = bcd(r[2] & 0x3F);   // 24-hour mode
  uint8_t d  = bcd(r[4] & 0x3F);
  uint8_t mo = bcd(r[5] & 0x1F);
  uint8_t y  = bcd(r[6]);
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 59 || y > 99) return false;
  dt.year = (uint16_t)(2000 + y);
  dt.month = mo; dt.day = d; dt.hour = h; dt.minute = mi; dt.second = s;
  return true;
}

bool formatDs3231Time(const uint8_t r[7], char* out, size_t n) {
  DateTime dt;
  if (!decodeDs3231(r, dt)) return false;
  snprintf(out, n, "%04u-%02u-%02uT%02u:%02u:%02uZ", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
  return true;
}

int64_t toUnixSeconds(const DateTime& dt) {
  // Days from 1970-01-01 to the date (Howard Hinnant's days_from_civil).
  int y = dt.year - (dt.month <= 2 ? 1 : 0);
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned m = dt.month;
  unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + dt.day - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
  return days * 86400 + dt.hour * 3600 + dt.minute * 60 + dt.second;
}

}  // namespace wineador
