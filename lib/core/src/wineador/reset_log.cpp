#include "wineador/reset_log.h"
#include "wineador/status.h"
#include <stdio.h>

namespace wineador {

uint8_t loadMask(const LoadOutputs& l) {
  return (l.cool ? kLoadCool : 0) | (l.heat ? kLoadHeat : 0) |
         (l.relayHeating ? kLoadRelayHeating : 0) | (l.humid ? kLoadHumid : 0);
}

void ResetLog::add(ResetEntry x) {
  if (!valid()) clear();
  boots++;
  x.bootNo = boots;
  if (count < kMax) {
    e[(head + count) % kMax] = x;
    count++;
  } else {
    e[head] = x;                    // overwrite the oldest
    head = (head + 1) % kMax;
  }
}

int formatResetEntry(char* out, size_t n, const ResetEntry& x, const char* reasonName) {
  int len = snprintf(out, n, "boot %lu  %-13s ", (unsigned long)x.bootNo, reasonName);
  if (len < 0 || (size_t)len >= n) return len;
  if (!x.prevKnown) {
    len += snprintf(out + len, n - len, "previous boot: heartbeat lost");
    return len;
  }
  char t[24];
  formatDuration(t, sizeof(t), (uint64_t)x.prevAliveS * 1000ULL);
  len += snprintf(out + len, n - len, "previous boot last alive at %s, live:", t);
  if (x.prevLoads == 0) len += snprintf(out + len, n - len, " nothing");
  if (x.prevLoads & kLoadCool)  len += snprintf(out + len, n - len, " cool");
  if (x.prevLoads & kLoadHeat)  len += snprintf(out + len, n - len, " heat");
  if (x.prevLoads & kLoadHumid) len += snprintf(out + len, n - len, " humid");
  len += snprintf(out + len, n - len, " K1-%s", (x.prevLoads & kLoadRelayHeating) ? "heat" : "cool");
  return len;
}

}  // namespace wineador
