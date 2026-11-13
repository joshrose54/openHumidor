#pragma once
// OTA from the dashboard (spec 5.2, 5.5, 3A.11; stage Q5d), the plain-logic half. The board's half
// (src/app/ota.cpp) gives it the flash; the Mac review build gives it a flash in memory, so both
// run this same code.
//
//   the writer (the long-transfer task): the image a 4 KB piece at a time, one piece per control
//     loop pass (so the hardware watchdog line changes between any two flash steps; each piece's
//     write erases the one sector it enters), the 60 s stall rule, an upload cut off short, the
//     image's own check at the end.
//   the loop's side (OtaLoop): starting (the compressor checked on that pass, the window used up),
//     Updating (loads off, no sensor sweeps, no fan-stall check), its own watch (the writing stops
//     moving for 70 s: Updating ends by itself), the end, the restart.
//   the boot record (OtaRecord, readOtaBoot): what the last update did, read at every boot — waiting
//     to start properly, rolled back, interrupted by a reset or power cut — for the event and the
//     Update page.

#include <stdint.h>
#include <stddef.h>
#include "wineador/web_api.h"
#include "wineador/web_app.h"
#include "wineador/web_lists.h"
#include "wineador/lcd_ui.h"
#include "wineador/web_mail.h"
#include "wineador/image_identity.h"

namespace wineador {

constexpr size_t kOtaPiece = 4096;               // one flash sector: one erase, one piece, one pass
constexpr uint32_t kOtaLoopWatchMs = 70000;      // 5.2: the writing stops moving for 70 s
constexpr uint32_t kOtaRestartDelayMs = 1500;    // the reply and the event go out first
// (the self-rollback deadline, 4 min 30 s after boot: kSelfRollbackMs, ota_rules.h)

// ---------------------------------------------------------------- the writer
class OtaFlash {
 public:
  virtual ~OtaFlash() {}
  virtual bool begin(uint32_t imageBytes) = 0;   // nothing erased yet (sequential writes)
  virtual bool write(const uint8_t* p, size_t n) = 0;   // n <= kOtaPiece; erases the sector it enters
  enum class End : uint8_t { Ok, Corrupt, Failed };
  virtual End end() = 0;                         // the image's own check (checksum, SHA-256)
  virtual void abort() = 0;                      // the half-written slot is left unused
  virtual bool setBoot() = 0;                    // the new image boots next
};

class OtaPacer {
 public:
  virtual ~OtaPacer() {}
  // Waits until the control loop has made a pass since the last call (the watchdog line changed).
  // False: the loop ended the update (its own watch) or doesn't answer.
  virtual bool nextPass() = 0;
  virtual void progress(uint32_t bytesWritten) = 0;
};

enum class OtaOutcome : uint8_t { Installed, Stalled, CutOff, Corrupt, FlashFailed, Cancelled };
const char* otaOutcomeText(OtaOutcome o);   // "installed", "no data for 60 s" ...

// Writes the image: head (the first bytes, already read for the identity check) then the rest of
// the body, `total` bytes in all, in pieces of exactly kOtaPiece (the last may be shorter).
// `piece` is the caller's kOtaPiece buffer. Installed: checked, and set to boot next.
OtaOutcome runOtaWrite(const uint8_t* head, size_t headN, BodyReader& body, uint32_t total, OtaFlash& flash,
                       OtaPacer& pacer, uint8_t* piece, uint32_t stallMs = kUploadStallMs);

// The whole install, the same on the board and in the review build: asks the loop to start
// (UpdateBegin; refused: logged through noteRefused), writes (runOtaWrite), tells the loop how it
// ended (UpdateEnd; the loop logs it). *bodyLeft is lowered by what was read.
ApiResult runOtaInstall(WebBackend& b, const ImageFacts& f, const uint8_t* head, size_t headN, uint32_t total,
                        BodyReader& body, uint64_t* bodyLeft, uint8_t* piece, OtaFlash& flash, OtaPacer& pacer,
                        uint32_t stallMs = kUploadStallMs);
ApiResult otaOutcomeRefusal(OtaOutcome o);   // Installed: ok

// An image's own checksum (the byte after its segments: 0xEF xor every segment byte), as the
// bootloader and ESP-IDF check it, for the review build's flash and the native tests (the board
// uses ESP-IDF's own check, which also checks the SHA-256 every image carries). False if the
// segments don't fit or the checksum is wrong.
bool imageChecksumOk(const uint8_t* img, size_t n);

// ---------------------------------------------------------------- the loop's side
class OtaLoop {
 public:
  enum class Phase : uint8_t { Idle, Writing, Restarting };
  // The writer asks to start (the mailbox): decided on this pass against the loop's own state.
  // checkingLeftS > 0: this image is still being checked (5.2), refused being_checked.
  ApiResult begin(uint32_t nowMs, uint32_t totalBytes, const char* version, bool compressorRunning,
                  UpdateWindow& window, uint32_t checkingLeftS);
  // Every pass while Writing: the writer's bytes so far. True once if the writing stopped moving for
  // 70 s (then Idle: the loop ended it; the writer sees cancelled()).
  bool watch(uint32_t nowMs, uint32_t bytesWritten);
  // The writer has finished: Installed -> Restarting (the restart is due kOtaRestartDelayMs later).
  void end(uint32_t nowMs, OtaOutcome o);
  bool updating() const { return phase_ != Phase::Idle; }   // loads off, no sweeps, no fan-stall check
  bool cancelled() const { return cancelled_; }
  Phase phase() const { return phase_; }
  uint8_t pct() const;
  uint32_t bytes() const { return bytes_; }
  uint32_t total() const { return total_; }
  const char* version() const { return version_; }
  bool restartDue(uint32_t nowMs) const { return phase_ == Phase::Restarting && nowMs - endMs_ >= kOtaRestartDelayMs; }

