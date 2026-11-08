#include "wineador/guard_audit.h"
#include <stdarg.h>
#include <stdio.h>

namespace wineador {

namespace {
const char* name(int L) { return L == 0 ? "compressor" : L == 1 ? "heater" : "humidifier"; }
bool liveOf(const LoadOutputs& o, int L) { return L == 0 ? o.cool : L == 1 ? o.heat : o.humid; }
}  // namespace

void GuardAudit::begin(const Settings& s, uint32_t nowMs) {
  (void)nowMs;
  s_ = s;
  prev_ = LoadOutputs{};
  for (int L = 0; L < 3; L++) { known_[L] = false; starts_[L] = maxRunStops_[L] = 0; }
  breached_ = false;
  text_[0] = 0;
}

void GuardAudit::fail(const char* fmt, ...) {
  if (breached_) return;
  breached_ = true;
  va_list a;
  va_start(a, fmt);
  vsnprintf(text_, sizeof(text_), fmt, a);
  va_end(a);
}

void GuardAudit::update(uint32_t now, const LoadOutputs& o) {
  const SafetySettings& f = s_.safety;
  const uint32_t minOff[3] = {f.compressorMinOffMs, f.heaterMinOffMs, f.humidMinOffMs};
  const uint32_t minOn[3] = {f.compressorMinOnMs, f.heaterMinOnMs, 0};
  const uint32_t maxRun[3] = {f.compressorMaxRunMs, f.heaterMaxRunMs, f.humidMaxRunMs};

  if (o.cool && o.heat) fail("compressor and heater live together");
  if (o.cool && o.relayHeating) fail("compressor live with K1 on the heating side");
  if (o.heat && !o.relayHeating) fail("heater live with K1 on the cooling side");

  for (int L = 0; L < 3; L++) {
    const bool was = liveOf(prev_, L), is = liveOf(o, L);
    if (is && !was) {
      starts_[L]++;
      if (known_[L] && elapsedMs(now, offAt_[L]) + kSlackMs < minOff[L])
        fail("%s started %lu ms after stopping (min off %lu ms)", name(L), (unsigned long)elapsedMs(now, offAt_[L]),
             (unsigned long)minOff[L]);
      // crossover: the other temperature load's last stop
      if (L < 2) {
        const int other = 1 - L;
        if (known_[other] && elapsedMs(now, offAt_[other]) + kSlackMs < s_.tuning.crossoverDelayMs)
          fail("%s started %lu ms after the %s stopped (crossover %lu ms)", name(L),
               (unsigned long)elapsedMs(now, offAt_[other]), name(other), (unsigned long)s_.tuning.crossoverDelayMs);
      }
      onAt_[L] = now;
    }
    if (is && elapsedMs(now, onAt_[L]) > maxRun[L] + kSlackMs)
      fail("%s ran %lu ms (max run %lu ms)", name(L), (unsigned long)elapsedMs(now, onAt_[L]), (unsigned long)maxRun[L]);
    if (!is && was) {
      const uint32_t ran = elapsedMs(now, onAt_[L]);
      if (ran + kSlackMs < minOn[L]) fail("%s stopped after %lu ms (min on %lu ms)", name(L), (unsigned long)ran, (unsigned long)minOn[L]);
      if (ran + kSlackMs >= maxRun[L]) maxRunStops_[L]++;
      offAt_[L] = now;
      known_[L] = true;
    }
  }
  prev_ = o;
}

}  // namespace wineador
