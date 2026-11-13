// OTA, plain logic (spec 5.2, 5.5): which build an image is, from its first bytes, before
// anything is written — only a Rev Q cabinet build is accepted; the Allow update gate; the 60 s
// stall; "started properly"; and the one-long-transfer gate with the internal RAM margin.
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <vector>
#include "wineador/image_identity.h"
#include "wineador/ota_rules.h"
#include "wineador/logbook.h"
#include "wineador/lcd_ui.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

// A made-up image: the header, the app description's magic, and (optionally) an identity block.
static std::vector<uint8_t> image(uint16_t chip, bool appDesc, const ImageIdentityBlock* id, size_t n = 512) {
  std::vector<uint8_t> b(n, 0);
  b[0] = 0xE9;
  b[1] = 6;
  b[12] = (uint8_t)(chip & 0xFF);
  b[13] = (uint8_t)(chip >> 8);
  if (appDesc && n >= 0x24) { b[0x20] = 0x32; b[0x21] = 0x54; b[0x22] = 0xCD; b[0x23] = 0xAB; }
  if (id && n >= kImageCheckBytes) memcpy(b.data() + kIdentityOffset, id, sizeof(*id));
  return b;
}

static ImageVerdict verdict(const std::vector<uint8_t>& b) { return checkImage(b.data(), b.size()); }

void test_the_identity_block_layout() {
  TEST_ASSERT_EQUAL_UINT32(0x120, kIdentityOffset);
  TEST_ASSERT_EQUAL_UINT32(352, kImageCheckBytes);
  TEST_ASSERT_EQUAL_UINT32(16, offsetof(ImageIdentityBlock, board));
  TEST_ASSERT_EQUAL_UINT32(24, offsetof(ImageIdentityBlock, profile));
  TEST_ASSERT_EQUAL_UINT32(32, offsetof(ImageIdentityBlock, testBuild));
  TEST_ASSERT_EQUAL_UINT32(33, offsetof(ImageIdentityBlock, layout));
  TEST_ASSERT_EQUAL_UINT32(34, offsetof(ImageIdentityBlock, apiLo));
  TEST_ASSERT_EQUAL_UINT32(36, offsetof(ImageIdentityBlock, version));
}

void test_only_a_rev_q_cabinet_build_is_accepted() {
  ImageIdentityBlock id;
  makeIdentityBlock(&id, "rev-q", false, false, "2.0.0", 1);
  const std::vector<uint8_t> ok = image(kChipEsp32S3, true, &id);
  TEST_ASSERT_TRUE(verdict(ok) == ImageVerdict::Accept);
  const ImageFacts f = readImageFacts(ok.data(), ok.size());
  TEST_ASSERT_EQUAL_STRING("rev-q", f.board);
  TEST_ASSERT_EQUAL_STRING("real", f.profile);
  TEST_ASSERT_EQUAL_STRING("2.0.0", f.version);
  TEST_ASSERT_EQUAL_UINT16(1, f.api);
  TEST_ASSERT_FALSE(f.testBuild);
}

