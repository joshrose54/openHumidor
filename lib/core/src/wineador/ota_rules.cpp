#include "wineador/ota_rules.h"
#include "wineador/logbook.h"
#include <stdio.h>

namespace wineador {

Refusal checkUpdateStart(const UpdateGateInputs& in) {
  if (in.updating) return Refusal::AlreadyUpdating;
  if (!in.windowOpen) return Refusal::WindowClosed;
  if (in.compressorRunning) return Refusal::CompressorRunning;
  if (in.transferBusy) return Refusal::Busy;
  if (in.length < kImageCheckBytes) return Refusal::WrongImage;   // too short to be an image
  if (in.slotBytes == 0 || in.length > in.slotBytes) return Refusal::TooBig;
  return Refusal::None;
}

ApiResult refuseImage(ImageVerdict v) {
  ApiResult r = refuse(Refusal::WrongImage, nullptr, imageVerdictText(v));
  r.image = imageVerdictCode(v);
  return r;
}

void UploadWatch::begin(uint32_t nowMs, uint32_t totalBytes) {
  active_ = true;
  lastMs_ = nowMs;
  total_ = totalBytes;
  got_ = 0;
}

void UploadWatch::data(uint32_t nowMs, uint32_t bytes) {
  if (!active_ || bytes == 0) return;
  got_ += bytes;
  lastMs_ = nowMs;
}

uint8_t UploadWatch::percent() const {
  if (total_ == 0) return 0;
  const uint64_t p = (uint64_t)got_ * 100u / total_;
  return (uint8_t)(p > 100 ? 100 : p);
}

void StartedProperly::pass(uint32_t nowMs, bool settingsOk, bool hwWatchdogToggling, bool firstFullSweepDone) {
  if (!started_) { started_ = true; firstMs_ = nowMs; }
  settingsOk_ = settingsOk;
  hwdog_ = hwWatchdogToggling;
  sweep_ = firstFullSweepDone;
  if (!good_ && nowMs - firstMs_ >= kStartedProperlyMs && settingsOk && hwWatchdogToggling && firstFullSweepDone)
    good_ = true;
}

const char* StartedProperly::waitingFor(uint32_t nowMs, char* out, size_t n) const {
  if (good_) snprintf(out, n, "started properly");
  else if (!started_) snprintf(out, n, "loop not running");
  else if (!settingsOk_) snprintf(out, n, "settings check failed");
  else if (!hwdog_) snprintf(out, n, "hardware watchdog not toggling");
  else if (!sweep_) snprintf(out, n, "first sensor read not done");
  else snprintf(out, n, "running %lu s of 60", (unsigned long)((nowMs - firstMs_) / 1000));
  return out;
}

Refusal TransferGate::tryStart(TransferKind k, uint32_t freeInternalBytes, uint32_t marginBytes) {
  if (k == TransferKind::None) return Refusal::None;
  if (busy()) return Refusal::Busy;
  if ((uint64_t)freeInternalBytes < (uint64_t)kHeapFloorBytes + marginBytes) return Refusal::LowMemory;
  kind_ = k;
  return Refusal::None;
}

}  // namespace wineador
