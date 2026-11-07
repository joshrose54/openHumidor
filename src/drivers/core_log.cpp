#include "core_log.h"
#include <stdarg.h>
#include "esp_log.h"

namespace corelog {

// Every ESP-IDF log message (ESP_LOGE/W/I) is handed to this function instead of
// being printed. It throws the message away and returns at once.
// Early logs (watchdog interrupt) and panic output do not come through here.
static int discard(const char*, va_list) { return 0; }

void silence() { esp_log_set_vprintf(discard); }

}  // namespace corelog
