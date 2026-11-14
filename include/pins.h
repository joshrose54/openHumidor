#pragma once
// ALL pins, in one place: Rev Q, the cabinet board, ESP32-S3 (spec 3A.4), as pins_revq. The build
// picks it with -DWINEADOR_BOARD_REVQ (platformio.ini), and the drivers use it as plain `pins::`.
// Rev N, the bench board, is retired (git tag rev-n-last). lib/core never includes this file.
//
// Always compiled, so the Mac-side test test_board_map can check every value.

#include <stdint.h>

// ======================================================================== Rev Q
// Spec 3A.4. Stage Q1 names every pin; the drivers for the new ones (watchdog, SD power,
// I2C1, 12V sense, blowers, ion, LCD, touch) arrive at stage Q2. Until a driver touches a
// pin it stays at its hardware default: blowers full speed, ion off, hardware watchdog
// disabled, SD card powered, backlight on.
namespace pins_revq {

constexpr int kBoot = 0;          // BOOT button, TP11 ("IO0"). 10k pull-up. Never driven
constexpr int kWatchdog = 1;      // hardware watchdog WDI (U5). Input until the loop runs, then toggled every pass (3A.11)
constexpr int kSpare = 2;         // J23 pin 4 ("IO2"). The one unrestricted spare

// The four load signals — each through U9 (74HCT08), 10k pull-down on the pin, as Rev P
constexpr int kCoolingSsr = 4;    // Q2, SSR. Live only when K1 de-energised
constexpr int kHeater = 5;        // Q3 + Q11, heater. Live only when K1 energised
constexpr int kRelayK1 = 6;       // Q5, K1 coil. HIGH = energised = heating side
constexpr int kHumidifier = 7;    // Q4, humidifier fan. Software guards only

constexpr int k12vSense = 8;      // ADC1 channel 7, 100k/15k divider: 12V reads 1.57V. 12dB attenuation

// LCD on SPI2 (FSPI), 15 MHz or less. No MISO: the screen is never read
constexpr int kLcdDc = 9;         // data/command ("RS")
constexpr int kLcdCs = 10;        // 10k pull-up
constexpr int kLcdMosi = 11;
constexpr int kLcdSck = 12;
constexpr int kLcdBacklight = 13; // the LCD pulls it high: on until driven; PWM dims it

// I2C1: RTC (0x68) and touch, 100 kHz
constexpr int kI2c1Scl = 14;
constexpr int kI2c1Sda = 21;

constexpr int kBlowerPwm = 15;    // about 100 kHz, high = on, duty = speed. 10k pull-UP: undriven = FULL speed

// Plenum fan, as Rev P
constexpr int kFanTach = 16;      // 1k pull-up, 1uF. Read from GPIO_IN_REG bit 16 (spec 5.3)
constexpr int kFanPwm = 18;       // 25 kHz. Undriven = full speed

constexpr int kIon = 17;          // ion generator, Q6, no U9. High = on. 10k pull-down: undriven = off

// GPIO19 / 20: native USB D- / D+ (flashing only). Not used by the firmware.

// I2C0: mux only (sensors on mux channels 0-5), 25 kHz. Named as the mux bus (kI2cSda / kI2cScl).
constexpr int kI2cSda = 35;
constexpr int kI2cScl = 36;
constexpr int kMuxReset = 37;     // TCA9548A RST, active low, 10k pull-up

// Micro-SD on SPI3 (HSPI)
constexpr int kSdMiso = 38;
constexpr int kSdPower = 39;      // Q10. HIGH cuts power; to switch on: INPUT, pull-up off (never drive low). 3A.11
constexpr int kSdSck = 40;
constexpr int kSdMosi = 41;
constexpr int kSdCs = 42;

// UART0 console, J23 pins 2 and 3 (Serial; ARDUINO_USB_CDC_ON_BOOT=0)
constexpr int kConsoleTx = 43;
constexpr int kConsoleRx = 44;

constexpr int kTouchInt = 47;     // 10k pull-up
constexpr int kLcdTouchReset = 48;  // resets the LCD AND the touch controller together. 10k pull-up

// GPIO3, 45, 46: not connected.

}  // namespace pins_revq

// ======================================================================== which board
#if defined(WINEADOR_BOARD_REVQ)
namespace pins = pins_revq;
#elif !defined(WINEADOR_BOARD_ANY)
#error "No board set: add -DWINEADOR_BOARD_REVQ to the environment in platformio.ini"
#endif
// WINEADOR_BOARD_ANY: only for the Mac-side tests that check the board's tables.

namespace i2caddr {
constexpr uint8_t kMux = 0x70;
constexpr uint8_t kSht40 = 0x44;
constexpr uint8_t kRtc = 0x68;
}  // namespace i2caddr
