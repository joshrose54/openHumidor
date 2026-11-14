// OTA from the dashboard (lib/core ota_update, spec 5.2, 5.5): the writer (one 4 KB piece per loop
// pass, the 60 s stall, cut off, corrupt, cancelled), the loop's side (start, its own 70 s watch,
// end, restart), the boot record (waiting, rolled back, interrupted, self-rollback), the install
// through the mailbox, the image's own checksum on real images, and the identity block made at
// compile time agreeing with the version string.
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "wineador/ota_update.h"
#include "wineador/ota_rules.h"
#include "wineador/build_info.h"
#include "wineador/settings.h"
#include "wineador/event_text.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------- fakes
struct Flash : OtaFlash {
  std::vector<uint8_t> img;
  std::vector<size_t> writes;
  bool begun = false, aborted = false, booted = false, failWrite = false;
  End endsWith = End::Ok;
  bool begin(uint32_t) override { begun = true; return true; }
  bool write(const uint8_t* p, size_t n) override {
    TEST_ASSERT_TRUE(n <= kOtaPiece);
    TEST_ASSERT_EQUAL_UINT32(0, img.size() % kOtaPiece);   // every piece starts on a sector
    if (failWrite) return false;
    img.insert(img.end(), p, p + n);
    writes.push_back(n);
    return true;
  }
  End end() override { return endsWith; }
  void abort() override { aborted = true; }
  bool setBoot() override { booted = true; return true; }
};

struct Pacer : OtaPacer {
  int passes = 0, cancelAfter = -1;
  uint32_t last = 0;
  bool nextPass() override { passes++; return cancelAfter < 0 || passes <= cancelAfter; }
  void progress(uint32_t b) override { last = b; }
};

struct Body : BodyReader {
  std::string data;
  size_t at = 0, stallAt = SIZE_MAX, chunk = 1000;
  bool cutShort = false;
  int read(char* p, size_t n, uint32_t) override {
    if (at >= stallAt) return -1;
    if (at >= data.size()) return 0;
    size_t k = std::min(std::min(n, chunk), data.size() - at);
    if (stallAt != SIZE_MAX && at + k > stallAt) k = stallAt - at;
    memcpy(p, data.data() + at, k);
    at += k;
    return (int)k;
  }
};

static std::string bytes(size_t n) {
  std::string s(n, 0);
  for (size_t i = 0; i < n; i++) s[i] = (char)(i * 7 + 3);
  return s;
}

static uint8_t gPiece[kOtaPiece];

static OtaOutcome write(const std::string& image, Body& body, Flash& f, Pacer& p, size_t headN = kImageCheckBytes) {
  body.data = image.substr(headN);
  return runOtaWrite((const uint8_t*)image.data(), headN, body, (uint32_t)image.size(), f, p, gPiece);
}

// ---------------------------------------------------------------- the writer
void test_the_image_goes_in_whole_one_piece_per_loop_pass() {
  const std::string img = bytes(3 * kOtaPiece + 1234);
  Flash f; Pacer p; Body b;
  TEST_ASSERT_TRUE(write(img, b, f, p) == OtaOutcome::Installed);
  TEST_ASSERT_TRUE(f.img == std::vector<uint8_t>(img.begin(), img.end()));
  TEST_ASSERT_EQUAL_INT(4, (int)f.writes.size());          // 4 KB, 4 KB, 4 KB, the rest
  TEST_ASSERT_EQUAL_UINT32(1234, f.writes.back());
  TEST_ASSERT_EQUAL_INT(1 + 4 + 2, p.passes);              // before the start, each piece, the check, the boot setting
  TEST_ASSERT_EQUAL_UINT32(img.size(), p.last);
  TEST_ASSERT_TRUE(f.booted);
  TEST_ASSERT_FALSE(f.aborted);
}

