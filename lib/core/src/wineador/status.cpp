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
  o.add("%s%s up %s | %s", s.timestamp, s.timeTrusted ? "" : " UNTRUSTED", formatDuration(up, sizeof(up), s.uptimeMs),
        s.profile == Profile::Bench ? "BENCH-PROFILE" : "REAL");
  if (s.testBuild) o.add(" TEST-BUILD");
  if (s.bootDemand) o.add(" BOOT-DEMAND");
  o.add(" | boot %s | ", s.bootReason);
  if (s.holdActive) o.add("hold %s left", formatDuration(t, sizeof(t), s.holdRemainingMs));
  else              o.add("hold ended at %s", formatDuration(t, sizeof(t), s.holdLengthMs));
  o.add(" | relay %s cool %s heat %s humid %s", s.loads.relayHeating ? "HEAT" : "COOL",
        onOff(s.loads.cool), onOff(s.loads.heat), onOff(s.loads.humid));
  if (s.guardText && s.guardText[0]) o.add(" | %s", s.guardText);
  if (s.controlText && s.controlText[0]) o.add(" | %s", s.controlText);
  if (s.humidText && s.humidText[0]) o.add(" | %s", s.humidText);
  o.add(" | fan %u%% %lurpm", (unsigned)s.fanDutyPct, (unsigned long)s.fanRpm);
  if (s.fanPwmReadback >= 0) {
    // What the PWM hardware is really set to (0-255; 20% = 51), and edges per real pulse.
    o.add(" pwm-hw %ld/255", (long)s.fanPwmReadback);
    if (s.fanPulses) o.add(" e/p %lu.%lu", (unsigned long)(s.fanEdges / s.fanPulses),
                           (unsigned long)((s.fanEdges * 10 / s.fanPulses) % 10));
    else o.add(" e/p -- (%lu edges)", (unsigned long)s.fanEdges);
  }
  o.add(" | %s", s.testBuild ? "scripted" : "real");
  if (s.cab.cabinetOk > 0)
    o.add(" cab %.2fC %.1f%% (%d/4, %.2f-%.2f)", s.cab.meanC, s.cab.meanRhPct, s.cab.cabinetOk, s.cab.minC, s.cab.maxC);
  else
    o.add(" cab -- (0/4)");
  if (s.cab.chamberOk) o.add(" ch4 %.2fC", s.cab.chamberC); else o.add(" ch4 --");
  if (s.cab.ambientOk) o.add(" ch5 %.2fC %.1f%%", s.cab.ambientC, s.cab.ambientRhPct); else o.add(" ch5 --");
  o.add(" | sensors %d/6", s.cab.sensorsOk);
  if (s.healthText && s.healthText[0]) o.add(" %s", s.healthText);
  o.add(" | loop max %lu.%lums", (unsigned long)(s.loopMaxUs / 1000), (unsigned long)((s.loopMaxUs % 1000) / 100));
  o.add(" | heap %lu min %lu", (unsigned long)s.heapFree, (unsigned long)s.heapMin);
  if (!s.rtcOk) o.add(" | RTC FAIL");
  if (s.rtcLostTime) o.add(" | RTC LOST TIME");
  if (s.netText && s.netText[0]) o.add(" | %s", s.netText);
  if (s.boardText && s.boardText[0]) o.add(" | %s", s.boardText);
  if (s.sdText && s.sdText[0]) o.add(" | %s", s.sdText);
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
