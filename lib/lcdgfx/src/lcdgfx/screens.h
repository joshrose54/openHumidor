#pragma once
// The three screens (spec 5.5) drawn from a UiView (lib/core lcd_ui.h). Each screen is a set of
// elements with fixed places (lcd_layout.h). The Painter redraws only the elements whose
// content changed, one element per call, so a drawing step is small (3A.11: no step holds the
// loop over 50 ms) - each element's area is at most kMaxStepPixels, checked by the native tests.
// paintScreen() draws everything at once with the same functions: the Mac's PNGs use it.

#include <stdint.h>
#include "lcdgfx/canvas.h"
#include "wineador/lcd_ui.h"

namespace lcdgfx {

enum class Element : uint8_t {
  Bg0, Bg1, Bg2, Bg3, Bg4, Bg5, Bg6, Bg7,           // the background, in 8 strips of 40 rows
  // walk-past
  Time, Tag, Wifi, Temp, Rh, Setpoints, Modes, Update, AlertLine, SettingsButton,
  // settings
  SetTitle, TempLabel, TempControl, RhLabel, RhControl, UpdateButton, CancelSave,
  // alerts
  AlertsTitle, Row0, Row1, Row2, Row3, Row4, AlertsBar,
  // boot splash (on black)
  SplashLogo, SplashVersion,
  Count
};
constexpr int kElements = (int)Element::Count;
constexpr int kBgStrips = 8;
constexpr int32_t kMaxStepPixels = 40000;   // the largest element: the temperature, 288 x 132
static_assert(kElements <= 64, "the Painter keeps one bit an element");

// Where the walk-past wordmark's ink is drawn (inside the Tag element; spec 5.5: top-centre,
// between the time and the WiFi icon). The native tests keep it clear of both.
Rect wordmarkRect();
// Where the splash logo's ink is drawn (centred in layout::kSplashLogo).
Rect splashLogoRect();
Rect elementRect(Element e);
// The elements of a screen (not the background strips). Returns how many.
int elementsOf(wineador::LcdScreen s, Element* out, int max);
void paintElement(Canvas& c, const wineador::UiView& v, Element e);
// A number that changes whenever what the element shows changes.
uint32_t elementKey(const wineador::UiView& v, Element e);

// Everything, at once: the background and every element of the view's screen.
void paintScreen(Canvas& c, const wineador::UiView& v);

class Painter {
 public:
  void invalidate() { all_ = true; }            // repaint the whole screen (e.g. at start, or a stress test)
  // Paints at most one element whose content changed since it was last painted (a new screen
  // repaints everything, background first). False if nothing needed painting.
  bool step(Canvas& c, const wineador::UiView& v, Rect* painted, Element* which = nullptr);
  bool idle(const wineador::UiView& v);         // nothing left to paint for this view

 private:
  void plan(const wineador::UiView& v);
  bool all_ = true;
  wineador::LcdScreen screen_ = wineador::LcdScreen::WalkPast;
  uint64_t dirty_ = 0;
  uint32_t keys_[kElements] = {};
};

// BP2 test 29's landscape check (not a firmware screen): an UP arrow and four corner targets.
// done = corners touched so far (bit 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right).
void paintLandscapeCheck(Canvas& c, const char* wayName, uint8_t done);
// Which corner a touch is in (0-3 as above), or -1 if not near one. Screen coordinates.
int cornerOf(int x, int y);

}  // namespace lcdgfx
