// The LCD screens (spec 5.5), drawn on the Mac by the same code the board runs (lib/core lcd_ui
// -> lib/lcdgfx), and saved as pictures for review:
//
//   .pio.nosync/lcd-screens/*.png      one per screen and state, 480 x 320, the panel's own colours
//   .pio.nosync/lcd-screens/index.html all of them on one page, with what each shows
//
// Also proves: redrawing only what changed ends with exactly the same pixels as drawing the
// whole screen; and no drawing step is larger than kMaxStepPixels (3A.11's 50 ms rule).
#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <sys/stat.h>
#include "lcdgfx/screens.h"
#include "wineador/uk_time.h"

using namespace wineador;
using namespace lcdgfx;

void setUp() {}
void tearDown() {}

static const char* kDir = ".pio.nosync/lcd-screens";

// ---------------------------------------------------------------- PNG (stored deflate: no zlib)
static uint32_t crcTable[256];
static void crcInit() {
  for (uint32_t n = 0; n < 256; n++) {
    uint32_t c = n;
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    crcTable[n] = c;
  }
}
static uint32_t crc(const uint8_t* b, size_t n, uint32_t c = 0xFFFFFFFFu) {
  for (size_t i = 0; i < n; i++) c = crcTable[(c ^ b[i]) & 0xFF] ^ (c >> 8);
  return c;
}
static void be32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 3; i >= 0; i--) v.push_back((uint8_t)(x >> (8 * i)));
}
static void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> c;
  be32(c, (uint32_t)data.size());
  c.insert(c.end(), type, type + 4);
  c.insert(c.end(), data.begin(), data.end());
  const uint32_t k = crc(c.data() + 4, c.size() - 4) ^ 0xFFFFFFFFu;
  be32(c, k);
  fwrite(c.data(), 1, c.size(), f);
}
// px: RGB565; dim: brightness factor (1 = as drawn; the dimmed picture is a simulation).
static void writePng(const char* path, const uint16_t* px, int w, int h, float dim = 1.0f) {
  std::vector<uint8_t> raw;
  for (int y = 0; y < h; y++) {
    raw.push_back(0);   // filter: none
    for (int x = 0; x < w; x++) {
      const uint16_t p = px[y * w + x];
      const uint8_t r5 = (p >> 11) & 31, g6 = (p >> 5) & 63, b5 = p & 31;
      raw.push_back((uint8_t)(((r5 << 3) | (r5 >> 2)) * dim));
      raw.push_back((uint8_t)(((g6 << 2) | (g6 >> 4)) * dim));
      raw.push_back((uint8_t)(((b5 << 3) | (b5 >> 2)) * dim));
    }
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
  for (size_t i = 0; i < raw.size(); i += 65535) {
    const size_t n = raw.size() - i < 65535 ? raw.size() - i : 65535;
    z.push_back(i + n == raw.size() ? 1 : 0);
    z.push_back((uint8_t)(n & 0xFF)); z.push_back((uint8_t)(n >> 8));
    z.push_back((uint8_t)(~n & 0xFF)); z.push_back((uint8_t)((~n >> 8) & 0xFF));
    z.insert(z.end(), raw.begin() + i, raw.begin() + i + n);
  }
  be32(z, (b << 16) | a);
  FILE* f = fopen(path, "wb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ihdr;
  be32(ihdr, (uint32_t)w); be32(ihdr, (uint32_t)h);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
  chunk(f, "IHDR", ihdr);
  chunk(f, "IDAT", z);
  chunk(f, "IEND", {});
  fclose(f);
}

// ---------------------------------------------------------------- the states
static uint16_t gFrame[kScreenW * kScreenH];
struct Shot { std::string file, title, note; };
static std::vector<Shot> gShots;

static const int64_t kSummerUtc = daysFromCivil(2026, 7, 14) * 86400 + 13 * 3600 + 32 * 60;   // 14:32 BST
static const int64_t kWinterUtc = daysFromCivil(2026, 11, 13) * 86400 + 14 * 3600 + 32 * 60;  // 14:32 GMT

static UiInputs base() {
  UiInputs in;
  in.nowMs = 2000000;
  in.uptimeMs = 2000000;
  in.cabinetOk = 4;
  in.fresh = true;
  in.meanC = 20.3f;
  in.meanRhPct = 62.4f;
  in.targetC = 20.0f;
  in.rhSetPct = 62.0f;
  in.utcNowS = kWinterUtc;
  in.wifi = true;
  in.alerts.sensorsKnown = true;
  return in;
}

static void shoot(const char* name, const char* title, const char* note, const UiView& v, float dim = 1.0f) {
  Canvas c(gFrame, kScreenW, kScreenH);
  paintScreen(c, v);
  char path[160];
  snprintf(path, sizeof(path), "%s/%s.png", kDir, name);
  writePng(path, gFrame, kScreenW, kScreenH, dim);
  gShots.push_back({std::string(name) + ".png", title, note});
}

static void shootInputs(const char* name, const char* title, const char* note, const UiInputs& in) {
  LcdUi ui;
  ui.begin(in.nowMs, 20);
  ui.update(in);
  shoot(name, title, note, ui.view());
}

static void tap(LcdUi& ui, UiInputs& in, Rect r) {
  in.nowMs += 300;
  in.uptimeMs += 300;
  ui.touch(true, r.x + r.w / 2, r.y + r.h / 2, in.nowMs);
  ui.update(in);
  in.nowMs += 100;
  in.uptimeMs += 100;
  ui.touch(false, 0, 0, in.nowMs);
  ui.update(in);
}

static AlertInputs allAlerts() {
  AlertInputs a;
  a.sensorsKnown = true;
  a.sensorFault = SensorFault::Chamber;
  a.fanStalled = true;
  a.settingsBad = true;
  a.dip12v = true;
  a.heaterIneffective = a.coolingIneffective = a.humidIneffective = true;
  a.excludedMask = 0x3F;
  a.manualMask = 0x04;
  a.sdFailed = true;
  a.clockBad = true;
  return a;
}

static void writeIndex() {
  char path[160];
  snprintf(path, sizeof(path), "%s/index.html", kDir);
  FILE* f = fopen(path, "w");
  TEST_ASSERT_NOT_NULL(f);
  fprintf(f, "<!doctype html><meta charset=utf-8><title>Wineador LCD screens</title>"
             "<style>body{background:#222;color:#eee;font:14px system-ui;margin:24px}"
             "figure{display:inline-block;margin:0 24px 28px 0;vertical-align:top;max-width:480px}"
             "img{width:480px;height:320px;image-rendering:pixelated;border:1px solid #555}"
             "figcaption b{display:block;margin:6px 0 2px}</style>"
             "<h1>Wineador LCD screens (stage Q4)</h1><p>Drawn on the Mac by the firmware's own drawing code "
             "(lib/lcdgfx), 480 x 320, in the panel's 16-bit colours. Made by <code>pio test -e native -f "
             "test_lcd_screens</code>.</p>\n");
  for (const Shot& s : gShots)
    fprintf(f, "<figure><img src=\"%s\" alt=\"%s\"><figcaption><b>%s</b>%s<br><small>%s</small></figcaption></figure>\n",
            s.file.c_str(), s.title.c_str(), s.title.c_str(), s.note.c_str(), s.file.c_str());
  fclose(f);
}

void test_render_every_screen() {
  crcInit();
  mkdir(".pio.nosync", 0755);
  mkdir(kDir, 0755);
  gShots.clear();

  // ---- walk-past: each mode alone, and two at once
  UiInputs in = base();
  shootInputs("01-standby", "Standby", "Nothing running. Time 14:32 (GMT, November).", in);
  in = base(); in.cool = true;
  shootInputs("02-cooling", "Cooling", "Compressor running.", in);
  in = base(); in.heat = true;
  shootInputs("03-heating", "Heating", "Heater on (also through its PWM off-phase).", in);
  in = base(); in.humid = true;
  shootInputs("04-humidifying", "Humidifying", "Humidifier fan running.", in);
  in = base(); in.cool = true; in.stripping = true;
  shootInputs("05-drying", "Drying", "An RH strip: shown instead of Cooling.", in);
  in = base(); in.heat = true; in.humid = true;
  shootInputs("06-heating-humidifying", "Heating + Humidifying", "Two modes at once.", in);
  in = base(); in.cool = true; in.humid = true;
  shootInputs("07-cooling-humidifying", "Cooling + Humidifying", "Only with 'humidify during cooling runs' on.", in);

  // ---- Fault, with each cause (it replaces every other icon; the alert line names the cause)
  in = base(); in.heat = true; in.alerts.sensorFault = SensorFault::Chamber; in.alerts.excludedMask = 1u << 4;
  shootInputs("08-fault-chamber", "Fault: chamber sensor", "Chamber sensor not healthy. Its exclusion is a second alert.", in);
  in = base(); in.alerts.sensorFault = SensorFault::Cabinet; in.alerts.excludedMask = 0x03;
  shootInputs("09-fault-cabinet", "Fault: under 3 cabinet sensors", "Two cabinet sensors out.", in);
  in = base(); in.alerts.sensorFault = SensorFault::RecoveryHold;
  shootInputs("10-fault-hold", "Fault: recovery hold", "Requirement met again; the 5-minute hold runs.", in);
  in = base(); in.alerts.fanStalled = true;
  shootInputs("11-fault-fan", "Fault: plenum fan stalled", "Until loads resume (5.6).", in);
  in = base(); in.alerts.settingsBad = true;
  shootInputs("12-fault-settings", "Fault: settings failed check", "The settings failed their boot check.", in);

  // ---- Starting, Updating
  in = base(); in.bootHold = true; in.bootHoldLeftMs = 299000;
  shootInputs("13-starting-4m59", "Starting 4:59", "The boot hold, just after a power-up (shown alone).", in);
  in = base(); in.bootHold = true; in.bootHoldLeftMs = 6200;
  shootInputs("14-starting-0m07", "Starting 0:07", "The boot hold, nearly over.", in);
  in = base(); in.updating = true; in.updatePct = 0;
  shootInputs("15-updating-0", "Updating 0%", "An OTA update starting (Q5). Settings greyed.", in);
  in = base(); in.updating = true; in.updatePct = 63;
  shootInputs("16-updating-63", "Updating 63%", "Part way.", in);

  // ---- Allow update window
  {
    in = base();
    LcdUi ui;
    ui.begin(in.nowMs, 20);
    ui.update(in);
    ui.window().open(in.nowMs - 48000);           // opened 48 s ago
    ui.update(in);
    shoot("17-allow-update", "Allow update window", "Its own line, with the countdown.", ui.view());
  }

  // ---- the alert line
  in = base(); in.alerts.humidIneffective = true;
  shootInputs("18-alert-one", "One alert", "Tapping the line opens the alerts screen.", in);
  in = base(); in.heat = true; in.alerts.heaterIneffective = true; in.alerts.excludedMask = 1u << 1;
  in.alerts.sdFailed = true; in.alerts.dip12v = true;
  shootInputs("19-alert-more", "Alert with +3 more", "The most serious (12V dip) first.", in);

  // ---- no readings, no clock, no WiFi
  in = base(); in.cabinetOk = 2; in.wifi = false; in.alerts.clockBad = true; in.clockOk = false;
  in.alerts.sensorFault = SensorFault::Cabinet; in.alerts.excludedMask = 0x0C;
  shootInputs("20-dashes", "\"--\" and \"--:--\"", "Under 3 cabinet sensors, clock not trusted, WiFi off.", in);
  in = base(); in.fresh = false;
  shootInputs("21-stale", "\"--\" (no fresh reading)", "No reading for twice the read rate; clock fine.", in);

  // ---- summer time
  in = base(); in.utcNowS = kSummerUtc;
  shootInputs("22-summer-time", "Summer time", "13:32 UTC in July shows 14:32 (BST).", in);

  // ---- build tags
  in = base(); in.tag = BuildTag::Bench;
  shootInputs("23-tag-bench", "BENCH tag", "revq_bench.", in);
  in = base(); in.tag = BuildTag::Test;
  shootInputs("24-tag-test", "TEST tag", "revq_real_test.", in);
  in = base(); in.tag = BuildTag::BenchTest; in.heat = true;
  shootInputs("25-tag-bench-test", "BENCH TEST tag", "revq_bench_test, revq_bench_test_boot.", in);

  // ---- dimmed (a simulation: the real look depends on the PWM and the panel)
  {
    in = base(); in.heat = true;
    LcdUi ui;
    ui.begin(in.nowMs - 61000, 20);
    ui.update(in);
    TEST_ASSERT_EQUAL_UINT8(20, ui.view().backlightPct);
    shoot("26-dimmed", "Dimmed (simulated)", "60 s after the last touch: backlight 20%. Picture darkened to suggest it.",
          ui.view(), 0.45f);
  }

  // ---- settings
  {
    in = base();
    LcdUi ui;
    ui.begin(in.nowMs, 20);
    ui.update(in);
    tap(ui, in, layout::kSettingsPill);
    shoot("27-settings", "Settings", "As opened: the saved values.", ui.view());
    for (int i = 0; i < 20; i++) tap(ui, in, layout::kTempMinus);
    for (int i = 0; i < 20; i++) tap(ui, in, layout::kRhMinus);
    shoot("28-settings-min", "Settings at the minimum", "15.0 C and 55%: the minus buttons can't go further.", ui.view());
    for (int i = 0; i < 20; i++) tap(ui, in, layout::kTempPlus);
    for (int i = 0; i < 20; i++) tap(ui, in, layout::kRhPlus);
    shoot("29-settings-max", "Settings at the maximum", "22.0 C and 70%.", ui.view());
  }
  {
    in = base();
    LcdUi ui;
    ui.begin(in.nowMs, 20);
    ui.update(in);
    tap(ui, in, layout::kSettingsPill);
    tap(ui, in, layout::kTempPlus);
    tap(ui, in, layout::kRhMinus);
    shoot("30-settings-edited", "Settings, changed, not saved", "20.5 C and 61%: Save lights up.", ui.view());
    tap(ui, in, layout::kAllowUpdate);
    shoot("31-settings-allow-update", "Allow update pressed", "The window is open: its countdown on the button.", ui.view());
  }

  // ---- alerts screen
  {
    in = base();
    in.alerts.humidIneffective = true;
    LcdUi ui;
    ui.begin(in.nowMs, 20);
    ui.update(in);
    tap(ui, in, layout::kAlertPill);
    shoot("32-alerts-one", "Alerts: one", "Started at 14:32 today.", ui.view());
  }
  {
    in = base();
    in.alerts = allAlerts();
    in.clockOk = false;
    LcdUi ui;
    ui.begin(in.nowMs, 20);
    ui.update(in);
    tap(ui, in, layout::kAlertPill);
    shoot("33-alerts-full-1", "Alerts: all 15, page 1", "Clock not trusted, so \"for ...\" times.", ui.view());
    tap(ui, in, layout::kPageDown);
    shoot("34-alerts-full-2", "Alerts: page 2", "", ui.view());
    tap(ui, in, layout::kPageDown);
    shoot("35-alerts-full-3", "Alerts: page 3", "The last page: down can't go further.", ui.view());
  }
  {
    // started at different times, the clock trusted: today, and yesterday
    in = base();
    LcdUi ui;
    ui.begin(in.nowMs, 20);
    in.alerts.sdFailed = true;
    ui.update(in);                                    // SD failed, now - 26 h
    in.nowMs += 26 * 3600000u; in.uptimeMs += 26ull * 3600000ull; in.utcNowS += 26 * 3600;
    in.alerts.excludedMask = 1u << 2; in.alerts.manualMask = 1u << 2;
    ui.update(in);
    in.nowMs += 45 * 60000u; in.uptimeMs += 45ull * 60000ull; in.utcNowS += 45 * 60;
    in.alerts.coolingIneffective = true;
    ui.update(in);
    tap(ui, in, layout::kAlertPill);                 // over 60 s since a touch: this one only wakes it (5.5)
    TEST_ASSERT_TRUE(ui.screen() == LcdScreen::WalkPast);
    tap(ui, in, layout::kAlertPill);
    shoot("36-alerts-times", "Alerts: start times", "Today \"HH:MM\"; before today \"13 Nov HH:MM\". Manual exclusion marked.",
          ui.view());
  }
  {
    in = base();
    LcdUi ui;
    ui.begin(in.nowMs, 20);
    in.alerts.sdFailed = true;
    ui.update(in);
    tap(ui, in, layout::kAlertPill);
    in.alerts.sdFailed = false;                       // resolved: it leaves the list by itself
    ui.update(in);
    shoot("37-alerts-empty", "Alerts: none left", "\"No active alerts\".", ui.view());
  }

  // ---- BP2 test 29's landscape check
  {
    Canvas c(gFrame, kScreenW, kScreenH);
    paintLandscapeCheck(c, "Landscape A", 0);
    char path[160];
    snprintf(path, sizeof(path), "%s/38-test29-landscape.png", kDir);
    writePng(path, gFrame, kScreenW, kScreenH);
    gShots.push_back({"38-test29-landscape.png", "BP2 test 29: landscape check", "The UP arrow and four corners."});
    paintLandscapeCheck(c, "Landscape B", 0x05);
    snprintf(path, sizeof(path), "%s/39-test29-two-corners.png", kDir);
    writePng(path, gFrame, kScreenW, kScreenH);
    gShots.push_back({"39-test29-two-corners.png", "Test 29: two corners touched", "Top left and bottom left done."});
  }

  writeIndex();
  struct stat st;
  TEST_ASSERT_EQUAL_INT(0, stat(".pio.nosync/lcd-screens/index.html", &st));
  printf("  %zu screen pictures in %s/\n", gShots.size(), kDir);
}

// Redrawing only what changed (the board's way) ends with exactly the pixels of a full redraw,
// through a run of changes and screen switches; and every step is bounded.
void test_partial_redraw_equals_full_redraw() {
  static uint16_t a[kScreenW * kScreenH], b[kScreenW * kScreenH];
  memset(a, 0xA5, sizeof(a));                       // garbage first: everything must get painted
  Canvas ca(a, kScreenW, kScreenH), cb(b, kScreenW, kScreenH);
  Painter p;
  UiInputs in = base();
  LcdUi ui;
  ui.begin(in.nowMs, 20);
  int maxSteps = 0;
  uint32_t maxWrites = 0;
  auto settle = [&]() {
    int steps = 0;
    Rect r;
    for (;;) {
      ca.resetWrites();
      if (!p.step(ca, ui.view(), &r)) break;
      TEST_ASSERT_TRUE(r.area() <= kMaxStepPixels);
      if (ca.writes() > maxWrites) maxWrites = ca.writes();
      steps++;
      TEST_ASSERT_TRUE(steps < 100);
    }
    if (steps > maxSteps) maxSteps = steps;
    paintScreen(cb, ui.view());
    TEST_ASSERT_EQUAL_MEMORY(b, a, sizeof(a));
  };
  ui.update(in); settle();
  in.heat = true; in.meanC = 19.8f; ui.update(in); settle();
  in.humid = true; in.alerts.humidIneffective = true; ui.update(in); settle();
  in.alerts.dip12v = true; in.utcNowS += 60; ui.update(in); settle();
  tap(ui, in, layout::kSettingsPill); settle();
  tap(ui, in, layout::kTempMinus); settle();
  tap(ui, in, layout::kAllowUpdate); settle();
  tap(ui, in, layout::kCancel); settle();
  tap(ui, in, layout::kAlertPill); settle();
  in.alerts.dip12v = false; ui.update(in); settle();
  tap(ui, in, layout::kBack); settle();
  in.wifi = false; in.cabinetOk = 2; ui.update(in); settle();
  printf("  longest redraw %d steps; largest step %u pixel writes (limit %ld area)\n", maxSteps, (unsigned)maxWrites,
         (long)kMaxStepPixels);
  TEST_ASSERT_TRUE(maxWrites <= 2u * (uint32_t)kMaxStepPixels);   // a fill plus its text and icons
}

// Every element fits the step limit, and the elements of a screen don't overlap (so drawing
// one never spoils another).
void test_elements_bounded_and_apart() {
  for (int e = 0; e < kElements; e++) TEST_ASSERT_TRUE(elementRect((Element)e).area() <= kMaxStepPixels);
  const LcdScreen screens[] = {LcdScreen::WalkPast, LcdScreen::Settings, LcdScreen::Alerts};
  for (LcdScreen s : screens) {
    Element list[kElements];
    const int n = elementsOf(s, list, kElements);
    for (int i = 0; i < n; i++)
      for (int j = i + 1; j < n; j++) {
        const Rect a = elementRect(list[i]), b = elementRect(list[j]);
        const bool apart = a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y;
        TEST_ASSERT_TRUE_MESSAGE(apart, "two elements overlap");
      }
  }
}

void test_corner_of() {
  TEST_ASSERT_EQUAL_INT(0, cornerOf(10, 10));
  TEST_ASSERT_EQUAL_INT(1, cornerOf(470, 10));
  TEST_ASSERT_EQUAL_INT(2, cornerOf(10, 310));
  TEST_ASSERT_EQUAL_INT(3, cornerOf(470, 310));
  TEST_ASSERT_EQUAL_INT(-1, cornerOf(240, 160));
}

void test_fit_text() {
  char out[48];
  fitText(out, sizeof(out), kFontMedium, "Short", 400);
  TEST_ASSERT_EQUAL_STRING("Short", out);
  fitText(out, sizeof(out), kFontMedium, "Fault: chamber sensor not healthy", 150);
  TEST_ASSERT_TRUE(Canvas::measure(kFontMedium, out) <= 150);
  TEST_ASSERT_NOT_NULL(strstr(out, "..."));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_elements_bounded_and_apart);
  RUN_TEST(test_corner_of);
  RUN_TEST(test_fit_text);
  RUN_TEST(test_partial_redraw_equals_full_redraw);
  RUN_TEST(test_render_every_screen);
  return UNITY_END();
}
