#pragma once
// UK local time for the LCD (spec 5.5): Europe/London, summer time included, 24-hour. Plain
// arithmetic, not the chip's localtime(), so the Mac tests prove the changeovers. Logs stay UTC.
//
// British Summer Time (UTC+1) runs from 01:00 UTC on the last Sunday in March to 01:00 UTC on
// the last Sunday in October; the rest of the year is GMT (UTC+0). At the October change the
// local hour 01:00-01:59 happens twice; local time is only ever shown, never stored, so that
// is harmless.

#include <stdint.h>
#include <stddef.h>

namespace wineador {

struct LocalTime {
  int year = 1970, month = 1, day = 1;   // month 1-12, day 1-31
  int hour = 0, minute = 0, second = 0;
  bool summer = false;                   // BST
};

// Days since 1970-01-01 for a civil date, and back (proleptic Gregorian).
int64_t daysFromCivil(int year, int month, int day);
void civilFromDays(int64_t days, int* year, int* month, int* day);

// 0 = Sunday ... 6 = Saturday.
int weekdayFromDays(int64_t days);

// UTC seconds when summer time starts / ends in `year`.
int64_t ukSummerStartUtc(int year);
int64_t ukSummerEndUtc(int year);

bool ukIsSummer(int64_t utcS);
LocalTime ukLocal(int64_t utcS);

// "14:32". Returns out.
const char* formatHhMm(char* out, size_t n, const LocalTime& t);

}  // namespace wineador