void test_every_other_image_is_refused_with_its_reason() {
  ImageIdentityBlock bench, test, benchTest, revnBlock, newer;
  makeIdentityBlock(&bench, "rev-q", true, false, "2.0.0-BENCH", 1);
  makeIdentityBlock(&test, "rev-q", false, true, "2.0.0-TEST", 1);
  makeIdentityBlock(&benchTest, "rev-q", true, true, "2.0.0-BENCH-TEST", 1);
  makeIdentityBlock(&revnBlock, "rev-n", false, false, "1.0.0", 1);
  makeIdentityBlock(&newer, "rev-q", false, false, "3.0.0", 1);
  newer.layout = 2;
  ImageIdentityBlock good;
  makeIdentityBlock(&good, "rev-q", false, false, "2.0.0", 1);
  TEST_ASSERT_TRUE(verdict(image(kChipEsp32S3, true, &bench)) == ImageVerdict::BenchBuild);
  TEST_ASSERT_TRUE(verdict(image(kChipEsp32S3, true, &test)) == ImageVerdict::TestBuild);
  TEST_ASSERT_TRUE(verdict(image(kChipEsp32S3, true, &benchTest)) == ImageVerdict::BenchBuild);
  TEST_ASSERT_TRUE(verdict(image(kChipEsp32S3, true, &revnBlock)) == ImageVerdict::WrongBoard);
  TEST_ASSERT_TRUE(verdict(image(kChipEsp32S3, true, &newer)) == ImageVerdict::UnknownLayout);
  TEST_ASSERT_TRUE(verdict(image(kChipEsp32, true, &good)) == ImageVerdict::RevNImage);   // the chip says Rev N
  TEST_ASSERT_TRUE(verdict(image(kChipEsp32, true, nullptr)) == ImageVerdict::RevNImage);
  TEST_ASSERT_TRUE(verdict(image(5, true, &good)) == ImageVerdict::WrongChip);
  TEST_ASSERT_TRUE(verdict(image(kChipEsp32S3, false, &good)) == ImageVerdict::NoAppDesc);
  TEST_ASSERT_TRUE(verdict(image(kChipEsp32S3, true, nullptr)) == ImageVerdict::NoIdentity);   // BP2 sketch, pre-Q5
  std::vector<uint8_t> notImage = image(kChipEsp32S3, true, &good);
  notImage[0] = 'P';                                            // e.g. a .zip or a text file
  TEST_ASSERT_TRUE(verdict(notImage) == ImageVerdict::NotFirmware);
  TEST_ASSERT_TRUE(verdict(image(kChipEsp32S3, true, &good, kImageCheckBytes - 1)) == ImageVerdict::TooShort);
  TEST_ASSERT_TRUE(checkImage(nullptr, 0) == ImageVerdict::TooShort);
  // a block whose magic is nearly right is not a block
  ImageIdentityBlock wrongMagic = good;
  wrongMagic.magic[0] = 'O';
  TEST_ASSERT_TRUE(verdict(image(kChipEsp32S3, true, &wrongMagic)) == ImageVerdict::NoIdentity);
  // a block with every text field full (no NUL) is still read safely
  ImageIdentityBlock full = good;
  memset(full.version, 'x', sizeof(full.version));
  memset(full.board, 'y', sizeof(full.board));
  const std::vector<uint8_t> fi = image(kChipEsp32S3, true, &full);
  const ImageFacts ff = readImageFacts(fi.data(), fi.size());
  TEST_ASSERT_EQUAL_UINT32(sizeof(ff.version) - 1, strlen(ff.version));
  TEST_ASSERT_TRUE(judgeImage(ff) == ImageVerdict::WrongBoard);
  // every verdict has words and a code
  for (int v = 0; v <= (int)ImageVerdict::TestBuild; v++) {
    TEST_ASSERT_TRUE(strlen(imageVerdictText((ImageVerdict)v)) > 10);
    TEST_ASSERT_TRUE(strlen(imageVerdictCode((ImageVerdict)v)) > 3);
  }
}

// The real images from the builds, when they have been built: Rev N's is refused by its chip;
// Rev Q's cabinet build is accepted (its identity block, stage Q5d); a BP2 sketch never.
static bool readFile(const char* path, std::vector<uint8_t>* out) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  out->assign(kImageCheckBytes, 0);
  const size_t k = fread(out->data(), 1, kImageCheckBytes, f);
  fclose(f);
  out->resize(k);
  return true;
}

void test_the_real_images_from_the_builds() {
  std::vector<uint8_t> b;
  int seen = 0;
  if (readFile(".pio.nosync/images/esp32dev.bin", &b)) {
    seen++;
    TEST_ASSERT_TRUE(verdict(b) == ImageVerdict::RevNImage);
  }
  if (readFile(".pio.nosync/images/revq.bin", &b)) {
    seen++;
    const ImageVerdict v = verdict(b);
    TEST_ASSERT_TRUE(v == ImageVerdict::Accept);             // stage Q5d: the cabinet build carries its block
    TEST_ASSERT_EQUAL_UINT16(kChipEsp32S3, readImageFacts(b.data(), b.size()).chip);
  }
  if (readFile(".pio.nosync/images/bp2_15_boot_state.bin", &b)) {
    seen++;
    TEST_ASSERT_TRUE(verdict(b) == ImageVerdict::NoIdentity);   // a BP2 sketch: never
  }
  if (seen == 0) TEST_IGNORE_MESSAGE("no .pio.nosync/images/*.bin yet: build esp32dev and revq, then run again");
}

