// The mailbox between the web server and the control loop (lib/core web_mail): one request at a
// time, each answer reaching only its own request, a withdrawn request never taken, and the loop's
// decisions made against its own settings and state (refused while Updating, the upload gate).
#include <unity.h>
#include <string.h>
#include "wineador/web_mail.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

static FlatJson parsed(const char* body) {
  FlatJson j;
  TEST_ASSERT_TRUE(parseWriteBody(body, strlen(body), &j).ok());
  return j;
}

void test_one_request_at_a_time_and_answers_by_number() {
  Mailbox m;
  MailRequest a, b, got;
  a.kind = MailKind::DoorTest;
  TEST_ASSERT_TRUE(m.post(&a));
  TEST_ASSERT_TRUE(a.id != 0);
  TEST_ASSERT_FALSE(m.post(&b));                    // one at a time
  TEST_ASSERT_TRUE(m.take(&got));
  TEST_ASSERT_EQUAL_UINT32(a.id, got.id);
  TEST_ASSERT_FALSE(m.take(&got));                  // taken once
  MailAnswer wrong;
  wrong.id = a.id + 7;
  m.answer(wrong);                                  // not its number: dropped
  MailAnswer out;
  TEST_ASSERT_FALSE(m.collect(a.id, &out));
  MailAnswer right;
  right.id = a.id;
  m.answer(right);
  TEST_ASSERT_FALSE(m.collect(a.id + 1, &out));     // another request can't collect it
  TEST_ASSERT_TRUE(m.collect(a.id, &out));
  TEST_ASSERT_TRUE(m.state() == Mailbox::State::Empty);
  TEST_ASSERT_TRUE(m.post(&b));
  TEST_ASSERT_TRUE(b.id != a.id);
}

void test_a_withdrawn_request_is_never_taken_and_a_taken_one_cannot_be_withdrawn() {
  Mailbox m;
  MailRequest a, got;
  m.post(&a);
  TEST_ASSERT_TRUE(m.withdraw(a.id));
  TEST_ASSERT_FALSE(m.take(&got));                  // the loop never sees it
  MailRequest b;
  m.post(&b);
  m.take(&got);
  TEST_ASSERT_FALSE(m.withdraw(b.id));              // the loop is reading it: the server must wait
  // a late answer to the first (withdrawn) request never reaches the second
  MailAnswer late;
  late.id = a.id;
  m.answer(late);
  MailAnswer out;
  TEST_ASSERT_FALSE(m.collect(b.id, &out));
  MailAnswer ans;
  ans.id = b.id;
  m.answer(ans);
  TEST_ASSERT_TRUE(m.collect(b.id, &out));
  // a server that gave up after the loop took its request: the answer comes later, uncollected,
  // and the next request still goes in (and never gets the old answer)
  MailRequest c, d;
  m.post(&c);
  m.take(&got);
  MailAnswer ac;
  ac.id = c.id;
  m.answer(ac);
  TEST_ASSERT_TRUE(m.post(&d));
  TEST_ASSERT_FALSE(m.collect(c.id, &out));
  TEST_ASSERT_FALSE(m.collect(d.id, &out));
}

void test_settings_decided_against_the_loops_own_settings() {
  MailContext c;
  c.user.targetTempC = 19.0f;
  FlatJson j = parsed("{\"target_temp_c\": 20.5}");
  MailRequest r;
  r.id = 5;
  r.kind = MailKind::Settings;
  r.json = &j;
  MailAnswer a = decideMail(r, c);
  TEST_ASSERT_TRUE(a.result.ok());
  TEST_ASSERT_EQUAL_UINT32(5, a.id);
  TEST_ASSERT_EQUAL_FLOAT(20.5f, a.user.targetTempC);
  // refused while Updating (5.5, as on the LCD): the settings stay as they are
  c.updating = true;
  a = decideMail(r, c);
  TEST_ASSERT_TRUE(a.result.refusal == Refusal::Updating);
  TEST_ASSERT_EQUAL_FLOAT(19.0f, a.user.targetTempC);
  // out of bounds: unchanged
  c.updating = false;
  FlatJson k = parsed("{\"target_temp_c\": 30}");
  r.json = &k;
  a = decideMail(r, c);
  TEST_ASSERT_TRUE(a.result.refusal == Refusal::OutOfBounds);
  TEST_ASSERT_EQUAL_FLOAT(19.0f, a.user.targetTempC);
}

void test_exclude_and_door_test() {
  MailContext c;
  FlatJson j = parsed("{\"channel\": \"ch3\", \"excluded\": true}");
  MailRequest r;
  r.kind = MailKind::Exclude;
  r.json = &j;
  MailAnswer a = decideMail(r, c);
  TEST_ASSERT_TRUE(a.result.ok());
  TEST_ASSERT_EQUAL_INT(3, a.channel);
  TEST_ASSERT_TRUE(a.excluded);
  TEST_ASSERT_EQUAL_HEX8(0x08, a.user.manualExcludeMask);
  c.updating = true;
  TEST_ASSERT_TRUE(decideMail(r, c).result.refusal == Refusal::Updating);
  FlatJson d = parsed("{\"note\": \"door open 30 s\"}");
  r.kind = MailKind::DoorTest;
  r.json = &d;
  a = decideMail(r, c);
  TEST_ASSERT_TRUE(a.result.ok());
  TEST_ASSERT_EQUAL_STRING("door open 30 s", a.note);
}

