#pragma once
// Where everything sits on the LCD (spec 5.5): landscape, 480 x 320, fixed. Plain constants,
// shared by the drawing (lib/lcdgfx) and the touch hit test (lcd_ui), so a button is always
// touched where it is drawn.

#include <stdint.h>

namespace wineador {

constexpr int16_t kScreenW = 480, kScreenH = 320;

struct Rect {
  int16_t x = 0, y = 0, w = 0, h = 0;
  constexpr Rect() = default;
  constexpr Rect(int16_t x_, int16_t y_, int16_t w_, int16_t h_) : x(x_), y(y_), w(w_), h(h_) {}
  constexpr bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  constexpr int32_t area() const { return (int32_t)w * h; }
  constexpr bool empty() const { return w <= 0 || h <= 0; }
};

// Touch targets are a little larger than what is drawn, so a finger near the edge still counts.
constexpr Rect grow(Rect r, int16_t by) { return Rect((int16_t)(r.x - by), (int16_t)(r.y - by), (int16_t)(r.w + 2 * by), (int16_t)(r.h + 2 * by)); }

namespace layout {

// ---- walk-past screen
constexpr Rect kTime{0, 0, 160, 40};
constexpr Rect kTag{160, 0, 200, 40};
constexpr Rect kWifi{400, 0, 80, 40};
constexpr Rect kTemp{0, 40, 288, 132};
constexpr Rect kRh{288, 40, 192, 132};
constexpr Rect kSetpoints{0, 172, 480, 28};
constexpr Rect kModes{0, 200, 480, 56};
constexpr Rect kUpdate{0, 256, 480, 22};
constexpr Rect kAlertLine{0, 278, 352, 42};
constexpr Rect kSettingsButton{352, 278, 128, 42};
// what is drawn (and touched) inside those
constexpr Rect kAlertPill{8, 282, 336, 34};
constexpr Rect kSettingsPill{360, 282, 112, 34};

// ---- settings screen
constexpr Rect kSetTitle{0, 0, 480, 44};
constexpr Rect kTempLabel{0, 48, 200, 88};
constexpr Rect kTempControl{200, 48, 280, 88};
constexpr Rect kRhLabel{0, 140, 200, 88};
constexpr Rect kRhControl{200, 140, 280, 88};
constexpr Rect kUpdateButtonArea{0, 236, 200, 84};
constexpr Rect kCancelSaveArea{200, 236, 280, 84};
constexpr Rect kTempMinus{208, 60, 64, 64};
constexpr Rect kTempPlus{408, 60, 64, 64};
constexpr Rect kRhMinus{208, 152, 64, 64};
constexpr Rect kRhPlus{408, 152, 64, 64};
constexpr Rect kAllowUpdate{16, 248, 176, 60};
constexpr Rect kCancel{208, 248, 120, 60};
constexpr Rect kSave{344, 248, 120, 60};

// ---- alerts screen
constexpr Rect kAlertsTitle{0, 0, 480, 44};
constexpr int kAlertRows = 5;            // per page
constexpr int16_t kAlertRowTop = 48, kAlertRowH = 44;
constexpr Rect alertRow(int i) { return Rect(0, (int16_t)(kAlertRowTop + i * kAlertRowH), 480, kAlertRowH); }
constexpr Rect kAlertsBar{0, 268, 480, 52};
constexpr Rect kBack{16, 274, 140, 40};
constexpr Rect kPageUp{312, 274, 72, 40};
constexpr Rect kPageDown{392, 274, 72, 40};

}  // namespace layout
}  // namespace wineador
