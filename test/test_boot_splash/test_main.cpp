// The boot splash (spec 5.5, 3A.11; lib/core lcd_ui.h BootSplash): its 2 s / 5 s timing, and the
// whole sequence run as the firmware runs it - the control pass, then the screen's part
// (src/app/screen.cpp's order: logic, one drawing step, the splash's decision, one sending step,
// the backlight) - with the real Controller, LcdUi and Painter, and a fake LCD that sends the
// picture piece by piece at the panel's real speed, copying pixels as it sends, as the driver does.
#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <memory>
#include "wineador/lcd_ui.h"
#include "wineador/lcd_seq.h"
#include "wineador/controller.h"
#include "wineador/settings.h"
#include "wineador/build_info.h"
#include "lcdgfx/screens.h"

using namespace wineador;
using namespace lcdgfx;
using Phase = BootSplash::Phase;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------- the 2 s / 5 s
// Never over before the splash is on the screen, however long that takes.
void test_not_over_before_shown() {
  BootSplash s;
  for (uint32_t t = 0; t <= 60000; t += 100) {
    s.update(true, false, t);                       // painted, but the LCD still sending
    TEST_ASSERT_TRUE(s.phase() == Phase::Splash);
    TEST_ASSERT_FALSE(s.backlightOn());
    TEST_ASSERT_FALSE(s.touches());
    TEST_ASSERT_TRUE(s.sends());
  }
  s.update(false, true, 60100);                     // the LCD idle but the splash not all painted
  TEST_ASSERT_TRUE(s.phase() == Phase::Splash);
  s.update(true, true, 60200);                      // both: it is on the screen
  TEST_ASSERT_TRUE(s.phase() == Phase::Holding);
  TEST_ASSERT_EQUAL_UINT32(60200, s.shownMs());
  TEST_ASSERT_TRUE(s.backlightOn());
  TEST_ASSERT_FALSE(s.sends());
  TEST_ASSERT_FALSE(s.touches());
}

// The walk-past screen ready early: the splash still stays 2 s.
void test_ready_early_over_at_2s() {
  BootSplash s;
  s.update(true, true, 1000);
  for (uint32_t t = 1000; t < 1000 + kSplashMinMs; t += 5) {
    s.update(true, true, t);
    TEST_ASSERT_TRUE_MESSAGE(s.phase() == Phase::Holding, "over before 2 s");
    TEST_ASSERT_FALSE(s.takeRelease());
  }
  s.update(true, true, 1000 + kSplashMinMs);
  TEST_ASSERT_TRUE(s.phase() == Phase::Done);
  TEST_ASSERT_EQUAL_UINT32(1000 + kSplashMinMs, s.endedMs());
  TEST_ASSERT_TRUE(s.takeRelease());
  TEST_ASSERT_FALSE(s.takeRelease());               // once
  TEST_ASSERT_TRUE(s.touches());
  TEST_ASSERT_TRUE(s.sends());
}

// Ready at 3 s: over then.
void test_ready_at_3s() {
  BootSplash s;
  s.update(true, true, 500);
  for (uint32_t t = 500; t < 3500; t += 5) { s.update(false, true, t); TEST_ASSERT_TRUE(s.phase() == Phase::Holding); }
  s.update(true, true, 3500);
  TEST_ASSERT_TRUE(s.phase() == Phase::Done);
  TEST_ASSERT_EQUAL_UINT32(3000, s.endedMs() - s.shownMs());
}

// Never ready: over at 5 s regardless.
void test_never_ready_over_at_5s() {
  BootSplash s;
  s.update(true, true, 700);
  for (uint32_t t = 700; t < 700 + kSplashMaxMs; t += 5) { s.update(false, false, t); TEST_ASSERT_TRUE(s.phase() == Phase::Holding); }
  s.update(false, false, 700 + kSplashMaxMs);
  TEST_ASSERT_TRUE(s.phase() == Phase::Done);
  TEST_ASSERT_TRUE(s.takeRelease());
}