void test_stalled_cut_off_corrupt_cancelled_and_failed() {
  const std::string img = bytes(5 * kOtaPiece);
  { Flash f; Pacer p; Body b; b.stallAt = 6000;             // no more data: 60 s
    TEST_ASSERT_TRUE(write(img, b, f, p) == OtaOutcome::Stalled);
    TEST_ASSERT_TRUE(f.aborted); TEST_ASSERT_FALSE(f.booted); }
  { Flash f; Pacer p; Body b;                               // the body ends short of Content-Length
    b.data = img.substr(kImageCheckBytes, 9000);
    TEST_ASSERT_TRUE(runOtaWrite((const uint8_t*)img.data(), kImageCheckBytes, b, (uint32_t)img.size(), f, p, gPiece) ==
                     OtaOutcome::CutOff);
    TEST_ASSERT_TRUE(f.aborted); TEST_ASSERT_FALSE(f.booted); }
  { Flash f; Pacer p; Body b; f.endsWith = OtaFlash::End::Corrupt;   // its own check fails
    TEST_ASSERT_TRUE(write(img, b, f, p) == OtaOutcome::Corrupt);
    TEST_ASSERT_FALSE(f.booted); }
  { Flash f; Pacer p; Body b; p.cancelAfter = 3;            // the loop ended it (its 70 s watch)
    TEST_ASSERT_TRUE(write(img, b, f, p) == OtaOutcome::Cancelled);
    TEST_ASSERT_TRUE(f.aborted); TEST_ASSERT_EQUAL_INT(2, (int)f.writes.size()); }
  { Flash f; Pacer p; Body b; f.failWrite = true;
    TEST_ASSERT_TRUE(write(img, b, f, p) == OtaOutcome::FlashFailed);
    TEST_ASSERT_TRUE(f.aborted); }
  TEST_ASSERT_TRUE(otaOutcomeRefusal(OtaOutcome::Corrupt).refusal == Refusal::CorruptImage);
  TEST_ASSERT_TRUE(otaOutcomeRefusal(OtaOutcome::CutOff).refusal == Refusal::Stalled);
  TEST_ASSERT_TRUE(otaOutcomeRefusal(OtaOutcome::Installed).ok());
}

// ---------------------------------------------------------------- the loop's side
void test_the_loop_starts_it_watches_it_and_restarts() {
  UpdateWindow w;
  OtaLoop L;
  // closed window, compressor running, being checked: refused, window untouched
  TEST_ASSERT_TRUE(L.begin(1000, 1385360, "2.0.1", false, w, 0).refusal == Refusal::WindowClosed);
  w.open(1000);
  TEST_ASSERT_TRUE(L.begin(2000, 1385360, "2.0.1", true, w, 0).refusal == Refusal::CompressorRunning);
  const ApiResult c = L.begin(2000, 1385360, "2.0.1", false, w, 42);
  TEST_ASSERT_TRUE(c.refusal == Refusal::BeingChecked);
  TEST_ASSERT_EQUAL_UINT32(42, c.retryS);
  TEST_ASSERT_EQUAL_STRING("Last update still being checked. Try again in 42 s.", c.message);
  TEST_ASSERT_TRUE(w.active(2000));
  // started: Updating, the window used up
  TEST_ASSERT_TRUE(L.begin(3000, 1385360, "2.0.1", false, w, 0).ok());
  TEST_ASSERT_TRUE(L.updating());
  TEST_ASSERT_FALSE(w.active(3000));
  TEST_ASSERT_TRUE(L.begin(3000, 1385360, "2.0.1", false, w, 0).refusal == Refusal::AlreadyUpdating);
  // progress keeps it alive; 70 s without: the loop ends it
  TEST_ASSERT_FALSE(L.watch(60000, 500000));
  TEST_ASSERT_EQUAL_UINT8(36, L.pct());
  TEST_ASSERT_FALSE(L.watch(129999, 500000));
  TEST_ASSERT_TRUE(L.watch(130000, 500000));
  TEST_ASSERT_FALSE(L.updating());
  TEST_ASSERT_TRUE(L.cancelled());
  L.end(130500, OtaOutcome::Cancelled);                    // the writer's late end changes nothing
  TEST_ASSERT_FALSE(L.updating());
  // a good one: installed -> restarting, due 1.5 s later
  w.open(200000);
  TEST_ASSERT_TRUE(L.begin(200000, 1000, "2.0.1", false, w, 0).ok());
  L.end(210000, OtaOutcome::Installed);
  TEST_ASSERT_TRUE(L.phase() == OtaLoop::Phase::Restarting);
  TEST_ASSERT_TRUE(L.updating());                          // loads stay off until the restart
  TEST_ASSERT_FALSE(L.restartDue(211499));
  TEST_ASSERT_TRUE(L.restartDue(211500));
}

