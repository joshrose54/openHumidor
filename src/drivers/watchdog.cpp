#include "watchdog.h"
#include "esp_task_wdt.h"

namespace watchdog {

bool begin(uint32_t timeoutMs) {
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = timeoutMs;
  // Keep the core's default watch on core 0's idle task (catches anything starving
  // core 0). Not core 1: our loop runs there without pausing and would trip it falsely.
  cfg.idle_core_mask = (1u << 0);
  cfg.trigger_panic = true;      // reset (not just a warning) when it fires
  // The core starts the watchdog itself; reconfigure it, or start it if it isn't running.
  esp_err_t e = esp_task_wdt_reconfigure(&cfg);
  if (e == ESP_ERR_INVALID_STATE) e = esp_task_wdt_init(&cfg);
  if (e != ESP_OK) return false;
  return esp_task_wdt_add(nullptr) == ESP_OK;   // nullptr = this task (the Arduino loop)
}

void feed() { esp_task_wdt_reset(); }

}  // namespace watchdog
