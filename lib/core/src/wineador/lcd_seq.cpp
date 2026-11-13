#include "wineador/lcd_seq.h"

namespace wineador {

uint16_t colourBarAt(uint16_t x, uint16_t y) {
  (void)y;
  static const uint16_t bars[8] = {kWhite, kYellow, kCyan, kGreen, kMagenta, kRed, kBlue, kBlack};
  if (x >= kLcdWidth) x = kLcdWidth - 1;
  return bars[(uint32_t)x * 8 / kLcdWidth];
}

void lcdWindowArgs(uint16_t first, uint16_t last, uint8_t out[4]) {
  out[0] = (uint8_t)(first >> 8);
  out[1] = (uint8_t)(first & 0xFF);
  out[2] = (uint8_t)(last >> 8);
  out[3] = (uint8_t)(last & 0xFF);
}

}  // namespace wineador
