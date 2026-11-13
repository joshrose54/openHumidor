#pragma once
// The ESP32's reset reason as a word (the boot banner, the reset record, the BP2 sketches).

#include "esp_system.h"

namespace resetreason {

inline const char* name(int r) {
  switch ((esp_reset_reason_t)r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_SW:        return "SOFTWARE";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WATCHDOG";
    case ESP_RST_TASK_WDT:  return "TASK_WATCHDOG";
    case ESP_RST_WDT:       return "WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    case ESP_RST_EXT:       return "EXTERNAL";
    default:                return "OTHER";
  }
}

inline const char* now() { return name((int)esp_reset_reason()); }

}  // namespace resetreason
