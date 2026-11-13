# Porting the firmware to the next board — planning note

*2026-11-11. A plan only: no next-board code until the board designer's proposal is back and the
spec has been updated for it. Based on `docs/next-board-changes.md`.*

The architecture was built for this: the control logic in `lib/core` is plain C++ with no hardware
calls, all pins are in `include/pins.h`, and the per-board sizes and behaviours are in
`include/board.h`. Most of the port is drivers and two headers; the control rules and their native
tests should not change.

## What changes in the firmware

| Area | Change | Where |
|---|---|---|
| **Build target** | ESP32-S3-WROOM-1 (N8R2 proposed): a new `[env]` per build kind with an S3 board definition, 8 MB flash, quad PSRAM on, on the same pinned platform (pioarduino 55.03.311). Check that the platform's S3 support matches what the board needs before relying on it. | `platformio.ini` |
| **Partitions** | 8 MB: two larger OTA slots and NVS. If NVS moves, the saved user settings start from defaults once. | a new partition CSV |
| **Pins** | A new `pins.h` from the designer's pin map; the load outputs on pins that stay quiet through boot and reset. We check the map against the S3's strapping and USB pins (0, 3, 45, 46; 19, 20; 43, 44). | `include/pins.h` |
| **Per-board settings** | WiFi always on (`kWifiOnlyForNtp = false`), card writes while WiFi is on (`kSdPauseWhileWifi = false`), larger outage buffers in PSRAM. The SD retry and verified writes stay. | `include/board.h`, `sd_log` |
| **Serial and USB** | The S3's own USB (USB-Serial-JTAG / CDC) replaces the CP2102. Serial output must never block when no host is connected, and boot messages printed before the host opens the port are lost (the banner can be reprinted with `boot`). The `monitor_rts` / `monitor_dtr` settings and our own serial scripts need re-checking (see risks). | `main.cpp`, `console`, `platformio.ini`, the Mac scripts |
| **I²C mux** | The TCA9548A chip on the board, address 0x70, RST on a GPIO (bus-fault recovery as now). **Sensors on channels 0–5** instead of 2–7, so `kFirstChannel` becomes 0. Channels 6 and 7 are brought out as spare connectors, with no firmware use yet. | `lib/core/types.h`, `sensor_table`, `i2c_bus` |
| **RTC** | A new driver for whichever on-board RTC is chosen, with an "oscillator stopped" (lost-time) flag in place of the DS3231's OSF. If the RTC gets its own I²C bus (the second controller), `i2c_bus` splits into two buses, each with its own recovery. | `i2c_bus` (or a new `rtc` driver) |
| **SD** | The micro-SD slot on the main board, with its own SPI host and chip select. The SD task is otherwise unchanged. The SPI clock is set from the designer's advice and re-proven. | `sd_card`, `pins.h` |
| **LCD and touch** | New drivers: ST7796 over SPI (on its own SPI host, so it never shares the bus with the SD card), FT6336U touch on I²C with CTP_RST and CTP_INT, backlight on PWM. A display task, never the loop, does the drawing. What the screen shows and what touch does are not yet in the spec. | new `drivers/lcd`, `drivers/touch`, an app-level screen |
| **Blowers** | New: speed set by the ESP32 as a voltage. The default with no signal is full 12 V, so the firmware only ever slows them. The S3 has no DAC, so this is PWM smoothed to DC or a regulator setpoint, depending on the design. New settings and rules are needed in the spec (at present the blowers are always on). | new `drivers/blowers`, settings, `lib/core` rules |
| **Ion generator** | New: one low-side output, off at boot, on its own GPIO, outside the 74HCT08 gating. Its control rules (when, how long, any guards) are not yet in the spec. | `outputs`, settings, `lib/core` rules |
| **WiFi** | Always connected, reconnecting by itself, with WiFi and network buffers in PSRAM, so the 30 KB internal-heap floor holds at all times. Check whether the prebuilt Arduino S3 libraries already allow this; if not, it needs a custom ESP-IDF configuration (see risks). | `time_sync`, build configuration |
| **OTA** | Wireless updates into the second slot, with rollback if the new firmware doesn't confirm itself. The loads' state during an update is a spec decision (see risks). | new `drivers/ota` |
| **Boot and reset** | The supply supervisor on EN gives a clean reset after a supply dip. Show it in the boot reason and the reset record. No load may pulse at boot; the blowers start at full speed. | `reset_record`, `outputs` |
| **Logs** | Sensor columns become ch0–ch5, so the log headers say so. Rev N logs keep ch2–ch7; the Mac checker must handle both. | `logbook`, `tools/check_logs.py` |