// ---------------------------------------------------------------- the Allow update gate
void test_update_gate_order_and_reasons() {
  UpdateGateInputs in;
  in.windowOpen = true;
  in.length = 1200000;
  in.slotBytes = 3342336;
  TEST_ASSERT_TRUE(checkUpdateStart(in) == Refusal::None);
  UpdateGateInputs x = in;
  x.updating = true; x.windowOpen = false;
  TEST_ASSERT_TRUE(checkUpdateStart(x) == Refusal::AlreadyUpdating);
  x = in; x.windowOpen = false; x.compressorRunning = true;
  TEST_ASSERT_TRUE(checkUpdateStart(x) == Refusal::WindowClosed);
  x = in; x.compressorRunning = true;
  TEST_ASSERT_TRUE(checkUpdateStart(x) == Refusal::CompressorRunning);
  x = in; x.transferBusy = true;
  TEST_ASSERT_TRUE(checkUpdateStart(x) == Refusal::Busy);
  x = in; x.length = 3342337;
  TEST_ASSERT_TRUE(checkUpdateStart(x) == Refusal::TooBig);
  x = in; x.length = 3342336;
  TEST_ASSERT_TRUE(checkUpdateStart(x) == Refusal::None);
  x = in; x.length = 100;
  TEST_ASSERT_TRUE(checkUpdateStart(x) == Refusal::WrongImage);
  x = in; x.slotBytes = 0;
  TEST_ASSERT_TRUE(checkUpdateStart(x) == Refusal::TooBig);
  TEST_ASSERT_EQUAL_INT(403, httpStatus(Refusal::WindowClosed));
  TEST_ASSERT_EQUAL_INT(409, httpStatus(Refusal::CompressorRunning));
  TEST_ASSERT_EQUAL_INT(422, httpStatus(Refusal::WrongImage));
}

void test_a_refused_upload_leaves_the_window_open() {
  // 5.5: a refused upload doesn't use the window up; the loop consumes it only when an update starts.
  UpdateWindow w;
  w.open(1000);
  UpdateGateInputs in;
  in.windowOpen = w.active(2000);
  in.compressorRunning = true;
  in.length = 1200000;
  in.slotBytes = 3342336;
  TEST_ASSERT_TRUE(checkUpdateStart(in) == Refusal::CompressorRunning);
  TEST_ASSERT_TRUE(w.active(3000));
  TEST_ASSERT_TRUE(w.consume(4000));      // the next, accepted, one
  TEST_ASSERT_FALSE(w.active(5000));
}

void test_a_wrong_image_says_why() {
  const ApiResult r = refuseImage(ImageVerdict::BenchBuild);
  TEST_ASSERT_TRUE(r.refusal == Refusal::WrongImage);
  char out[400];
  TEST_ASSERT_TRUE(formatError(out, sizeof(out), r) > 0);
  TEST_ASSERT_EQUAL_STRING("{\"api\":4,\"error\":\"wrong_image\",\"message\":\"That is a bench build (short timers): "
                           "never for the cabinet.\",\"image\":\"bench_build\"}", out);
}

// ---------------------------------------------------------------- the stall
void test_an_upload_stalls_after_60s_without_data() {
  UploadWatch u;
  TEST_ASSERT_FALSE(u.stalled(0));
  u.begin(1000, 1000000);
  u.data(2000, 250000);
  TEST_ASSERT_EQUAL_UINT8(25, u.percent());
  TEST_ASSERT_FALSE(u.stalled(61999));
  TEST_ASSERT_TRUE(u.stalled(62000));       // 60 s after the last data
  u.data(62000, 0);                          // nothing arrived: still stalled
  TEST_ASSERT_TRUE(u.stalled(62000));
  u.data(62000, 750000);
  TEST_ASSERT_FALSE(u.stalled(62000));
  TEST_ASSERT_EQUAL_UINT8(100, u.percent());
  u.end();
  TEST_ASSERT_FALSE(u.stalled(999999));
  // across the millis() wrap
  u.begin(0xFFFFF000u, 10);
  TEST_ASSERT_FALSE(u.stalled(0x00001000u));
  TEST_ASSERT_TRUE(u.stalled(0xFFFFF000u + 60000u));
}