 private:
  Phase phase_ = Phase::Idle;
  bool cancelled_ = false;
  uint32_t lastMoveMs_ = 0, endMs_ = 0, bytes_ = 0, total_ = 0;
  char version_[28] = "";
};

// ---------------------------------------------------------------- the record, kept in NVS
// Written by the loop at each step; read at every boot. Plain bytes (NVS keeps it as a blob).
struct OtaRecord {
  enum Step : uint8_t { None = 0, Writing = 1, Installed = 2, Done = 3 };
  uint8_t step = None;              // where the last update got to
  uint8_t result = 0;               // UpdateResult, for the Update page
  uint8_t selfRollback = 0;         // Installed: the new image gave up after 4 min 30 s (5.2)
  uint8_t slot = 0;                 // the app slot the image was written to (0 = ota_0, 1 = ota_1)
  int64_t utcS = 0;                 // when (0: unknown)
  char version[28] = "";            // the image being installed
  char from[28] = "";               // the image it replaced
  char detail[96] = "";             // for the Update page
};

enum class RunningState : uint8_t { Valid, PendingVerify, Other };

struct OtaBootFacts {
  RunningState running = RunningState::Valid;
  const char* runningVersion = "";
  int runningSlot = -1;             // the app slot running now (0, 1; -1 unknown): tells a rollback
                                    // from a good boot even when both images have the same version
  bool haveInvalid = false;         // the bootloader marked a slot invalid / aborted (a rollback)
  const char* invalidVersion = "";
};

struct OtaBoot {
  bool pendingVerify = false;       // this image must start properly within 4 min 30 s (5.2)
  bool logEvent = false;
  char eventType[16] = "";          // always "ota"
  char event[200] = "";
  bool saveRecord = false;
  OtaRecord record;                 // the record from now on
  UpdateResult last = UpdateResult::None;
  char lastDetail[120] = "";
  int64_t lastUtcS = 0;
};

// What the last update did, from the record and what the bootloader says.
OtaBoot readOtaBoot(const OtaRecord& rec, const OtaBootFacts& f);

// The record for each step the loop takes (the version and times filled in).
OtaRecord otaRecordStarted(const char* version, const char* from, int64_t utcS, uint8_t slot);
OtaRecord otaRecordEnded(const OtaRecord& started, OtaOutcome o, int64_t utcS);   // Installed: step Installed
OtaRecord otaRecordGood(const OtaRecord& rec, int64_t utcS);                       // marked good
OtaRecord otaRecordSelfRollback(const OtaRecord& rec, int64_t utcS);

// ---------------------------------------------------------------- events (type "ota", 5.4)
int formatOtaStarted(char* out, size_t n, const char* version, uint32_t bytes, const char* from);
int formatOtaEnded(char* out, size_t n, OtaOutcome o, const char* version);
int formatOtaRefused(char* out, size_t n, const ApiResult& r);
int formatOtaGood(char* out, size_t n, const char* version);
int formatOtaSelfRollback(char* out, size_t n, const char* version);
int formatOtaLoopGaveUp(char* out, size_t n, const char* version, uint32_t bytes, uint32_t total);

}  // namespace wineador
