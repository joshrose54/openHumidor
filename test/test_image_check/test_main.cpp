// The board's image check (lib/core image_identity) and the Update page's early copy of it
// (web/image_check.js) must give the same verdict on every image, or the page could refuse good
// images (or wave through bad ones). This writes a set of images — every kind of good and bad one,
// each byte the check reads changed in turn, and the real built images when they exist — with the
// C++ verdict for each, then runs the page's check on them under node (tools/check_image_js.js).
// Needs node on the Mac. Run from the project folder (pio test does).
#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>
#include "wineador/image_identity.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

static const char* kDir = ".pio.nosync/image-check";

static std::vector<uint8_t> image(uint16_t chip, bool appDesc, const ImageIdentityBlock* id, size_t n = 512) {
  std::vector<uint8_t> b(n, 0);
  b[0] = 0xE9;
  b[12] = (uint8_t)(chip & 0xFF);
  b[13] = (uint8_t)(chip >> 8);
  if (appDesc && n >= 0x24) { b[0x20] = 0x32; b[0x21] = 0x54; b[0x22] = 0xCD; b[0x23] = 0xAB; }
  if (id && n >= kImageCheckBytes) memcpy(b.data() + kIdentityOffset, id, sizeof(*id));
  return b;
}

struct Set {
  std::string manifest = "[\n";
  int n = 0;
  void add(const char* name, const std::vector<uint8_t>& b) {
    char file[64];
    snprintf(file, sizeof(file), "%03d-%s.bin", n, name);
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", kDir, file);
    FILE* f = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    fwrite(b.data(), 1, b.size(), f);
    fclose(f);
    const char* code = imageVerdictCode(checkImage(b.data(), b.size()));
    char row[160];
    snprintf(row, sizeof(row), "%s  {\"file\": \"%s\", \"code\": \"%s\"}", n ? ",\n" : "", file, code);
    manifest += row;
    n++;
  }
};

static bool readFirst(const char* path, std::vector<uint8_t>* out) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  out->assign(kImageCheckBytes, 0);
  out->resize(fread(out->data(), 1, kImageCheckBytes, f));
  fclose(f);
  return true;
}