// ---------------------------------------------------------------- the boot record
void test_the_boot_record_tells_what_happened() {
  OtaRecord started = otaRecordStarted("2.0.1", "2.0.0", 1794571200, 1);
  OtaRecord installed = otaRecordEnded(started, OtaOutcome::Installed, 1794571260);
  OtaBootFacts f;
  // the new image's first boot: pending, waiting to start properly
  f.running = RunningState::PendingVerify;
  f.runningVersion = "2.0.1";
  OtaBoot b = readOtaBoot(installed, f);
  TEST_ASSERT_TRUE(b.pendingVerify);
  TEST_ASSERT_TRUE(b.logEvent);
  TEST_ASSERT_TRUE(eventIs(kEvTypeOta, b.event, kEventTexts[0]) || strstr(b.event, kEvOtaWaiting) == b.event);
  // a reset before it was marked good: the bootloader went back to 2.0.0
  f.running = RunningState::Valid;
  f.runningVersion = "2.0.0";
  f.haveInvalid = true;
  f.invalidVersion = "2.0.1";
  b = readOtaBoot(installed, f);
  TEST_ASSERT_FALSE(b.pendingVerify);
  TEST_ASSERT_TRUE(b.last == UpdateResult::RolledBack);
  TEST_ASSERT_EQUAL_STRING("OTA ROLLED BACK from 2.0.1 to 2.0.0: it didn't start properly", b.event);
  TEST_ASSERT_EQUAL_STRING("2.0.1 didn't start; back on 2.0.0", b.lastDetail);   // the page's words
  TEST_ASSERT_TRUE(b.saveRecord);
  TEST_ASSERT_EQUAL_UINT8(OtaRecord::Done, b.record.step);
  // ... and the next boot logs nothing more
  TEST_ASSERT_FALSE(readOtaBoot(b.record, f).logEvent);
  // gave up after 4 min 30 s
  b = readOtaBoot(otaRecordSelfRollback(installed, 1794571600), f);
  TEST_ASSERT_EQUAL_STRING("OTA ROLLED BACK from 2.0.1 to 2.0.0: not started properly within 4 min 30 s", b.event);
  // a power cut or reset during the write: still the old image, nothing changed
  f.haveInvalid = false;
  b = readOtaBoot(started, f);
  TEST_ASSERT_TRUE(b.last == UpdateResult::Failed);
  TEST_ASSERT_TRUE(strstr(b.event, kEvOtaInterrupted) == b.event);
  TEST_ASSERT_EQUAL_STRING("2.0.1: cut off by a restart; still 2.0.0", b.lastDetail);
  // marked good, then restarted before the record said so: quietly done
  f.runningVersion = "2.0.1";
  b = readOtaBoot(installed, f);
  TEST_ASSERT_FALSE(b.logEvent);
  TEST_ASSERT_TRUE(b.last == UpdateResult::Installed);
  TEST_ASSERT_EQUAL_STRING("2.0.1 (was 2.0.0)", b.lastDetail);
  // the same version over itself (B7 step 4): told apart by slot, not by version
  OtaRecord same = otaRecordEnded(otaRecordStarted("2.0.0", "2.0.0", 1794571200, 1), OtaOutcome::Installed, 1794571260);
  OtaBootFacts s;
  s.running = RunningState::Valid;
  s.runningVersion = "2.0.0";
  s.runningSlot = 0;                                       // back on the old slot: a rollback
  b = readOtaBoot(same, s);
  TEST_ASSERT_TRUE(b.last == UpdateResult::RolledBack);
  TEST_ASSERT_TRUE(b.logEvent);
  s.runningSlot = 1;                                       // on the new slot, marked good: fine
  b = readOtaBoot(same, s);
  TEST_ASSERT_TRUE(b.last == UpdateResult::Installed);
  TEST_ASSERT_FALSE(b.logEvent);
  // nothing ever: nothing to say
  b = readOtaBoot(OtaRecord{}, f);
  TEST_ASSERT_FALSE(b.logEvent);
  TEST_ASSERT_FALSE(b.saveRecord);
  TEST_ASSERT_TRUE(b.last == UpdateResult::None);
}

