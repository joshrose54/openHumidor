#include "wineador/web_mail.h"
#include <stdio.h>
#include <string.h>

namespace wineador {

bool Mailbox::post(MailRequest* r) {
  // An answer nobody collected (its server gave up waiting) is simply replaced.
  if (state_ == State::Posted || state_ == State::Taken) return false;
  r->id = next_++;
  if (next_ == 0) next_ = 1;        // 0 is never a request's number
  req_ = *r;
  state_ = State::Posted;
  return true;
}

bool Mailbox::take(MailRequest* out) {
  if (state_ != State::Posted) return false;
  *out = req_;
  state_ = State::Taken;
  return true;
}

void Mailbox::answer(const MailAnswer& a) {
  if (state_ != State::Taken || a.id != req_.id) return;
  ans_ = a;
  state_ = State::Answered;
}

bool Mailbox::collect(uint32_t id, MailAnswer* out) {
  if (state_ != State::Answered || ans_.id != id) return false;
  *out = ans_;
  state_ = State::Empty;
  return true;
}

bool Mailbox::withdraw(uint32_t id) {
  if (state_ == State::Posted && req_.id == id) {
    state_ = State::Empty;
    return true;
  }
  return false;
}

uint32_t SaveLimit::waitS(uint32_t nowMs) const {
  if (!any_) return 0;
  const uint32_t since = nowMs - lastMs_;
  if (since >= kDashboardSaveGapMs) return 0;
  return (kDashboardSaveGapMs - since + 999) / 1000;
}

// A valid change that would be saved: refused if it comes too soon after the last.
static void applyLimit(MailAnswer* a, const MailContext& ctx) {
  if (!a->result.ok() || !ctx.limit || sameUserSettings(a->user, ctx.user)) return;
  const uint32_t w = ctx.limit->waitS(ctx.nowMs);
  if (w == 0) return;
  char m[120];
  snprintf(m, sizeof(m), "Changes from the dashboard are saved at most once every 10 s. Try again in %lu s.",
           (unsigned long)w);
  a->result = refuse(Refusal::TooSoon, nullptr, m);
  a->result.retryS = w;
  a->user = ctx.user;
  a->channel = -1;
  a->excluded = false;
}

MailAnswer decideMail(const MailRequest& r, const MailContext& ctx) {
  MailAnswer a;
  a.id = r.id;
  a.user = ctx.user;
  switch (r.kind) {
    case MailKind::Settings:
      if (!r.json) { a.result = refuse(Refusal::BadJson); break; }
      a.result = checkSettingsFields(*r.json, ctx.user, ctx.updating, &a.user);
      if (!a.result.ok()) a.user = ctx.user;
      applyLimit(&a, ctx);
      break;
    case MailKind::Exclude:
      if (!r.json) { a.result = refuse(Refusal::BadJson); break; }
      a.result = checkExcludeFields(*r.json, ctx.user, ctx.updating, &a.channel, &a.user);
      if (a.result.ok()) a.excluded = (a.user.manualExcludeMask >> a.channel) & 1;
      else a.user = ctx.user;
      applyLimit(&a, ctx);
      break;
    case MailKind::DoorTest:
      if (!r.json) { a.result = refuse(Refusal::BadJson); break; }
      a.result = checkDoorTestFields(*r.json, a.note, sizeof(a.note));
      break;
    case MailKind::UpdateStart: {
      UpdateGateInputs g;
      g.windowOpen = ctx.windowOpen;
      g.compressorRunning = ctx.compressorRunning;
      g.updating = ctx.updating;
      g.transferBusy = r.transferBusy;
      g.length = r.length;
      g.slotBytes = ctx.slotBytes;
      const Refusal f = checkUpdateStart(g);
      a.result = f == Refusal::None ? ApiResult{} : (f == Refusal::WrongImage ? refuseImage(ImageVerdict::TooShort) : refuse(f));
      if (f == Refusal::None && ctx.checkingLeftS > 0) {
        char m[120];
        snprintf(m, sizeof(m), "The last update is still being checked: try again in %lu s.", (unsigned long)ctx.checkingLeftS);
        a.result = refuse(Refusal::BeingChecked, nullptr, m);
        a.result.retryS = ctx.checkingLeftS;
      }
      break;
    }
    case MailKind::None:
    default:
      a.result = refuse(Refusal::NotFound);
      break;
  }
  return a;
}

}  // namespace wineador