// Once over, it stays over (a restart is a new boot, a new BootSplash).
void test_latches() {
  BootSplash s;
  s.update(true, true, 0);
  s.update(true, true, kSplashMinMs);
  TEST_ASSERT_TRUE(s.phase() == Phase::Done);
  s.takeRelease();
  for (uint32_t t = kSplashMinMs; t < 100000; t += 1000) {
    s.update(false, false, t);
    TEST_ASSERT_TRUE(s.phase() == Phase::Done);
    TEST_ASSERT_FALSE(s.takeRelease());
  }
}

// millis() wrapping past 2^32 while the splash is up changes nothing.
void test_across_millis_wrap() {
  BootSplash s;
  const uint32_t t0 = 0xFFFFFC00u;                  // 1 s before the wrap
  s.update(true, true, t0);
  s.update(true, true, t0 + 1999);
  TEST_ASSERT_TRUE(s.phase() == Phase::Holding);
  s.update(true, true, t0 + 2000);
  TEST_ASSERT_TRUE(s.phase() == Phase::Done);
}

// ---------------------------------------------------------------- the sequence, as the firmware runs it
namespace {

constexpr uint32_t kPassMs = 5;   // a normal pass
// What the LCD sends in one pass at its real clock: 16 bits a pixel.
constexpr uint32_t kPixelsPerPass = (uint32_t)((uint64_t)s3SpiClockHz(kSpiSourceHz, kLcdSpiHz) * kPassMs / 1000 / 16);

// The LCD driver, faked: rectangles queued, sent row by row as the passes go, the pixels copied
// from the picture AS THEY ARE SENT (as src/drivers/lcd does), into what the panel shows.
struct FakeLcd {
  const uint16_t* frame = nullptr;
  uint16_t panel[kScreenW * kScreenH];
  std::deque<Rect> q;
  Rect cur;
  int row = 0;
  bool have = false;
  bool stuck = false;               // a panel that never finishes sending
  uint32_t sent = 0;
  void queue(Rect r) { q.push_back(r); }
  bool idle() const { return !stuck && q.empty() && !have; }
  void step() {
    if (stuck) return;
    uint32_t budget = kPixelsPerPass;
    while (budget > 0) {
      if (!have) {
        if (q.empty()) return;
        cur = q.front();
        q.pop_front();
        row = cur.y;
        have = true;
      }
      memcpy(panel + row * kScreenW + cur.x, frame + row * kScreenW + cur.x, (size_t)cur.w * 2);
      sent += cur.w;
      budget = budget > (uint32_t)cur.w ? budget - cur.w : 0;
      if (++row >= cur.y + cur.h) have = false;
    }
  }
};

struct Firmware {
  Settings s = settingsFor(Profile::Real);
  Controller ctl;
  LcdUi ui;
  BootSplash splash;
  UiView sv;
  Painter painter;
  uint16_t frame[kScreenW * kScreenH];
  Canvas canvas{frame, kScreenW, kScreenH};
  FakeLcd lcd;
  LoadOutputs live;
  uint32_t now = 0;
  uint8_t backlight = 0;
  uint32_t passes = 0;
  int32_t maxStepArea = 0;
  uint32_t maxStepWrites = 0;
  bool sentWhileHolding = false;
  bool settingsOk = true;
  Demand demand;
  char version[40];

  Firmware() {
    memset(frame, 0xA5, sizeof(frame));            // garbage: the boot clear is the LCD's
    lcd.frame = frame;
    memset(lcd.panel, 0, sizeof(lcd.panel));       // cleared to black at boot (3A.11)
    makeVersionString(version, sizeof(version), "2.0.0", Profile::Real, false, false);
    ctl.begin(s, 0);
    ui.begin(0, 20);
    sv = splashView(version);
    demand.heat = true;
    demand.humid = true;
  }