void test_started_properly_countdown_and_deadline() {
  StartedProperly s;
  s.pass(1000, true, true, false);
  TEST_ASSERT_EQUAL_UINT32(270, s.leftS(1000));            // a condition unmet: to the 4 min 30 s deadline
  s.pass(31000, true, true, true);
  TEST_ASSERT_EQUAL_UINT32(30, s.leftS(31000));            // only the 60 s left
  s.pass(61000, true, true, true);
  TEST_ASSERT_TRUE(s.good());
  TEST_ASSERT_EQUAL_UINT32(0, s.leftS(61000));
  TEST_ASSERT_FALSE(s.overdue(400000));
  StartedProperly never;
  never.pass(0, false, true, true);                        // settings check failing
  never.pass(269999, false, true, true);
  TEST_ASSERT_FALSE(never.overdue(269999));
  TEST_ASSERT_TRUE(never.overdue(270000));                  // 4 min 30 s: inside the 5-minute boot hold
  TEST_ASSERT_TRUE(kSelfRollbackMs < settingsFor(Profile::Real).safety.bootHoldMs);
}

// ---------------------------------------------------------------- the install, through the mailbox
struct Backend : WebBackend {
  std::vector<MailKind> asked;
  std::vector<std::string> refused;
  ApiResult beginWith;
  uint8_t endedWith = 255;
  void status(StatusSnapshot*) override {}
  void settings(UserSettings*, TuningSettings*) override {}
  void updateState(UpdateState*) override {}
  History& history() override { static History h; return h; }
  void lockData() override {}
  void unlockData() override {}
  int recentEvents(EventRow*, int) override { return 0; }
  bool card(CardFacts*) override { return true; }
  int listLogs(LogFileEntry*, int) override { return 0; }
  int readLog(const char*, uint64_t, char*, size_t, uint64_t*) override { return -2; }
  bool ask(MailRequest* r, MailAnswer* a) override {
    asked.push_back(r->kind);
    if (r->kind == MailKind::UpdateEnd) endedWith = r->outcome;
    a->result = r->kind == MailKind::UpdateBegin ? beginWith : ApiResult{};
    return true;
  }
  Refusal startTransfer(TransferKind) override { return Refusal::None; }
  void endTransfer() override {}
  bool transferBusy() override { return false; }
  ApiResult install(const ImageFacts&, const uint8_t*, size_t, uint32_t, BodyReader&, uint64_t*, uint8_t*) override {
    return ApiResult{};
  }
  void noteRefused(const ApiResult& r) override { refused.push_back(refusalCode(r.refusal)); }
};

void test_the_install_starts_writes_and_ends_through_the_loop() {
  const std::string img = bytes(2 * kOtaPiece);
  ImageFacts facts;
  snprintf(facts.version, sizeof(facts.version), "2.0.1");
  { Backend be; Flash f; Pacer p; Body b; b.data = img.substr(kImageCheckBytes);
    uint64_t left = b.data.size();
    const ApiResult r = runOtaInstall(be, facts, (const uint8_t*)img.data(), kImageCheckBytes, (uint32_t)img.size(), b,
                                      &left, gPiece, f, p);
    TEST_ASSERT_TRUE(r.ok());
    TEST_ASSERT_EQUAL_INT(2, (int)be.asked.size());
    TEST_ASSERT_TRUE(be.asked[0] == MailKind::UpdateBegin && be.asked[1] == MailKind::UpdateEnd);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)OtaOutcome::Installed, be.endedWith);
    TEST_ASSERT_EQUAL_UINT64(0, left);
    TEST_ASSERT_TRUE(be.refused.empty()); }
  { Backend be; Flash f; Pacer p; Body b; b.data = img.substr(kImageCheckBytes);   // refused at the start: nothing written, logged
    be.beginWith = refuse(Refusal::CompressorRunning);
    uint64_t left = b.data.size();
    const ApiResult r = runOtaInstall(be, facts, (const uint8_t*)img.data(), kImageCheckBytes, (uint32_t)img.size(), b,
                                      &left, gPiece, f, p);
    TEST_ASSERT_TRUE(r.refusal == Refusal::CompressorRunning);
    TEST_ASSERT_FALSE(f.begun);
    TEST_ASSERT_EQUAL_INT(1, (int)be.refused.size());
    TEST_ASSERT_EQUAL_UINT64(b.data.size(), left); }       // the caller reads the rest away
}

