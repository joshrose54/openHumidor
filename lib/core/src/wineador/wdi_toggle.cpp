#include "wineador/wdi_toggle.h"

namespace wineador {

void WdiToggle::start(uint32_t nowMs) {
  if (started_) return;                          // once only: a pause is undone by resume(), never by start()
  started_ = true;
  running_ = true;
  level_ = true;
  lastMs_ = nowMs;
  changes_ = changes_ + 1;
}

bool WdiToggle::pass(uint32_t nowMs) {
  if (!running_) return level_;
  const uint32_t gap = nowMs - lastMs_;           // unsigned: right across the millis() wrap
  if (gap > longest_) longest_ = gap;
  if (gap > kMaxGapMs) { longGap_ = true; longGapMs_ = gap; }
  level_ = !level_;
  lastMs_ = nowMs;
  changes_ = changes_ + 1;
  return level_;
}

void WdiToggle::pause() { running_ = false; }

void WdiToggle::resume(uint32_t nowMs) {
  if (running_) return;
  running_ = true;
  level_ = !level_;                              // the first change on resume
  lastMs_ = nowMs;
  changes_ = changes_ + 1;
}

bool WdiToggle::takeLongGap(uint32_t* gapMs) {
  if (!longGap_) return false;
  longGap_ = false;
  if (gapMs) *gapMs = longGapMs_;
  return true;
}

}  // namespace wineador
