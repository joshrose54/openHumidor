#include "wineador/uk_time.h"
#include <stdio.h>

namespace wineador {

// Howard Hinnant's days-from-civil algorithm.
int64_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

void civilFromDays(int64_t z, int* year, int* month, int* day) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = yoe + era * 400;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  const int64_t d = doy - (153 * mp + 2) / 5 + 1;
  const int64_t m = mp + (mp < 10 ? 3 : -9);
  *year = (int)(y + (m <= 2));
  *month = (int)m;
  *day = (int)d;
}

int weekdayFromDays(int64_t z) { return (int)(((z % 7) + 7 + 4) % 7); }   // 1970-01-01 was a Thursday (4)

static int64_t lastSundayDays(int year, int month, int daysInMonth) {
  const int64_t last = daysFromCivil(year, month, daysInMonth);
  return last - weekdayFromDays(last);
}

int64_t ukSummerStartUtc(int year) { return lastSundayDays(year, 3, 31) * 86400 + 3600; }
int64_t ukSummerEndUtc(int year) { return lastSundayDays(year, 10, 31) * 86400 + 3600; }

static int64_t floorDiv(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

bool ukIsSummer(int64_t utcS) {
  int y, m, d;
  civilFromDays(floorDiv(utcS, 86400), &y, &m, &d);
  return utcS >= ukSummerStartUtc(y) && utcS < ukSummerEndUtc(y);
}

LocalTime ukLocal(int64_t utcS) {
  LocalTime t;
  t.summer = ukIsSummer(utcS);
  const int64_t local = utcS + (t.summer ? 3600 : 0);
  const int64_t days = floorDiv(local, 86400);
  const int64_t secs = local - days * 86400;
  civilFromDays(days, &t.year, &t.month, &t.day);
  t.hour = (int)(secs / 3600);
  t.minute = (int)(secs / 60 % 60);
  t.second = (int)(secs % 60);
  return t;
}

const char* formatHhMm(char* out, size_t n, const LocalTime& t) {
  snprintf(out, n, "%02d:%02d", t.hour, t.minute);
  return out;
}

}  // namespace wineador
