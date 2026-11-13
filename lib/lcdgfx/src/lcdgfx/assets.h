#pragma once
// The LCD's fonts and icons as 4-bit alpha tables (made by tools/make_lcd_assets.py from Inter,
// SIL OFL 1.1, and Material Symbols Rounded, Apache 2.0; licences in lib/lcdgfx/LICENSES/).
// Plain data: the same tables draw on the board and on the Mac.

#include <stdint.h>

namespace lcdgfx {

struct Glyph {
  uint16_t codepoint;
  uint32_t offset;      // into the font's bits
  uint8_t w, h;         // ink size (0 for a space)
  int8_t xoff;          // ink left edge from the pen position
  int16_t yoff;         // ink top from the baseline (negative = above)
  uint8_t advance;      // pen move after the glyph
};

struct Font {
  const Glyph* glyphs;  // sorted by codepoint
  const uint8_t* bits;  // 4 bits a pixel, two to a byte, high nibble first, rows padded to a byte
  uint16_t count;
  uint8_t lineHeight, ascent, descent;
};

struct Icon {
  uint8_t w, h;
  const uint8_t* bits;  // as a font's
};

extern const Font kFontHuge, kFontLarge, kFontMedium, kFontSmall;
extern const Icon kIconCooling, kIconHeating, kIconHumidifying, kIconDrying, kIconStandby, kIconStarting,
    kIconUpdating, kIconFault, kIconWifi, kIconWifiOff, kIconSettings, kIconAlert, kIconAdd, kIconRemove,
    kIconPageUp, kIconPageDown, kIconBack, kIconArrowUp;

}  // namespace lcdgfx