void test_the_upload_gate_is_the_loops() {
  MailContext c;
  c.slotBytes = 3342336;
  MailRequest r;
  r.kind = MailKind::UpdateStart;
  r.length = 1200000;
  TEST_ASSERT_TRUE(decideMail(r, c).result.refusal == Refusal::WindowClosed);
  c.windowOpen = true;
  c.compressorRunning = true;
  TEST_ASSERT_TRUE(decideMail(r, c).result.refusal == Refusal::CompressorRunning);
  c.compressorRunning = false;
  r.transferBusy = true;
  TEST_ASSERT_TRUE(decideMail(r, c).result.refusal == Refusal::Busy);
  r.transferBusy = false;
  c.updating = true;
  TEST_ASSERT_TRUE(decideMail(r, c).result.refusal == Refusal::AlreadyUpdating);
  c.updating = false;
  r.length = 100;
  MailAnswer a = decideMail(r, c);
  TEST_ASSERT_TRUE(a.result.refusal == Refusal::WrongImage);
  TEST_ASSERT_EQUAL_STRING("too_short", a.result.image);
  r.length = 4000000;
  TEST_ASSERT_TRUE(decideMail(r, c).result.refusal == Refusal::TooBig);
  r.length = 1200000;
  TEST_ASSERT_TRUE(decideMail(r, c).result.ok());
}

// Spec 5.5: at most one saved dashboard change every 10 s (settings and the manual exclude).
void test_one_saved_dashboard_change_every_ten_seconds() {
  SaveLimit limit;
  MailContext c;
  c.limit = &limit;
  c.nowMs = 50000;
  FlatJson t = parsed("{\"target_temp_c\": 20.5}");
  MailRequest r;
  r.kind = MailKind::Settings;
  r.json = &t;
  MailAnswer a = decideMail(r, c);
  TEST_ASSERT_TRUE(a.result.ok());                          // the first: saved
  c.user = a.user;
  limit.saved(c.nowMs);
  // 3 s later, another change: refused, with how long to wait, settings unchanged
  c.nowMs = 53000;
  FlatJson t2 = parsed("{\"target_temp_c\": 21}");
  r.json = &t2;
  a = decideMail(r, c);
  TEST_ASSERT_TRUE(a.result.refusal == Refusal::TooSoon);
  TEST_ASSERT_EQUAL_INT(429, httpStatus(a.result.refusal));
  TEST_ASSERT_EQUAL_STRING("Changes from the dashboard are saved at most once every 10 s. Try again in 7 s.", a.result.message);
  TEST_ASSERT_EQUAL_UINT32(7, a.result.retryS);           // the Retry-After header's number
  TEST_ASSERT_EQUAL_FLOAT(20.5f, a.user.targetTempC);
  // the manual exclude is limited too
  FlatJson e = parsed("{\"channel\": 1, \"excluded\": true}");
  MailRequest x;
  x.kind = MailKind::Exclude;
  x.json = &e;
  MailAnswer ax = decideMail(x, c);
  TEST_ASSERT_TRUE(ax.result.refusal == Refusal::TooSoon);
  TEST_ASSERT_EQUAL_HEX8(0, ax.user.manualExcludeMask);
  // a wrong value is told as such, not "too soon"
  FlatJson bad = parsed("{\"target_temp_c\": 30}");
  r.json = &bad;
  TEST_ASSERT_TRUE(decideMail(r, c).result.refusal == Refusal::OutOfBounds);
  // sending what is already set changes nothing: neither refused nor counted
  r.json = &t;
  TEST_ASSERT_TRUE(decideMail(r, c).result.ok());
  // the door test isn't limited
  FlatJson d = parsed("{}");
  MailRequest dr;
  dr.kind = MailKind::DoorTest;
  dr.json = &d;
  TEST_ASSERT_TRUE(decideMail(dr, c).result.ok());
  // the wait rounds up, and at 10 s it is allowed again
  c.nowMs = 59999;
  r.json = &t2;
  TEST_ASSERT_EQUAL_STRING("Changes from the dashboard are saved at most once every 10 s. Try again in 1 s.",
                           decideMail(r, c).result.message);
  c.nowMs = 60000;
  TEST_ASSERT_TRUE(decideMail(r, c).result.ok());
  // millis() wrapping past 49 days doesn't stop it
  SaveLimit w;
  w.saved(0xFFFFF000u);
  TEST_ASSERT_TRUE(w.waitS(0x00001000u) > 0);               // 8 s later, across the wrap
  TEST_ASSERT_EQUAL_UINT32(0, w.waitS(0x00002000u + 10000));
  // no limit given (nothing else counts): never refused
  c.limit = nullptr;
  c.nowMs = 60001;
  TEST_ASSERT_TRUE(decideMail(r, c).result.ok());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_one_request_at_a_time_and_answers_by_number);
  RUN_TEST(test_a_withdrawn_request_is_never_taken_and_a_taken_one_cannot_be_withdrawn);
  RUN_TEST(test_settings_decided_against_the_loops_own_settings);
  RUN_TEST(test_exclude_and_door_test);
  RUN_TEST(test_the_upload_gate_is_the_loops);
  RUN_TEST(test_one_saved_dashboard_change_every_ten_seconds);
  return UNITY_END();
}
