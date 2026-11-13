#pragma once
// Drawing into a 16-bit (RGB565) picture held in memory: filled and rounded rectangles,
// anti-aliased text and icons. No hardware: the same code draws on the board (into PSRAM, then
// sent to the LCD) and on the Mac (then saved as a PNG). Pixels are stored in the CPU's own byte
// order; the LCD driver swaps them as it sends.

#include <stdint.h>
#include <stddef.h>
#include "lcdgfx/assets.h"
#include "wineador/lcd_layout.h"

namespace lcdgfx {

using wineador::Rect;

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
// a = 0..15 (the tables' 4-bit alpha)
uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t a);

class Canvas {
 public:
  Canvas(uint16_t* px, int16_t w, int16_t h) : px_(px), w_(w), h_(h), clip_(0, 0, w, h) {}

  int16_t width() const { return w_; }
  int16_t height() const { return h_; }
  uint16_t* pixels() { return px_; }
  const uint16_t* pixels() const { return px_; }

  // Drawing stays inside the clip (and the picture).
  void setClip(Rect r);
  void clearClip() { clip_ = Rect(0, 0, w_, h_); }

  void fill(Rect r, uint16_t c);
  void roundRect(Rect r, int16_t radius, uint16_t c);   // filled; corners blended with what is there
  void icon(const Icon& ic, int16_t x, int16_t y, uint16_t c);
  void bitmap(const Bitmap& b, int16_t x, int16_t y, uint16_t c);
  // Text from its baseline; returns the width drawn. UTF-8; characters the font lacks are skipped.
  int16_t text(const Font& f, int16_t x, int16_t baseline, const char* s, uint16_t c);
  static int16_t measure(const Font& f, const char* s);

  // Pixels written since the last reset: the native tests bound each drawing step with it.
  uint32_t writes() const { return writes_; }
  void resetWrites() { writes_ = 0; }

 private:
  void mask4(int16_t x, int16_t y, uint16_t w, uint16_t h, const uint8_t* bits, uint16_t c);
  uint16_t* px_;
  int16_t w_, h_;
  Rect clip_;
  uint32_t writes_ = 0;
};

// Next UTF-8 codepoint from *s (moves *s on); 0 at the end.
uint32_t nextCodepoint(const char** s);
const Glyph* findGlyph(const Font& f, uint32_t cp);

// Copies s into out, cut with "..." so it measures at most maxW.
void fitText(char* out, size_t n, const Font& f, const char* s, int16_t maxW);

}  // namespace lcdgfx