// ---------------------------------------------------------------- real images
static bool readAll(const char* path, std::vector<uint8_t>* out) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  out->clear();
  uint8_t b[65536];
  size_t k;
  while ((k = fread(b, 1, sizeof(b), f)) > 0) out->insert(out->end(), b, b + k);
  fclose(f);
  return true;
}

void test_the_images_own_checksum_on_the_real_images() {
  std::vector<uint8_t> img;
  TEST_ASSERT_TRUE_MESSAGE(readAll(".pio.nosync/images/revq.bin", &img), "no .pio.nosync/images/revq.bin: build revq first");
  TEST_ASSERT_TRUE(imageChecksumOk(img.data(), img.size()));
  for (const char* p : {".pio.nosync/images/revq_bench.bin", ".pio.nosync/images/revq_rollback_test.bin",
                        ".pio.nosync/images/bp2_15_boot_state.bin"})
    if (readAll(p, &img)) TEST_ASSERT_TRUE_MESSAGE(imageChecksumOk(img.data(), img.size()), p);
  if (readAll(".pio.nosync/images/revq-corrupt.bin", &img)) {   // tools/make_bad_image.py
    TEST_ASSERT_FALSE(imageChecksumOk(img.data(), img.size()));
    TEST_ASSERT_TRUE(checkImage(img.data(), img.size()) == ImageVerdict::Accept);   // its identity is intact
  }
  // a byte changed anywhere in a segment is caught
  readAll(".pio.nosync/images/revq.bin", &img);
  img[img.size() / 3] ^= 0x40;
  TEST_ASSERT_FALSE(imageChecksumOk(img.data(), img.size()));
}

// ---------------------------------------------------------------- the identity block, made at compile time
void test_the_compile_time_block_matches_the_version_string() {
  for (int bench = 0; bench < 2; bench++)
    for (int test = 0; test < 2; test++)
      for (int rb = 0; rb < 2; rb++) {
        if (rb && (bench || test)) continue;   // the rollback image is real profile, not a test build
        const ImageIdentityBlock b = identityBlockFor("rev-q", bench, test, rb, "2.0.0", 3);
        char v[40];
        makeVersionString(v, sizeof(v), "2.0.0", bench ? Profile::Bench : Profile::Real, test, rb);
        TEST_ASSERT_EQUAL_STRING(v, b.version);
        ImageIdentityBlock m;
        makeIdentityBlock(&m, "rev-q", bench, test, v, 3);
        TEST_ASSERT_EQUAL_MEMORY(&m, &b, sizeof(b));
        uint8_t image[kImageCheckBytes] = {};
        image[0] = kImageMagic; image[12] = 9;
        image[0x20] = 0x32; image[0x21] = 0x54; image[0x22] = 0xCD; image[0x23] = 0xAB;
        memcpy(image + kIdentityOffset, &b, sizeof(b));
        const ImageVerdict want = bench ? ImageVerdict::BenchBuild : test ? ImageVerdict::TestBuild : ImageVerdict::Accept;
        TEST_ASSERT_TRUE(checkImage(image, sizeof(image)) == want);
      }
  constexpr ImageIdentityBlock k = identityBlockFor("rev-q", false, false, false, "2.0.0", 3);   // a constant
  static_assert(k.layout == kIdentityLayout, "made at compile time");
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_image_goes_in_whole_one_piece_per_loop_pass);
  RUN_TEST(test_stalled_cut_off_corrupt_cancelled_and_failed);
  RUN_TEST(test_the_loop_starts_it_watches_it_and_restarts);
  RUN_TEST(test_the_boot_record_tells_what_happened);
  RUN_TEST(test_started_properly_countdown_and_deadline);
  RUN_TEST(test_the_install_starts_writes_and_ends_through_the_loop);
  RUN_TEST(test_the_images_own_checksum_on_the_real_images);
  RUN_TEST(test_the_compile_time_block_matches_the_version_string);
  return UNITY_END();
}