void test_the_page_and_the_board_agree_on_every_image() {
  mkdir(".pio.nosync", 0755);
  mkdir(kDir, 0755);
  Set s;
  ImageIdentityBlock good, bench, test, benchTest, revn, layout2;
  makeIdentityBlock(&good, "rev-q", false, false, "2.0.0", 2);
  makeIdentityBlock(&bench, "rev-q", true, false, "2.0.0-BENCH", 2);
  makeIdentityBlock(&test, "rev-q", false, true, "2.0.0-TEST", 2);
  makeIdentityBlock(&benchTest, "rev-q", true, true, "2.0.0-BENCH-TEST", 2);
  makeIdentityBlock(&revn, "rev-n", false, false, "1.0.0", 2);
  layout2 = good;
  layout2.layout = 2;
  s.add("good", image(kChipEsp32S3, true, &good));
  s.add("bench", image(kChipEsp32S3, true, &bench));
  s.add("test", image(kChipEsp32S3, true, &test));
  s.add("bench-test", image(kChipEsp32S3, true, &benchTest));
  s.add("rev-n-block", image(kChipEsp32S3, true, &revn));
  s.add("layout2", image(kChipEsp32S3, true, &layout2));
  s.add("rev-n-chip", image(kChipEsp32, true, &good));
  s.add("other-chip", image(5, true, &good));
  s.add("no-app-desc", image(kChipEsp32S3, false, &good));
  s.add("no-identity", image(kChipEsp32S3, true, nullptr));
  s.add("short", image(kChipEsp32S3, true, &good, kImageCheckBytes - 1));
  s.add("exact-length", image(kChipEsp32S3, true, &good, kImageCheckBytes));
  std::vector<uint8_t> notImage = image(kChipEsp32S3, true, &good);
  notImage[0] = 'P';
  s.add("not-image", notImage);
  // every byte the check reads, changed in turn (to two values), from the good image
  const std::vector<uint8_t> base = image(kChipEsp32S3, true, &good);
  std::vector<size_t> bytesRead = {0, 12, 13, 0x20, 0x21, 0x22, 0x23};
  for (size_t i = 0; i < kIdentityBytes; i++) bytesRead.push_back(kIdentityOffset + i);
  for (size_t at : bytesRead)
    for (uint8_t v : {(uint8_t)0x00, (uint8_t)0x41}) {
      std::vector<uint8_t> m = base;
      if (m[at] == v) continue;
      m[at] = v;
      char name[32];
      snprintf(name, sizeof(name), "byte%03zx-%02x", at, v);
      s.add(name, m);
    }
  // every real image built (tools/check_identity.py keeps a copy of each in .pio.nosync/images/),
  // each with the verdict it must get: build the boards first, then run this test
  struct Want { const char* env; ImageVerdict v; };
  const Want wants[] = {
    {"revq", ImageVerdict::Accept}, {"revq_rollback_test", ImageVerdict::Accept},
    {"revq_bench", ImageVerdict::BenchBuild}, {"revq_bench_test", ImageVerdict::BenchBuild},
    {"revq_bench_test_boot", ImageVerdict::BenchBuild}, {"revq_real_test", ImageVerdict::TestBuild},
    {"revq-corrupt", ImageVerdict::Accept},   // tools/make_bad_image.py: identity intact, refused at its own check
    {"esp32dev", ImageVerdict::RevNImage}, {"bench", ImageVerdict::RevNImage}, {"bench_test", ImageVerdict::RevNImage},
    {"bench_test_boot", ImageVerdict::RevNImage}, {"real_test", ImageVerdict::RevNImage},
    {"bp2_15_boot_state", ImageVerdict::NoIdentity}, {"bp2_18_outputs", ImageVerdict::NoIdentity},
    {"bp2_19_interlock", ImageVerdict::NoIdentity}, {"bp2_20_full_load", ImageVerdict::NoIdentity},
    {"bp2_21_wifi_rails", ImageVerdict::NoIdentity}, {"bp2_22_blowers", ImageVerdict::NoIdentity},
    {"bp2_23_blower_reset", ImageVerdict::NoIdentity}, {"bp2_24_watchdog", ImageVerdict::NoIdentity},
    {"bp2_26_sd_power", ImageVerdict::NoIdentity}, {"bp2_27_12v", ImageVerdict::NoIdentity},
    {"bp2_28_sensors", ImageVerdict::NoIdentity}, {"bp2_29_lcd_touch", ImageVerdict::NoIdentity},
    {"bp2_30_rtc", ImageVerdict::NoIdentity}, {"bp2_35_bulb", ImageVerdict::NoIdentity},
  };
  std::vector<uint8_t> real;
  int realSeen = 0;
  for (const Want& w : wants) {
    char p[96];
    snprintf(p, sizeof(p), ".pio.nosync/images/%s.bin", w.env);
    if (!readFirst(p, &real)) {
      if (strcmp(w.env, "revq") == 0) TEST_FAIL_MESSAGE("no .pio.nosync/images/revq.bin: build revq first, then run this test");
      continue;
    }
    char msg[160];
    snprintf(msg, sizeof(msg), "%s: %s, should be %s", w.env, imageVerdictCode(checkImage(real.data(), real.size())),
             imageVerdictCode(w.v));
    TEST_ASSERT_TRUE_MESSAGE(checkImage(real.data(), real.size()) == w.v, msg);
    printf("  real image %-22s %s\n", w.env, imageVerdictCode(w.v));
    s.add(w.env, real);
    realSeen++;
  }
  printf("  %d real images, each with its expected verdict\n", realSeen);
  s.manifest += "\n]\n";
  char path[128];
  snprintf(path, sizeof(path), "%s/manifest.json", kDir);
  FILE* f = fopen(path, "wb");
  fwrite(s.manifest.data(), 1, s.manifest.size(), f);
  fclose(f);
  // the page's copy, under node
  char cmd[200];
  snprintf(cmd, sizeof(cmd), "node tools/check_image_js.js %s 2>&1", kDir);
  FILE* p = popen(cmd, "r");
  TEST_ASSERT_NOT_NULL_MESSAGE(p, "can't run node");
  std::string out;
  char b[300];
  while (fgets(b, sizeof(b), p)) out += b;
  const int st = pclose(p);
  printf("%s", out.c_str());
  TEST_ASSERT_TRUE_MESSAGE(WIFEXITED(st) && WEXITSTATUS(st) == 0, out.c_str());
  TEST_ASSERT_TRUE(s.n > 80);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_page_and_the_board_agree_on_every_image);
  return UNITY_END();
}
