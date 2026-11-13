#pragma once
// The OTA rules (spec 5.2, 5.5) as plain logic:
//   - the Allow update gate: whether an upload may start (before a byte of it is read);
//   - the stall watch: an upload with no data for 60 s is abandoned;
//   - "started properly": when a new image is marked good (a reset before that rolls back).
// And the transfer gate (5.5): at most one long transfer (a log download or an update) at a
// time, and none started while free internal RAM is near the floor.
//
// The window itself is the LCD's UpdateWindow (lcd_ui.h): it is used up only when an update
// actually starts (the loop calls consume()); a refused upload leaves it open (5.5).

#include <stdint.h>
#include <stddef.h>
#include "wineador/web_api.h"
#include "wineador/image_identity.h"

namespace wineador {

constexpr uint32_t kUploadStallMs = 60000;      // 5.2: no data for 60 s -> abandoned
constexpr uint32_t kStartedProperlyMs = 60000;  // 5.2: the loop has run 60 s ...
constexpr uint32_t kSelfRollbackMs = 270000;    // 5.2: not marked good within 4 min 30 s of boot (inside the 5-min boot hold)

// ---------------------------------------------------------------- the Allow update gate
struct UpdateGateInputs {
  bool windowOpen = false;         // the LCD's Allow update window
  bool compressorRunning = false;
  bool updating = false;           // an update already running
  bool transferBusy = false;       // a log download running (the transfer gate)
  uint32_t length = 0;             // Content-Length of the upload
  uint32_t slotBytes = 0;          // the OTA app slot's size (3,342,336 on Rev Q)
};
// Checked in this order: already updating, window closed, compressor running, another transfer,
// too short to be an image, too big. None = the upload may be read; its first bytes then go to checkImage().
Refusal checkUpdateStart(const UpdateGateInputs& in);

// The refusal for an image that isn't a Rev Q cabinet build: wrong_image, with the reason as
// "image" and its plain sentence as the message.
ApiResult refuseImage(ImageVerdict v);

// ---------------------------------------------------------------- the stall watch
class UploadWatch {
 public:
  void begin(uint32_t nowMs, uint32_t totalBytes);
  void data(uint32_t nowMs, uint32_t bytes);
  bool stalled(uint32_t nowMs) const { return active_ && nowMs - lastMs_ >= kUploadStallMs; }
  uint8_t percent() const;
  uint32_t received() const { return got_; }
  bool active() const { return active_; }
  void end() { active_ = false; }

 private:
  bool active_ = false;
  uint32_t lastMs_ = 0, total_ = 0, got_ = 0;
};

// ---------------------------------------------------------------- "started properly"
// Spec 5.2: the control loop has run 60 s with the settings check passed, the hardware watchdog
// toggling and the first full sensor read done; only then is a new image marked good. Fed every
// pass from the first; good() turns true once, then stays.
class StartedProperly {
 public:
  void pass(uint32_t nowMs, bool settingsOk, bool hwWatchdogToggling, bool firstFullSweepDone);
  bool good() const { return good_; }
  // 5.2: not marked good within 4 min 30 s of boot (running, but never meeting the rule): true once
  // from then on; the firmware rolls itself back, inside the boot hold so it never cuts a run short.
  bool overdue(uint32_t nowMs) const { return started_ && !good_ && nowMs - firstMs_ >= kSelfRollbackMs; }
  // The seconds a further upload should wait (being_checked): to 60 s while that is the only thing
  // left; otherwise to the 4 min 30 s deadline. 0 once good.
  uint32_t leftS(uint32_t nowMs) const;
  // Why not yet (for the status line): "running 42 s", "settings check failed" ...
  const char* waitingFor(uint32_t nowMs, char* out, size_t n) const;

 private:
  bool started_ = false, good_ = false;
  uint32_t firstMs_ = 0;
  bool settingsOk_ = false, hwdog_ = false, sweep_ = false;
};

// ---------------------------------------------------------------- the transfer gate
enum class TransferKind : uint8_t { None, Download, Update };

class TransferGate {
 public:
  // A long transfer may start: none running, and free internal RAM at least the floor plus the
  // margin (set from BP2 test 39). Busy / LowMemory otherwise.
  Refusal tryStart(TransferKind k, uint32_t freeInternalBytes, uint32_t marginBytes);
  void finish() { kind_ = TransferKind::None; }
  bool busy() const { return kind_ != TransferKind::None; }
  TransferKind kind() const { return kind_; }

 private:
  TransferKind kind_ = TransferKind::None;
};

}  // namespace wineador
