#include "wineador/status.h"
#include "wineador/build_info.h"
#include <stdarg.h>
#include <stdio.h>

namespace wineador {

namespace {
// Appends formatted text to a fixed buffer, never overrunning it.
struct Out {
  char* p;
  size_t n;
  size_t len = 0;
  Out(char* p_, size_t n_) : p(p_), n(n_) { if (n) p[0] = 0; }
  void add(const char* fmt, ...) {
    if (len + 1 >= n) return;
    va_list a;
    va_start(a, fmt);
    int w = vsnprintf(p + len, n - len, fmt, a);
    va_end(a);
    if (w > 0) len = (len + w < n) ? len + w : n - 1;
  }
};
const char* onOff(bool b) { return b ? "ON" : "off"; }
}  // namespace

const char* formatDuration(char* out, size_t n, uint64_t ms) {
  uint64_t s = ms / 1000;
  snprintf(out, n, "%lu:%02u:%02u", (unsigned long)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));
  return out;
}

int formatStatusLine(char* buf, size_t n, const StatusInfo& s) {
  Out o(buf, n);
  char up[24], t[24];
  o.add("%s up %s | %s", s.timestamp, formatDuration(up, sizeof(up), s.uptimeMs),
        s.profile == Profile::Bench ? "BENCH-PROFILE" : "REAL");
  if (s.testBuild) o.add(" TEST-BUILD");
  o.add(" | boot %s | ", s.bootReason);
  if (s.holdActive) o.add("hold %s left", formatDuration(t, sizeof(t), s.holdRemainingMs));
  else              o.add("hold ended at %s", formatDuration(t, sizeof(t), s.holdLengthMs));
  o.add(" | relay %s cool %s heat %s humid %s", s.loads.relayHeating ? "HEAT" : "COOL",
        onOff(s.loads.cool), onOff(s.loads.heat), onOff(s.loads.humid));
  o.add(" | fan %u%% %lurpm", (unsigned)s.fanDutyPct, (unsigned long)s.fanRpm);
  o.add(" | %s", s.testBuild ? "scripted" : "raw");
  if (s.cab.cabinetOk > 0)
    o.add(" cab %.2fC %.1f%% (%d/4, %.2f-%.2f)", s.cab.meanC, s.cab.meanRhPct, s.cab.cabinetOk, s.cab.minC, s.cab.maxC);
  else
    o.add(" cab -- (0/4)");
  if (s.cab.chamberOk) o.add(" ch6 %.2fC", s.cab.chamberC); else o.add(" ch6 --");
  if (s.cab.ambientOk) o.add(" ch7 %.2fC %.1f%%", s.cab.ambientC, s.cab.ambientRhPct); else o.add(" ch7 --");
  o.add(" | sensors %d/6", s.cab.sensorsOk);
  o.add(" | loop max %lu.%lums", (unsigned long)(s.loopMaxUs / 1000), (unsigned long)((s.loopMaxUs % 1000) / 100));
  o.add(" | heap %lu", (unsigned long)s.heapFree);
  if (!s.rtcOk) o.add(" | RTC FAIL");
  if (s.i2cPullupsOn) o.add(" | I2C PULLUPS ON");
  if (s.readingsAgeMs > s.staleAfterMs) o.add(" | readings STALE %lus", (unsigned long)(s.readingsAgeMs / 1000));
  return (int)o.len;
}

int formatLiveBlock(char* buf, size_t n, uint64_t uptimeMs, const char* why,
                    const LoadOutputs& l, uint8_t fanDutyPct) {
  Out o(buf, n);
  char up[24];
  o.add("\n#################################################\n");
  o.add("  NOW LIVE  (up %s)  %s\n", formatDuration(up, sizeof(up), uptimeMs), why);
  o.add("#################################################\n");
  o.add("  RELAY K1 ........ %s\n", l.relayHeating ? "ON  (heating side)" : "OFF (cooling side)");
  o.add("  COOLING (SSR) ... %s\n", l.cool ? "ON" : "OFF");
  o.add("  HEATER .......... %s\n", l.heat ? "ON" : "OFF");
  o.add("  HUMID FAN ....... %s\n", l.humid ? "ON" : "OFF");
  if (fanDutyPct == 0) o.add("  PLENUM FAN ...... STOPPED\n");
  else                 o.add("  PLENUM FAN ...... %u%%\n", (unsigned)fanDutyPct);
  o.add("#################################################");
  return (int)o.len;
}

}  // namespace wineador
