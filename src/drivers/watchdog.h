#pragma once
// Task watchdog on the main loop, 5 s (spec 5.2). If the loop stops feeding it,
// the ESP32 resets and the next boot reports TASK_WATCHDOG.

#include <stdint.h>

namespace watchdog {

bool begin(uint32_t timeoutMs);   // BOOT ONLY. Subscribes the loop task
void feed();

}  // namespace watchdog