  // One pass: the minimum job, then the screen's part (src/app/screen.cpp's order).
  void pass() {
    now += kPassMs;
    passes++;
    PassInputs pi;
    pi.nowMs = now;
    pi.settingsOk = settingsOk;
    pi.fanOk = true;
    pi.readingsFresh = true;
    pi.live = live;
    live = ctl.pass(pi, demand);

    UiInputs in;
    in.nowMs = now;
    in.uptimeMs = now;
    in.cabinetOk = 4;
    in.fresh = true;
    in.meanC = 20.3f;
    in.meanRhPct = 62.4f;
    in.bootHold = ctl.hold().active();
    in.bootHoldLeftMs = ctl.hold().remainingMs();
    in.heat = live.heat;
    in.humid = live.humid;
    in.alerts.sensorsKnown = true;
    ui.update(in);

    const UiView& view = splash.paintsSplash() ? sv : ui.view();
    const bool sends = splash.sends();
    canvas.resetWrites();
    Rect painted;
    if (painter.step(canvas, view, &painted)) {
      if (painted.area() > maxStepArea) maxStepArea = painted.area();
      if (canvas.writes() > maxStepWrites) maxStepWrites = canvas.writes();
      if (sends) lcd.queue(painted);
    }
    const uint32_t before = lcd.sent;
    splash.update(painter.idle(view), lcd.idle(), now);
    if (splash.takeRelease()) lcd.queue(Rect(0, 0, kScreenW, kScreenH));
    lcd.step();
    if (splash.phase() == Phase::Holding && lcd.sent != before) sentWhileHolding = true;
    backlight = splash.backlightOn() ? ui.backlightPct(now) : 0;
  }
};

// The same control, with no screen at all: what every pass must match.
struct ControlOnly {
  Settings s = settingsFor(Profile::Real);
  Controller ctl;
  LoadOutputs live;
  uint32_t now = 0;
  ControlOnly() { ctl.begin(s, 0); }
  LoadOutputs pass(bool settingsOk, const Demand& d) {
    now += kPassMs;
    PassInputs pi;
    pi.nowMs = now;
    pi.settingsOk = settingsOk;
    pi.fanOk = true;
    pi.readingsFresh = true;
    pi.live = live;
    live = ctl.pass(pi, d);
    return live;
  }
};

// Big (two pictures): on the heap, a fresh one for each test (never copied: its canvas points
// into its own picture).

}  // namespace

