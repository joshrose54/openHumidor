#include "reset_record.h"

#if defined(WINEADOR_TEST_BUILD)

#include <Arduino.h>
#include <Preferences.h>
#include "esp_attr.h"
#include "wineador/reset_log.h"
#include "app/console.h"

using namespace wineador;

namespace resetrec {

// RTC memory, not touched by the startup code: it keeps its contents across a
// software, watchdog or brownout reset. After a power cut it holds garbage, so
// it is trusted only if the magic number and the check value both match.
struct Heartbeat {
  uint32_t magic;
  uint32_t aliveS;
  uint32_t loads;
  uint32_t check;     // magic ^ aliveS ^ loads ^ 0xA5A5A5A5
};
static RTC_NOINIT_ATTR Heartbeat rtcBeat;
static constexpr uint32_t kBeatMagic = 0x48425431;   // "HBT1"

static uint32_t checkOf(const Heartbeat& h) { return h.magic ^ h.aliveS ^ h.loads ^ 0xA5A5A5A5u; }

static ResetLog gLog;
static uint32_t gLastBeatS = 0xFFFFFFFFu;
static uint8_t gLastLoads = 0xFF;

static void save() {
  Preferences p;
  if (p.begin("wineador", false)) {
    p.putBytes("resetlog", &gLog, sizeof(gLog));
    p.end();
  }
}

void begin(int reason) {
  // 1. What the previous boot was doing when last seen alive
  ResetEntry x;
  x.reason = (uint8_t)reason;
  if (rtcBeat.magic == kBeatMagic && rtcBeat.check == checkOf(rtcBeat)) {
    x.prevKnown = 1;
    x.prevAliveS = rtcBeat.aliveS;
    x.prevLoads = (uint8_t)rtcBeat.loads;
  }
  // 2. Start this boot's heartbeat
  rtcBeat.magic = kBeatMagic;
  rtcBeat.aliveS = 0;
  rtcBeat.loads = 0;
  rtcBeat.check = checkOf(rtcBeat);

  // 3. Read the list from flash, add this boot, write it back (boot only)
  Preferences p;
  if (p.begin("wineador", true)) {
    if (p.getBytesLength("resetlog") == sizeof(gLog)) p.getBytes("resetlog", &gLog, sizeof(gLog));
    p.end();
  }
  if (!gLog.valid()) gLog.clear();
  gLog.add(x);
  save();
}

void heartbeat(uint32_t uptimeS, const LoadOutputs& live) {
  const uint8_t m = loadMask(live);
  if (uptimeS == gLastBeatS && m == gLastLoads) return;
  gLastBeatS = uptimeS;
  gLastLoads = m;
  rtcBeat.aliveS = uptimeS;
  rtcBeat.loads = m;
  rtcBeat.check = checkOf(rtcBeat);
}

void print() {
  Serial.printf("\n  RESET RECORD (test build): %lu boots since the last 'resets clear', last %u shown, oldest first\n",
                (unsigned long)gLog.boots, (unsigned)gLog.count);
  char line[160];
  for (int i = 0; i < gLog.count; i++) {
    const ResetEntry& x = gLog.at(i);
    formatResetEntry(line, sizeof(line), x, console::resetReasonName(x.reason));
    Serial.printf("    %s\n", line);
  }
  Serial.println("  (heartbeat = uptime and loads once a second, kept in RTC memory; lost on a power cut)");
}

void clear() {
  gLog.clear();
  save();
  Serial.println("  Reset record cleared.");
}

}  // namespace resetrec

#else   // cabinet build: nothing

namespace resetrec {
void begin(int) {}
void heartbeat(uint32_t, const wineador::LoadOutputs&) {}
void print() {}
void clear() {}
}  // namespace resetrec

#endif
