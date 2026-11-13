#include "blowers.h"
#include <Arduino.h>
#include "pins.h"
#include "wineador/bp2_calc.h"

namespace blowers {

#if defined(WINEADOR_BOARD_REVQ)

static uint8_t gPct = 100;
static bool gPwm = false;

void begin() {
  digitalWrite(pins::kBlowerPwm, HIGH);   // level first: the pin goes from pulled-up (full) to driven high (full)
  pinMode(pins::kBlowerPwm, OUTPUT);
  digitalWrite(pins::kBlowerPwm, HIGH);
  gPct = 100;
}

void setPct(uint8_t pct) {
  if (pct > 100) pct = 100;
  if (pct == 100) {
    if (gPwm) { ledcDetach(pins::kBlowerPwm); gPwm = false; }
    pinMode(pins::kBlowerPwm, OUTPUT);
    digitalWrite(pins::kBlowerPwm, HIGH);
  } else {
    if (!gPwm) gPwm = ledcAttach(pins::kBlowerPwm, kPwmHz, kPwmBits);
    ledcWrite(pins::kBlowerPwm, wineador::blowerDuty(pct, kPwmBits));
  }
  gPct = pct;
}

uint8_t pct() { return gPct; }
bool fitted() { return true; }

#else   // Rev N: no blowers on the board

void begin() {}
void setPct(uint8_t) {}
uint8_t pct() { return 0; }
bool fitted() { return false; }

#endif

}  // namespace blowers
