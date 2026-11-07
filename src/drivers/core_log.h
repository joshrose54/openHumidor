#pragma once
// Stops the ESP32 core's own error messages from holding up the main loop (spec 5.2).
//
// Why: on a bus fault (RTC unplugged, SD card missing, stuck I2C bus) the core
// prints an error line. That print goes out over the same USB serial as our own
// output, and if the transmit buffer is busy (e.g. the boot banner) it WAITS.
// Seen on the bench: one loop pass took 175 ms with the RTC unplugged.
// The firmware already reports these faults itself (status line: RTC FAIL,
// sensors n/6, SD NOT MOUNTED), so the core's messages are not needed.
//
// Two sources, two switches:
//   - Arduino-layer messages (log_e, e.g. "i2cWrite(): ... failed", "Card Failed!")
//     are removed at compile time: -DCORE_DEBUG_LEVEL=0 in platformio.ini.
//   - ESP-IDF driver messages (ESP_LOGE, e.g. "I2C transaction timeout detected")
//     come from precompiled libraries; silence() discards them at run time.
//
// NOT affected, still printed (B3 test 11 needs them):
//   - the boot ROM's "rst:0x.. (reason)" line
//   - the task watchdog's "Task watchdog got triggered" lines (early log, sent
//     straight to the UART)
//   - the panic handler's report and backtrace ("abort() was called ...")
//   - the reset reason in our banner (read from the chip, not from a log)

namespace corelog {

void silence();   // BOOT ONLY. Call first thing in setup()

}  // namespace corelog