// ---------------------------------------------------------------- started properly
void test_started_properly_after_60s_with_all_three() {
  StartedProperly s;
  char why[48];
  s.pass(5000, true, true, false);
  TEST_ASSERT_EQUAL_STRING("first sensor read not done", s.waitingFor(5000, why, sizeof(why)));
  s.pass(20000, true, true, true);
  TEST_ASSERT_EQUAL_STRING("running 15 s of 60", s.waitingFor(20000, why, sizeof(why)));
  s.pass(64999, true, true, true);
  TEST_ASSERT_FALSE(s.good());
  s.pass(65000, true, true, true);
  TEST_ASSERT_TRUE(s.good());
  s.pass(70000, false, false, false);        // once good, stays good
  TEST_ASSERT_TRUE(s.good());
}

void test_never_good_with_bad_settings_or_a_still_watchdog() {
  StartedProperly s;
  char why[48];
  for (uint32_t t = 0; t <= 600000; t += 1000) s.pass(t, false, true, true);
  TEST_ASSERT_FALSE(s.good());
  TEST_ASSERT_EQUAL_STRING("settings check failed", s.waitingFor(600000, why, sizeof(why)));
  StartedProperly h;
  for (uint32_t t = 0; t <= 100000; t += 1000) h.pass(t, true, false, true);
  TEST_ASSERT_FALSE(h.good());
  TEST_ASSERT_EQUAL_STRING("hardware watchdog not toggling", h.waitingFor(100000, why, sizeof(why)));
  h.pass(101000, true, true, true);           // toggling again, and well past 60 s
  TEST_ASSERT_TRUE(h.good());
}

// ---------------------------------------------------------------- the transfer gate
void test_one_long_transfer_at_a_time_and_the_ram_margin() {
  TransferGate g;
  const uint32_t margin = 20000;
  TEST_ASSERT_TRUE(g.tryStart(TransferKind::Download, kHeapFloorBytes + margin - 1, margin) == Refusal::LowMemory);
  TEST_ASSERT_FALSE(g.busy());
  TEST_ASSERT_TRUE(g.tryStart(TransferKind::Download, kHeapFloorBytes + margin, margin) == Refusal::None);
  TEST_ASSERT_TRUE(g.busy());
  TEST_ASSERT_TRUE(g.kind() == TransferKind::Download);
  TEST_ASSERT_TRUE(g.tryStart(TransferKind::Update, 200000, margin) == Refusal::Busy);
  TEST_ASSERT_TRUE(g.tryStart(TransferKind::Download, 200000, margin) == Refusal::Busy);
  g.finish();
  TEST_ASSERT_TRUE(g.tryStart(TransferKind::Update, 200000, margin) == Refusal::None);
  TEST_ASSERT_TRUE(g.kind() == TransferKind::Update);
  g.finish();
  TEST_ASSERT_TRUE(g.tryStart(TransferKind::Update, kHeapFloorBytes, 0) == Refusal::None);   // margin 0: the floor
  TEST_ASSERT_EQUAL_INT(503, httpStatus(Refusal::LowMemory));
  TEST_ASSERT_EQUAL_INT(503, httpStatus(Refusal::Busy));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_identity_block_layout);
  RUN_TEST(test_only_a_rev_q_cabinet_build_is_accepted);
  RUN_TEST(test_every_other_image_is_refused_with_its_reason);
  RUN_TEST(test_the_real_images_from_the_builds);
  RUN_TEST(test_update_gate_order_and_reasons);
  RUN_TEST(test_a_refused_upload_leaves_the_window_open);
  RUN_TEST(test_a_wrong_image_says_why);
  RUN_TEST(test_an_upload_stalls_after_60s_without_data);
  RUN_TEST(test_started_properly_after_60s_with_all_three);
  RUN_TEST(test_never_good_with_bad_settings_or_a_still_watchdog);
  RUN_TEST(test_one_long_transfer_at_a_time_and_the_ram_margin);
  return UNITY_END();
}
