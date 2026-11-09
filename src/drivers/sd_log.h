#pragma once
// SD logging, the writing half (spec 5.2, 5.4). The control loop NEVER touches
// the card: it hands finished CSV lines to this module's queue and carries on.
// A separate low-priority task (core 0) does every SD operation:
//   - readings rows are kept in RAM and written 16 at a time (wear, 5.4);
//   - events and hourly rows are written at once;
//   - a new file gets its header first; folders are made as needed;
//   - at start, once the clock is known, the retention check (logbook.h);
//   - a failed write is retried once after a remount; a recovered retry is counted
//     and logged ("SD write error, retried ... OK");
//   - a second failure (card missing, write error) marks the card FAILED; while it
//     is out, events (up to 100), readings rows (up to 60) and hourly rows (up to
//     24) are kept in RAM (sizes per board, include/board.h), the oldest dropped and
//     counted beyond that; the task tries to remount every 30 s and, when the card
//     is back, writes everything kept, in order.
// Card writes can stall for hundreds of ms as a card fills (build log); here that
// only delays this task. stalledMs() lets the loop flag an operation that hangs.

#include <stdint.h>
#include <stddef.h>

namespace sdlog {

// The SD stress test (test builds, spec B4 note): 16-row batches back to back.
struct StressStats {
  bool active = false, done = false;
  uint32_t khz = 0;
  bool retry = false;
  uint32_t target = 0, writes = 0, failures = 0, retries = 0, worstMs = 0;
  uint32_t atFolder = 0, atOpen = 0, atHeader = 0, atData = 0;   // step the first attempt failed at
};

struct Stats {
  bool mounted = false;         // the card is mounted and the last operation worked
  bool failed = false;          // the last operation failed; remounting every 30 s
  uint32_t failures = 0;        // times it has failed since boot (a write failed twice)
  uint32_t retries = 0;         // writes that failed once and worked on the retry after a remount
  char lastRetry[64] = {};      // which file, and at which step the first attempt failed
  uint32_t spiKhz = 0;          // the card's SPI clock in normal running
  uint32_t rowsBuffered = 0;    // readings rows in RAM, not yet written (the batch, or kept while out)
  uint32_t rowsBufferedMax = 0, eventsPendingMax = 0, hourlyPendingMax = 0;   // the most kept at once
  uint32_t hourlyPending = 0, hourlyDropped = 0;
  StressStats stress;
  uint32_t rowsWritten = 0, rowsDropped = 0;
  uint32_t eventsWritten = 0, eventsPending = 0, eventsDropped = 0;
  uint32_t hourlyWritten = 0;
  uint32_t flushes = 0;         // readings batches written
  uint32_t lastFlushRows = 0;
  uint64_t lastFlushUptimeMs = 0;
  uint32_t lastWriteMs = 0;     // how long the last write took
  uint32_t worstWriteMs = 0;    // worst since boot
  uint64_t totalBytes = 0, freeBytes = 0;   // refreshed at start and hourly
  int retentionDeleted = -1;    // files deleted by the boot check; -1 = not run yet
  char lastError[64] = {};
  char readingsPath[40] = {};   // the file rows go to now
};

// Boot: after sdcard::begin(). Headers are copied. Starts the task; nothing is
// written until start().
void begin(const char* readingsHeader, const char* eventsHeader, const char* hourlyHeader);
// Once the clock is set (or given up on): start writing; the retention check
// runs first, for this UTC year and month.
void start(int year, int month);
bool started();

// Queue a finished line (newline included). Never waits: if the queue is full the
// line is dropped and counted. A readings/events line must begin with its ISO
// timestamp — the file's month comes from it.
void reading(const char* line);
void event(const char* line);
void hourly(const char* line);

Stats stats();                  // a copy
uint32_t takeWorstMs();         // worst write since the last call (for the readings row)
bool writing();                 // a card operation is under way (don't sample heap now)
uint32_t stalledMs(uint32_t nowMs);   // how long the current operation has run (0 if none)

// TEST BUILDS: the SD stress test. False if one is already running.
bool stressStart(uint32_t khz, bool retry, uint32_t writes);
void stressStop();

}  // namespace sdlog
