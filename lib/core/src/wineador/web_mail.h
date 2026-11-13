#pragma once
// The mailbox between the web server and the control loop (spec 5.2, 5.5), the plain-logic half.
// The server never touches the control objects: a write (settings, a manual exclude, a door test)
// or the start of an upload goes in here; the loop takes it on its next pass, decides it against
// its own real settings and state (decideMail below), applies it, and puts the answer back. The
// server waits for that answer (a few ms: one pass) and sends it to the browser.
//
// One request at a time. Each has a number, and an answer carries its request's number, so an
// answer can only ever reach the request it belongs to. A request the loop hasn't taken yet can be
// withdrawn (the server gave up waiting); once taken, the server waits for its answer, because the
// loop is reading the request's fields.
//
// Not thread-safe by itself: on the board every call is made inside one short critical section
// (src/app/web_link.cpp), so the loop never waits for the server.

#include <stdint.h>
#include <stddef.h>
#include "wineador/web_api.h"
#include "wineador/ota_rules.h"
#include "wineador/json_in.h"
#include "wineador/settings.h"

namespace wineador {

constexpr uint32_t kMailWaitMs = 3000;   // the server's wait for the loop's answer (no_answer after)
constexpr uint32_t kDashboardSaveGapMs = 10000;   // 5.5: at most one saved dashboard change every 10 s

// Spec 5.5: at most one saved dashboard change (settings or a manual exclude) every 10 s, so a
// faulty page or script can't keep writing to flash or pausing the loop. Only changes from the
// dashboard count and are limited; the LCD and the serial console are not. A write that changes
// nothing saves nothing, so it is neither refused nor counted. The loop owns it (no locking).
class SaveLimit {
 public:
  // 0 if a change may be saved now, else the whole seconds still to wait (1-10).
  uint32_t waitS(uint32_t nowMs) const;
  void saved(uint32_t nowMs) { any_ = true; lastMs_ = nowMs; }

 private:
  bool any_ = false;
  uint32_t lastMs_ = 0;
};

// UpdateStart: the upload's gate (before a byte is read). UpdateBegin: the writing is about to start
// (the loop checks the compressor on that pass, uses the window up, sets Updating). UpdateEnd: the
// writing has finished (outcome). UpdateBegin and UpdateEnd are handled by the loop's own OTA state
// (ota_update.h OtaLoop), not decideMail.
enum class MailKind : uint8_t { None, Settings, Exclude, DoorTest, UpdateStart, UpdateBegin, UpdateEnd };

struct MailRequest {
  uint32_t id = 0;
  MailKind kind = MailKind::None;
  const FlatJson* json = nullptr;   // Settings, Exclude, DoorTest: the body, already read (stays put until answered)
  uint32_t length = 0;              // UpdateStart: the upload's Content-Length
  bool transferBusy = false;        // UpdateStart: a log download is running (the server's transfer gate)
  char version[28] = "";            // UpdateBegin: the image's version (its identity block)
  uint8_t outcome = 0;              // UpdateEnd: OtaOutcome
};

struct MailAnswer {
  uint32_t id = 0;
  ApiResult result;
  UserSettings user;                // Settings, Exclude: the settings now (accepted or not)
  int channel = -1;                 // Exclude
  bool excluded = false;            // Exclude
  char note[kMaxDoorNote + 1] = ""; // DoorTest
};

class Mailbox {
 public:
  // Server: posts a request (it gets the next number). False while another is waiting or being
  // decided; an old answer that was never collected is dropped.
  bool post(MailRequest* r);
  // Loop: the waiting request, if any; it is then taken (can no longer be withdrawn).
  bool take(MailRequest* out);
  // Loop: the answer to the request it took (the number must match, or it is dropped).
  void answer(const MailAnswer& a);
  // Server: its answer, if it has come (then the box is empty again).
  bool collect(uint32_t id, MailAnswer* out);
  // Server: gives up waiting. True if the loop hadn't taken it (it never will); false if it has,
  // and then the server must keep waiting for collect().
  bool withdraw(uint32_t id);

  enum class State : uint8_t { Empty, Posted, Taken, Answered };
  State state() const { return state_; }

 private:
  State state_ = State::Empty;
  uint32_t next_ = 1;
  MailRequest req_;
  MailAnswer ans_;
};

// What the loop decides a request against: its own settings and state, at that pass.
struct MailContext {
  UserSettings user;
  bool updating = false;
  bool windowOpen = false;          // the LCD's Allow update window
  bool compressorRunning = false;
  uint32_t slotBytes = 0;           // the OTA app slot
  uint32_t checkingLeftS = 0;       // the image running is still being checked (5.2): being_checked
  uint32_t nowMs = 0;               // for the save limit
  const SaveLimit* limit = nullptr; // the dashboard's save limit (nullptr: none)
};

// The loop's decision: the answer to send back, with the new settings for Settings and Exclude
// (equal to ctx.user when refused). A valid change less than 10 s after the last saved one is
// refused too_soon, "Try again in N s." (checked after the values, so a wrong value is told as
// such). The loop then applies an accepted change (logs it, and calls limit.saved()); an
// accepted DoorTest is logged; an accepted UpdateStart means the upload may be read (the server
// then checks the image).
MailAnswer decideMail(const MailRequest& r, const MailContext& ctx);

}  // namespace wineador
