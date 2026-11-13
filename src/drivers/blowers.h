#pragma once
// Rev Q's circulation blowers, GPIO15 -> Q7 (spec 3A.6, 3A.11): high = on, duty = speed, about
// 100 kHz; undriven = full speed (10k pull-up). begin() drives GPIO15 high (full speed, the same as
// the hardware default); the loop then sets the duty from the rule (4.6b, lib/core blower_rule.h),
// calling setPct() only when it changes. The BP2 test 22 sketch steps it by hand (duty-to-voltage
// table). Rev N: no blowers, nothing.

#include <stdint.h>

namespace blowers {

constexpr uint32_t kPwmHz = 100000;   // 3A.6: about 100 kHz
constexpr uint8_t kPwmBits = 9;       // 80 MHz / 100 kHz = 800 steps: 9 bits (512) is the most

void begin();                 // BOOT: GPIO15 high, full speed
void setPct(uint8_t pct);     // 100 = high (full), else PWM at kPwmHz
uint8_t pct();
bool fitted();

}  // namespace blowers
