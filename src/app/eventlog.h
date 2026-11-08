#pragma once
// The one path for every event (spec 5.4): log("type", ...) prints the line to
// serial, keeps it in RAM for the 'events' command (the last 24), and queues the
// CSV row for the SD card (src/drivers/sd_log). Never waits.
//
// Types: compressor · heater · humidifier · interlock · harvest · guard · crc ·
// timeout · sensor · fan · settings · boot · sd · i2c · rtc · ntp · crossover ·
// strip · humidify · exercise · ineffective · scenario (test builds) · (disturbance: off until B6)

#include <stdint.h>
#include <stddef.h>

namespace eventlog {

struct Entry {
  char timestamp[24];
  bool trusted;
  uint64_t uptimeMs;
  char type[12];
  char detail[200];
};

// Every pass (cheap): whether the clock has been set at all, and whether its
// timestamps are trusted (5.4, 5.6).
void setClock(bool set, bool trusted);

void log(const char* type, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
// The same, without printing to serial (for events that print their own fuller text).
void logQuiet(const char* type, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

int count();                       // kept in RAM, at most 24
const Entry& recent(int i);        // 0 = oldest kept
uint32_t total();                  // since boot

// ISO 8601 UTC from the system clock (whatever it holds), e.g. 2026-11-08T16:00:00Z.
void isoNow(char* out, size_t n);
bool clockTrusted();

}  // namespace eventlog
