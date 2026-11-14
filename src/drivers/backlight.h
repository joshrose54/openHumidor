#pragma once
// Rev Q's LCD backlight, GPIO13 -> 100R -> the LCD's LED pin (spec 3A.9, 3A.11): PWM 5 kHz,
// 8-bit. The LCD pulls the pin high (backlight on) until the firmware drives it. Before the B7
// screens the backlight is OFF (3A.11). set() is for the BP2 test 29 sketch.

#include <stdint.h>

namespace backlight {

constexpr uint32_t kPwmHz = 5000;   // 3A.11
constexpr uint8_t kPwmBits = 8;

void begin();                 // BOOT: PWM attached, backlight off
void set(uint8_t pct);        // 0-100
uint8_t pct();
bool fitted();

}  // namespace backlight
