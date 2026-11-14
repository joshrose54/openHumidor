#include "backlight.h"
#include <Arduino.h>
#include "pins.h"

namespace backlight {

static uint8_t gPct = 0;

void begin() {
  ledcAttach(pins::kLcdBacklight, kPwmHz, kPwmBits);
  set(0);
}

void set(uint8_t pct) {
  if (pct > 100) pct = 100;
  ledcWrite(pins::kLcdBacklight, (uint32_t)pct * 255u / 100u);
  gPct = pct;
}

uint8_t pct() { return gPct; }
bool fitted() { return true; }

}  // namespace backlight
