#pragma once
// Rev Q: the link between the control loop and the web server (spec 5.2, 5.5). The server never
// touches the control objects; the loop never waits for the server. Two things cross:
//
//   the state copy: the loop fills staging() and publish()es it about once a second (and straight
//     after a change from the dashboard). publish() only tries the lock: if the server is copying
//     at that moment, the loop carries on and publishes on a later pass. The server copies it out
//     under the same lock (a memcpy of a few KB).
//   the mailbox (lib/core web_mail.h): the server posts a write or the start of an upload and waits
//     (on its own task) for the answer; the loop takes it with takeMail(), decides it against its
//     own settings, applies it, and answerMail()s. Both are a few instructions in a critical
//     section, never a wait.
//
// The copies live in PSRAM.

#include <stdint.h>
#include "wineador/web_status.h"
#include "wineador/web_lists.h"
#include "wineador/web_mail.h"
#include "wineador/settings.h"

namespace weblink {

struct State {
  wineador::StatusSnapshot status;
  wineador::UserSettings user;
  wineador::TuningSettings tuning;
  wineador::UpdateState update;
};

bool begin();                       // BOOT ONLY: the copies in PSRAM, the lock, the mailbox's signal

// ---- the loop's side (never waits)
State* staging();                   // fill this, then publish()
bool publish();                     // false: the server was copying; try again next pass
bool takeMail(wineador::MailRequest* out);
void answerMail(const wineador::MailAnswer& a);

// ---- the server's side (its own tasks)
void copy(State* out);              // the last published state
// Posts a request and waits up to kMailWaitMs for the loop's answer. False: no answer (withdrawn).
bool ask(wineador::MailRequest* r, wineador::MailAnswer* a);
// A refused upload, for the loop to log (5.4, capped). Never waits: if four are already waiting,
// this one is counted as dropped.
void noteRefused(const wineador::ApiResult& r);

// ---- the loop's side
bool takeRefused(wineador::ApiResult* out);
uint32_t refusedDropped();

}  // namespace weblink