// From the first pass: the control pass runs every pass, the boot hold counts down from boot,
// the guards are decided - a settings failure during the splash blocks the loads at once - and
// nothing differs from control with no screen. Meanwhile: the splash goes up in small steps
// (3A.11), the backlight comes on only once it is all on the panel, the panel shows the splash
// unchanged while the walk-past screen is drawn behind it, and then the walk-past screen whole.
void test_control_runs_under_the_splash() {
  std::unique_ptr<Firmware> fw(new Firmware());
  Firmware& f = *fw;
  ControlOnly ref;
  static uint16_t splashPicture[kScreenW * kScreenH];
  {
    Canvas c(splashPicture, kScreenW, kScreenH);
    paintScreen(c, f.sv);
  }
  uint32_t lastLeft = f.ctl.hold().lengthMs() + 1;
  bool sawHolding = false, sawDone = false;
  uint32_t shownAt = 0;
  for (int i = 0; i < 2000; i++) {                 // 10 s of passes
    // 3 s to 3.5 s in (the splash is up): the settings fail their check, then pass again.
    f.settingsOk = !(f.now >= 3000 && f.now < 3500);
    f.pass();
    const LoadOutputs want = ref.pass(f.settingsOk, f.demand);
    TEST_ASSERT_TRUE_MESSAGE(f.live == want, "control differs with the screen running");
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(i + 1), f.passes);
    // the boot hold counts down from boot, pass after pass
    TEST_ASSERT_TRUE(f.ctl.hold().active());
    TEST_ASSERT_TRUE(f.ctl.hold().remainingMs() < lastLeft);
    lastLeft = f.ctl.hold().remainingMs();
    TEST_ASSERT_EQUAL_UINT32(f.ctl.hold().lengthMs() - f.now, f.ctl.hold().remainingMs());
    // the guards decided this pass: boot hold, or the settings failure ahead of it
    const GuardBlock want_block = f.settingsOk ? GuardBlock::BootHold : GuardBlock::SettingsProblem;
    TEST_ASSERT_TRUE(f.ctl.decisions().heat.block == want_block);
    TEST_ASSERT_TRUE(f.ctl.decisions().humid.block == want_block);
    TEST_ASSERT_FALSE(f.live.heat || f.live.humid || f.live.cool);

    switch (f.splash.phase()) {
      case Phase::Splash:
        TEST_ASSERT_EQUAL_UINT8(0, f.backlight);
        break;
      case Phase::Holding:
        if (!sawHolding) {
          sawHolding = true;
          shownAt = f.now;
          // all of the splash is on the panel at the moment the backlight comes on
          TEST_ASSERT_EQUAL_MEMORY(splashPicture, f.lcd.panel, sizeof(splashPicture));
        }
        // every pass it is held, the panel still shows the splash, unchanged: nothing of the
        // walk-past screen being drawn behind it leaks out
        TEST_ASSERT_EQUAL_MEMORY(splashPicture, f.lcd.panel, sizeof(splashPicture));
        TEST_ASSERT_EQUAL_UINT8(100, f.backlight);
        TEST_ASSERT_FALSE(f.splash.touches());
        break;
      case Phase::Done:
        if (!sawDone) {
          sawDone = true;
          TEST_ASSERT_TRUE(sawHolding);
          const uint32_t up = f.now - shownAt;
          TEST_ASSERT_TRUE(up >= kSplashMinMs && up <= kSplashMaxMs);
        }
        TEST_ASSERT_EQUAL_UINT8(100, f.backlight);
        break;
    }
  }
  TEST_ASSERT_TRUE(sawDone);
  TEST_ASSERT_FALSE(f.sentWhileHolding);
  // every drawing step stayed small (3A.11's 50 ms)
  TEST_ASSERT_TRUE(f.maxStepArea <= kMaxStepPixels);
  TEST_ASSERT_TRUE(f.maxStepWrites <= 2u * (uint32_t)kMaxStepPixels);
  // the walk-past screen is what the panel shows now: Starting with its countdown. (The countdown
  // redraws every second: let the piece on the wire finish first.)
  for (int i = 0; i < 400 && !(f.lcd.idle() && f.painter.idle(f.ui.view())); i++) f.pass();
  TEST_ASSERT_TRUE(f.lcd.idle());
  TEST_ASSERT_TRUE(f.painter.idle(f.ui.view()));
  static uint16_t walk[kScreenW * kScreenH];
  Canvas c(walk, kScreenW, kScreenH);
  paintScreen(c, f.ui.view());
  TEST_ASSERT_EQUAL_MEMORY(walk, f.lcd.panel, sizeof(walk));
  TEST_ASSERT_TRUE(f.ui.view().nModes == 1 && f.ui.view().modes[0] == ModeIcon::Starting);
  printf("  splash on the panel %lu ms after boot, held %lu ms; %lu pixels a pass\n", (unsigned long)f.splash.shownMs(),
         (unsigned long)(f.splash.endedMs() - f.splash.shownMs()), (unsigned long)kPixelsPerPass);
}

// An LCD that never finishes sending: the splash never goes up and the backlight stays off -
// and control runs every pass exactly as without a screen.
void test_stuck_lcd_never_holds_control() {
  std::unique_ptr<Firmware> fw(new Firmware());
  Firmware& f = *fw;
  f.lcd.stuck = true;
  ControlOnly ref;
  for (int i = 0; i < 2000; i++) {
    f.pass();
    TEST_ASSERT_TRUE(f.live == ref.pass(f.settingsOk, f.demand));
    TEST_ASSERT_TRUE(f.splash.phase() == Phase::Splash);
    TEST_ASSERT_EQUAL_UINT8(0, f.backlight);
  }
  TEST_ASSERT_EQUAL_UINT32(2000, f.passes);
  TEST_ASSERT_EQUAL_UINT32(f.ctl.hold().lengthMs() - f.now, f.ctl.hold().remainingMs());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_not_over_before_shown);
  RUN_TEST(test_ready_early_over_at_2s);
  RUN_TEST(test_ready_at_3s);
  RUN_TEST(test_never_ready_over_at_5s);
  RUN_TEST(test_latches);
  RUN_TEST(test_across_millis_wrap);
  RUN_TEST(test_control_runs_under_the_splash);
  RUN_TEST(test_stuck_lcd_never_holds_control);
  return UNITY_END();
}
