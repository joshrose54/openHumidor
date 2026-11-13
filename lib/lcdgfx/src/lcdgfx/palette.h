#pragma once
// The LCD's colours, approved by Josh at stage Q4 from the screen pictures. Spec 5.5 names this
// file as the palette; the web dashboard uses the same values. Dark, warm neutrals (a dim room,
// and the backlight dims to 20%), one colour per mode.

#include "lcdgfx/canvas.h"

namespace lcdgfx {
namespace pal {

constexpr uint16_t kBg       = rgb565(0x14, 0x11, 0x0F);   // background
constexpr uint16_t kSurface  = rgb565(0x2A, 0x24, 0x20);   // buttons
constexpr uint16_t kLine     = rgb565(0x3A, 0x33, 0x2E);   // separators
constexpr uint16_t kText     = rgb565(0xF3, 0xED, 0xE7);
constexpr uint16_t kMuted    = rgb565(0xA6, 0x9C, 0x93);   // captions, setpoints
constexpr uint16_t kFaint    = rgb565(0x5E, 0x56, 0x50);   // a button that can't be pressed
constexpr uint16_t kCool     = rgb565(0x62, 0xB8, 0xEB);   // Cooling
constexpr uint16_t kHeat     = rgb565(0xF2, 0x8C, 0x4E);   // Heating
constexpr uint16_t kHumid    = rgb565(0x46, 0xC2, 0xAE);   // Humidifying
constexpr uint16_t kDry      = rgb565(0xE0, 0xB8, 0x50);   // Drying (RH strip)
constexpr uint16_t kStandby  = rgb565(0xA6, 0x9C, 0x93);   // Standby
constexpr uint16_t kAccent   = rgb565(0xD9, 0xA5, 0x5B);   // Starting, Updating, Allow update, Save
constexpr uint16_t kFault    = rgb565(0xEF, 0x5A, 0x55);   // Fault, and fault alerts
constexpr uint16_t kWarn     = rgb565(0xF2, 0xB3, 0x3D);   // other alerts; the BENCH / TEST tag
constexpr uint16_t kFaultBg  = rgb565(0x4A, 0x1E, 0x1C);   // the alert line behind a fault
constexpr uint16_t kWarnBg   = rgb565(0x43, 0x33, 0x17);   // the alert line behind other alerts

}  // namespace pal
}  // namespace lcdgfx
