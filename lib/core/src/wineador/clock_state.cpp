#include "wineador/clock_state.h"
#include "wineador/status.h"
#include <stdio.h>
#include <stdarg.h>

namespace wineador {

namespace {
struct Out {
  char* p; size_t n; size_t len = 0;
  Out(char* p_, size_t n_) : p(p_), n(n_) { if (n) p[0] = 0; }
  void add(const char* fmt, ...) {
    if (len + 1 >= n) return;
    va_list a; va_start(a, fmt);
    int w = vsnprintf(p + len, n - len, fmt, a);
    va_end(a);
    if (w > 0) len = (len + w < n) ? len + w : n - 1;
  }
};

// "-841234567 s (about 26.7 years)" for big errors; "+3 s" for small ones.
void addError(Out& o, int64_t e) {
  const int64_t a = e < 0 ? -e : e;
  o.add("%+lld s", (long long)e);
  if (a >= 365LL * 86400) o.add(" (about %.1f years)", (double)a / (365.25 * 86400.0));
  else if (a >= 86400) o.add(" (about %.1f days)", (double)a / 86400.0);
}
}  // namespace

int formatClockSummary(char* out, size_t n, const ClockState& c) {
  Out o(out, n);
  char t[24];
  if (!c.osfKnown)       o.add("OSF not read this boot");
  else if (c.osfAtBoot)  o.add("OSF SET at this boot (the DS3231 lost time)");
  else                   o.add("OSF clear at this boot");

  if (c.syncs == 0) {
    o.add(" | NTP not synced yet");
  } else {
    o.add(" | NTP first sync at %s, %lu sync%s", formatDuration(t, sizeof(t), c.firstSyncAtMs),
          (unsigned long)c.syncs, c.syncs == 1 ? "" : "s");
    if (c.corrected) {
      o.add(" | DS3231 ");
      if (c.rtcWritten) {
        o.add("was ");
        addError(o, c.lastCorrectionS);
        o.add(" out; written at %s%s", formatDuration(t, sizeof(t), c.correctedAtMs),
              c.osfAtBoot ? ", OSF cleared" : "");
      } else {
        o.add("within 2 s (");
        addError(o, c.lastCorrectionS);
        o.add("), not written");
      }
    } else {
      o.add(" | DS3231 not yet checked against NTP");
    }
  }
  o.add(" | timestamps %s", c.trusted() ? "trusted" : "UNTRUSTED");
  return (int)o.len;
}

}  // namespace wineador