## B3 tests to re-run in BP2

The control logic doesn't change, but every output, pin, bus and driver under it does.

- **Re-run, because their hardware path changes:**
  - **1, 2, 3:** guards and the interlock, on the new pins.
  - **4, 5, 6:** sensor failure and divergence on channels 0–5; the fan tach on its new pin.
  - **11, 12:** the watchdog, and a power interruption with the new supervisor.
  - **13:** the relay exercise.
  - **14:** the profile safeguard, for the new build targets.
  - **15:** I²C bus recovery, with the on-board mux and possibly two buses.
  - **16:** RTC lost time, with the new RTC.
  - **18, 24:** SD failure and logging, on the new slot.
  - **23:** the plenum fan rules.
- **Re-run quickly, because they use scripted readings and the same logic, but drive the new
  outputs:** 7, 8, 9, 10, 17, 19, 20, 21, 22.
- **New tests BP2 will need:**
  - blower speed control, including full speed by default;
  - the ion generator, off at boot;
  - LCD and touch, with the loop time unaffected while drawing;
  - OTA, including rollback;
  - USB plugged in and out and the monitor opened and closed with no restart;
  - BP2's deep SD test, with WiFi on all the time;
  - the 30 KB heap floor at all times;
  - a short soak.

## Hard or risky for the firmware

1. **Native USB serial may restart the board.** The S3's USB-Serial-JTAG can reset the chip when a
   host toggles DTR/RTS, which is how flashing resets it. Opening a serial monitor could therefore
   restart the board, against the requirement that it must not. We need to find, on the board,
   monitor and script settings that never reset it while flashing still works.
2. **PSRAM for WiFi's buffers.** The 30 KB floor at all times depends on WiFi and the network stack
   using PSRAM. If the prebuilt Arduino S3 libraries don't do this, we need a custom ESP-IDF
   configuration, which rebuilds the framework instead of using the pinned prebuilt one. That makes
   builds slower and less reproducible. The spec should also say the floor means internal RAM, since
   PSRAM is measured separately.
3. **OTA and the guards.** Writing flash pauses code running from flash on both cores for tens of ms
   per erase. The guards and the 5 s watchdog must survive an update, and the spec should say what
   the loads do during one: held off, or carry on.
4. **The display's load on the board.** Drawing on a 480×320 screen over SPI and a cable takes CPU
   time and memory (frame buffers in PSRAM). It must run in its own task, at a priority that can
   never delay the loop or the SD task. A graphics library (e.g. LVGL) would add flash and memory.
5. **Blower voltage control is new safety behaviour, and its default is inverted:** no signal means
   full speed. A crash or a floating pin must leave them at full speed, and that needs checking on
   the real circuit through boot, reset and a watchdog reset.
6. **The RTC chip isn't chosen yet.** Its lost-time flag and its behaviour on the backup cell decide
   how the firmware handles a power-up. The design must have an equivalent of the DS3231's OSF.
7. **The renumbered sensor channels** break continuity between Rev N and next-board logs. The
   checker, and any later dashboard graphs spanning both boards, must handle it.
8. **Spec gaps to fill before coding:** what the LCD shows and what touch does; the blower speed
   rules; the ion generator rules; the OTA policy and how it's secured; WiFi reconnect and
   always-on behaviour with the dashboard.
9. **The SD cure is the hardware's job.** The firmware keeps verified writes and the retry, but BP2's
   deep SD test, with WiFi always on and zero first-try failures, proves the board, not the code.
