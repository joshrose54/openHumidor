#include "wineador/ota_update.h"
#include "wineador/event_text.h"
#include <stdio.h>
#include <string.h>

namespace wineador {

const char* otaOutcomeText(OtaOutcome o) {
  switch (o) {
    case OtaOutcome::Installed:   return "installed";
    case OtaOutcome::Stalled:     return "no data for 60 s";
    case OtaOutcome::CutOff:      return "the upload stopped part way";
    case OtaOutcome::Corrupt:     return "the image's own check failed (corrupt)";
    case OtaOutcome::FlashFailed: return "a flash write failed";
    case OtaOutcome::Cancelled:   return "the writing stopped moving for 70 s";
  }
  return "?";
}

const char* otaOutcomeWords(OtaOutcome o) {
  switch (o) {
    case OtaOutcome::Installed:   return "installed";
    case OtaOutcome::Stalled:     return "upload stalled";
    case OtaOutcome::CutOff:      return "upload cut off";
    case OtaOutcome::Corrupt:     return "file damaged";
    case OtaOutcome::FlashFailed: return "couldn't write it";
    case OtaOutcome::Cancelled:   return "writing stalled";
  }
  return "?";
}

// ---------------------------------------------------------------- the writer
OtaOutcome runOtaWrite(const uint8_t* head, size_t headN, BodyReader& body, uint32_t total, OtaFlash& flash,
                       OtaPacer& pacer, uint8_t* piece, uint32_t stallMs) {
  // a whole loop pass first: the "updating" guard has turned every load off before the first flash
  // step (esp_ota_begin itself erases a sector of the boot settings)
  if (!pacer.nextPass()) return OtaOutcome::Cancelled;
  if (!flash.begin(total)) return OtaOutcome::FlashFailed;
  uint32_t written = 0;
  size_t fill = headN < kOtaPiece ? headN : kOtaPiece;
  memcpy(piece, head, fill);
  size_t headLeft = headN - fill;            // (the head is never more than one piece)
  (void)headLeft;
  uint32_t taken = (uint32_t)fill;           // bytes of the image in hand (written + in the piece)
  for (;;) {
    // fill the piece to kOtaPiece, or to the end of the image
    while (fill < kOtaPiece && taken < total) {
      const size_t want = (size_t)(total - taken) < kOtaPiece - fill ? (size_t)(total - taken) : kOtaPiece - fill;
      const int k = body.read((char*)piece + fill, want, stallMs);
      if (k < 0) { flash.abort(); return OtaOutcome::Stalled; }    // no data for 60 s (or the connection went)
      if (k == 0) { flash.abort(); return OtaOutcome::CutOff; }    // the body ended short of its length
      fill += (size_t)k;
      taken += (uint32_t)k;
    }
    if (fill == 0) break;
    // one piece per loop pass: the hardware watchdog line has changed since the last flash step
    if (!pacer.nextPass()) { flash.abort(); return OtaOutcome::Cancelled; }
    if (!flash.write(piece, fill)) { flash.abort(); return OtaOutcome::FlashFailed; }
    written += (uint32_t)fill;
    pacer.progress(written);
    fill = 0;
    if (written >= total) break;
  }
  if (!pacer.nextPass()) { flash.abort(); return OtaOutcome::Cancelled; }
  switch (flash.end()) {
    case OtaFlash::End::Ok: break;
    case OtaFlash::End::Corrupt: return OtaOutcome::Corrupt;
    case OtaFlash::End::Failed: return OtaOutcome::FlashFailed;
  }
  if (!pacer.nextPass()) return OtaOutcome::Cancelled;
  if (!flash.setBoot()) return OtaOutcome::FlashFailed;
  return OtaOutcome::Installed;
}

ApiResult otaOutcomeRefusal(OtaOutcome o) {
  switch (o) {
    case OtaOutcome::Installed: return ApiResult{};
    case OtaOutcome::Corrupt: return refuse(Refusal::CorruptImage);
    case OtaOutcome::FlashFailed: return refuse(Refusal::FlashFailed);
    default: {
      char m[120];
      snprintf(m, sizeof(m), "Update stopped: %s. Nothing changed; try again.", otaOutcomeWords(o));
      return refuse(Refusal::Stalled, nullptr, m);
    }
  }
}

namespace {
// Counts what the writer reads, so the caller knows how much of the body is left to read away.
class CountingBody : public BodyReader {
 public:
  CountingBody(BodyReader& b, uint64_t* left) : b_(b), left_(left) {}
  int read(char* p, size_t n, uint32_t waitMs) override {
    const int k = b_.read(p, n, waitMs);
    if (k > 0) *left_ = (uint64_t)k > *left_ ? 0 : *left_ - (uint64_t)k;
    return k;
  }
 private:
  BodyReader& b_;
  uint64_t* left_;
};
}  // namespace

ApiResult runOtaInstall(WebBackend& b, const ImageFacts& f, const uint8_t* head, size_t headN, uint32_t total,
                        BodyReader& body, uint64_t* bodyLeft, uint8_t* piece, OtaFlash& flash, OtaPacer& pacer,
                        uint32_t stallMs) {
  MailRequest r;
  MailAnswer a;
  r.kind = MailKind::UpdateBegin;
  r.length = total;
  snprintf(r.version, sizeof(r.version), "%s", f.version);
  if (!b.ask(&r, &a)) { const ApiResult x = refuse(Refusal::NoAnswer); b.noteRefused(x); return x; }
  if (!a.result.ok()) { b.noteRefused(a.result); return a.result; }
  CountingBody counted(body, bodyLeft);
  const OtaOutcome o = runOtaWrite(head, headN, counted, total, flash, pacer, piece, stallMs);
  MailRequest e;
  MailAnswer ea;
  e.kind = MailKind::UpdateEnd;
  e.outcome = (uint8_t)o;
  snprintf(e.version, sizeof(e.version), "%s", f.version);
  b.ask(&e, &ea);          // if the loop doesn't answer, its own 70 s watch ends Updating
  return otaOutcomeRefusal(o);
}

bool imageChecksumOk(const uint8_t* img, size_t n) {
  if (n < 24 || img[0] != kImageMagic) return false;
  const int segments = img[1];
  size_t at = 24;
  uint8_t sum = 0xEF;
  for (int s = 0; s < segments; s++) {
    if (at + 8 > n) return false;
    const uint32_t len = (uint32_t)img[at + 4] | ((uint32_t)img[at + 5] << 8) | ((uint32_t)img[at + 6] << 16) |
                         ((uint32_t)img[at + 7] << 24);
    at += 8;
    if (len > n - at) return false;
    for (uint32_t i = 0; i < len; i++) sum ^= img[at + i];
    at += len;
  }
  // padded so the checksum byte is the last of a 16-byte block
  at = (at | 15);
  if (at >= n) return false;
  return img[at] == sum;
}

// ---------------------------------------------------------------- the loop's side
ApiResult OtaLoop::begin(uint32_t nowMs, uint32_t totalBytes, const char* version, bool compressorRunning,
                         UpdateWindow& window, uint32_t checkingLeftS) {
  if (phase_ != Phase::Idle) return refuse(Refusal::AlreadyUpdating);
  if (checkingLeftS > 0) {
    char m[120];
    snprintf(m, sizeof(m), "Last update still being checked. Try again in %lu s.", (unsigned long)checkingLeftS);
    ApiResult r = refuse(Refusal::BeingChecked, nullptr, m);
    r.retryS = checkingLeftS;
    return r;
  }
  if (!window.active(nowMs)) return refuse(Refusal::WindowClosed);
  if (compressorRunning) return refuse(Refusal::CompressorRunning);   // checked again on this pass (5.2)
  window.consume(nowMs);                     // the update has started: the window is used up (5.5)
  phase_ = Phase::Writing;
  cancelled_ = false;
  lastMoveMs_ = nowMs;
  bytes_ = 0;
  total_ = totalBytes;
  snprintf(version_, sizeof(version_), "%s", version ? version : "");
  return ApiResult{};
}

bool OtaLoop::watch(uint32_t nowMs, uint32_t bytesWritten) {
  if (phase_ != Phase::Writing) return false;
  if (bytesWritten != bytes_) {
    bytes_ = bytesWritten;
    lastMoveMs_ = nowMs;
    return false;
  }
  if (nowMs - lastMoveMs_ < kOtaLoopWatchMs) return false;
  phase_ = Phase::Idle;                      // the loop ends it: loads back under normal control
  cancelled_ = true;
  return true;
}

void OtaLoop::end(uint32_t nowMs, OtaOutcome o) {
  if (phase_ == Phase::Idle) return;         // the loop had already ended it
  endMs_ = nowMs;
  phase_ = o == OtaOutcome::Installed ? Phase::Restarting : Phase::Idle;
  if (o == OtaOutcome::Installed) bytes_ = total_;
}

uint8_t OtaLoop::pct() const {
  if (total_ == 0) return 0;
  const uint64_t p = (uint64_t)bytes_ * 100u / total_;
  return (uint8_t)(p > 100 ? 100 : p);
}

// ---------------------------------------------------------------- the record
static void copyText(char* dst, size_t n, const char* s) { snprintf(dst, n, "%s", s ? s : ""); }

OtaRecord otaRecordStarted(const char* version, const char* from, int64_t utcS, uint8_t slot) {
  OtaRecord r;
  r.step = OtaRecord::Writing;
  r.slot = slot;
  r.result = (uint8_t)UpdateResult::None;
  r.utcS = utcS;
  copyText(r.version, sizeof(r.version), version);
  copyText(r.from, sizeof(r.from), from);
  snprintf(r.detail, sizeof(r.detail), "%s", r.version);
  return r;
}

OtaRecord otaRecordEnded(const OtaRecord& started, OtaOutcome o, int64_t utcS) {
  OtaRecord r = started;
  r.utcS = utcS;
  if (o == OtaOutcome::Installed) {
    r.step = OtaRecord::Installed;
    r.result = (uint8_t)UpdateResult::Installed;
    snprintf(r.detail, sizeof(r.detail), "%s (was %s)", r.version, r.from);
  } else {
    r.step = OtaRecord::Done;
    r.result = (uint8_t)(o == OtaOutcome::Stalled || o == OtaOutcome::CutOff || o == OtaOutcome::Cancelled
                             ? UpdateResult::Abandoned : UpdateResult::Failed);
    snprintf(r.detail, sizeof(r.detail), "%s: %s", r.version, otaOutcomeWords(o));
  }
  return r;
}

OtaRecord otaRecordGood(const OtaRecord& rec, int64_t utcS) {
  OtaRecord r = rec;
  r.step = OtaRecord::Done;
  r.result = (uint8_t)UpdateResult::Installed;
  r.utcS = utcS;
  snprintf(r.detail, sizeof(r.detail), "%s (was %s)", r.version, r.from);
  return r;
}

OtaRecord otaRecordSelfRollback(const OtaRecord& rec, int64_t utcS) {
  OtaRecord r = rec;
  r.selfRollback = 1;
  r.utcS = utcS;
  return r;
}

OtaBoot readOtaBoot(const OtaRecord& rec, const OtaBootFacts& f) {
  OtaBoot b;
  b.record = rec;
  snprintf(b.eventType, sizeof(b.eventType), "%s", kEvTypeOta);
  b.last = (UpdateResult)rec.result;
  copyText(b.lastDetail, sizeof(b.lastDetail), rec.detail);
  b.lastUtcS = rec.utcS;
  auto done = [&](UpdateResult res, const char* detail) {
    b.record.step = OtaRecord::Done;
    b.record.result = (uint8_t)res;
    copyText(b.record.detail, sizeof(b.record.detail), detail);
    b.saveRecord = true;
    b.last = res;
    copyText(b.lastDetail, sizeof(b.lastDetail), detail);
  };
  if (f.running == RunningState::PendingVerify) {
    // the new image's first boot (or a later one before it was marked good)
    b.pendingVerify = true;
    b.logEvent = true;
    snprintf(b.event, sizeof(b.event), "%s: %s, waiting to start properly (5.2: 60 s of the loop; a reset before "
             "then rolls back)", kEvOtaWaiting, f.runningVersion);
    b.last = UpdateResult::Installed;
    snprintf(b.lastDetail, sizeof(b.lastDetail), "%s", f.runningVersion);   // the Checking box says the rest
    return b;
  }
  if (rec.step == OtaRecord::Writing) {
    // a reset or power cut while the image was being written: the old image was never touched
    char d[96];
    snprintf(d, sizeof(d), "%s: cut off by a restart; still %s", rec.version, f.runningVersion);
    done(UpdateResult::Failed, d);
    b.logEvent = true;
    snprintf(b.event, sizeof(b.event), "%s: %s (a reset or power cut during the write); still %s, nothing changed",
             kEvOtaInterrupted, rec.version, f.runningVersion);
    return b;
  }
  if (rec.step == OtaRecord::Installed) {
    const bool onNew = f.runningSlot >= 0 ? f.runningSlot == (int)rec.slot : strcmp(f.runningVersion, rec.version) == 0;
    if (onNew) {
      // the new image was marked good, and the board restarted before the record said so
      OtaRecord g = otaRecordGood(rec, rec.utcS);
      done(UpdateResult::Installed, g.detail);
      return b;
    }
    // back on the old image: the new one was rolled back by the bootloader (a reset before it was
    // marked good), or by itself after 4 min 30 s
    // the page's words in the record; the event keeps its logged wording (tools read it)
    char d[96];
    snprintf(d, sizeof(d), "%s didn't start; back on %s", rec.version, f.runningVersion);
    done(UpdateResult::RolledBack, d);
    b.logEvent = true;
    snprintf(b.event, sizeof(b.event), "%s from %s to %s: %s", kEvOtaRolledBack, rec.version, f.runningVersion,
             rec.selfRollback ? "not started properly within 4 min 30 s" : "it didn't start properly");
    return b;
  }
  return b;
}

// ---------------------------------------------------------------- events
int formatOtaStarted(char* out, size_t n, const char* version, uint32_t bytes, const char* from) {
  return snprintf(out, n, "%s: %s (%lu bytes), replacing %s; loads off", kEvOtaStarted, version, (unsigned long)bytes, from);
}

int formatOtaEnded(char* out, size_t n, OtaOutcome o, const char* version) {
  switch (o) {
    case OtaOutcome::Installed:
      return snprintf(out, n, "%s: %s, checked; restarting", kEvOtaInstalled, version);
    case OtaOutcome::Stalled:
    case OtaOutcome::CutOff:
    case OtaOutcome::Cancelled:
      return snprintf(out, n, "%s: %s, %s; half-written image discarded, normal control", kEvOtaAbandoned, version,
                      otaOutcomeText(o));
    default:
      return snprintf(out, n, "%s: %s, %s; nothing changed, normal control", kEvOtaFailed, version, otaOutcomeText(o));
  }
}

int formatOtaRefused(char* out, size_t n, const ApiResult& r) {
  // the code only (and the image's): the logged text stays stable when a message is reworded; the
  // event sentence turns the code into words
  return snprintf(out, n, "%s: %s%s%s", kEvOtaRefused, refusalCode(r.refusal), r.image ? " " : "", r.image ? r.image : "");
}

int formatOtaGood(char* out, size_t n, const char* version) {
  return snprintf(out, n, "%s: %s marked good (60 s of the loop, settings check passed, watchdog toggling, "
                  "first full sensor read)", kEvOtaGood, version);
}

int formatOtaSelfRollback(char* out, size_t n, const char* version) {
  return snprintf(out, n, "%s: %s rolls itself back to the previous image", kEvOtaSelfRollback, version);
}

int formatOtaLoopGaveUp(char* out, size_t n, const char* version, uint32_t bytes, uint32_t total) {
  return snprintf(out, n, "%s: %s, the writing stopped moving for 70 s at %lu of %lu bytes; the loop ended the update, "
                  "normal control", kEvOtaAbandoned, version, (unsigned long)bytes, (unsigned long)total);
}

}  // namespace wineador
