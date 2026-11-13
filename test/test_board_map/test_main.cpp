// Both boards' pin and board tables (include/pins.h, include/board.h), checked on the Mac.
//   Rev N: every value exactly as before stage Q1 (spec 3.1; the B3/B4-proven board).
//   Rev Q: every pin as spec 3A.4; sensor channel n on mux channel n (3A.7).
//   Both:  no pin used twice; sensor channels 0-5 map to the right mux channels.
#define WINEADOR_BOARD_ANY   // compile both blocks, choose neither
#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "pins.h"
#include "board.h"
#include "wineador/types.h"
#include "wineador/sensor_table.h"
#include "wineador/commands.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------- Rev N unchanged
void test_revn_pins_unchanged() {
  namespace p = pins_revn;
  TEST_ASSERT_EQUAL_INT(21, p::kI2cSda);
  TEST_ASSERT_EQUAL_INT(22, p::kI2cScl);
  TEST_ASSERT_EQUAL_INT(16, p::kMuxReset);
  TEST_ASSERT_EQUAL_INT(25, p::kFanPwm);
  TEST_ASSERT_EQUAL_INT(34, p::kFanTach);
  TEST_ASSERT_EQUAL_INT(17, p::kSdCs);
  TEST_ASSERT_EQUAL_INT(18, p::kSdSck);
  TEST_ASSERT_EQUAL_INT(19, p::kSdMiso);
  TEST_ASSERT_EQUAL_INT(23, p::kSdMosi);
  TEST_ASSERT_EQUAL_INT(26, p::kCoolingSsr);
  TEST_ASSERT_EQUAL_INT(27, p::kHeater);
  TEST_ASSERT_EQUAL_INT(32, p::kRelayK1);
  TEST_ASSERT_EQUAL_INT(33, p::kHumidifier);
}

void test_revn_board_settings_unchanged() {
  namespace b = board_revn;
  TEST_ASSERT_EQUAL_UINT(60, b::kSdEventsKept);
  TEST_ASSERT_EQUAL_UINT(12000, b::kSdEventsBytes);
  TEST_ASSERT_EQUAL_UINT(30, b::kSdRowsKept);
  TEST_ASSERT_EQUAL_UINT(14000, b::kSdRowsBytes);
  TEST_ASSERT_EQUAL_UINT(24, b::kSdHourlyKept);
  TEST_ASSERT_EQUAL_UINT(9000, b::kSdHourlyBytes);
  TEST_ASSERT_TRUE(b::kWifiOnlyForNtp);
  TEST_ASSERT_TRUE(b::kSdPauseWhileWifi);
}

// Rev N: channel n -> mux n+2, so the same mux select byte reaches the same sensor as before Q1.
void test_revn_channel_n_is_mux_n_plus_2() {
  for (int i = 0; i < kNumSensors; i++) TEST_ASSERT_EQUAL_UINT(i + 2, board_revn::kSensorMuxChannel[i]);
  TEST_ASSERT_EQUAL_STRING("rev-n (channel n = mux n+2)", board_revn::kLogBoard);
}

// ---------------------------------------------------------------- Rev Q per 3A.4
void test_revq_pins_per_spec_3A4() {
  namespace p = pins_revq;
  TEST_ASSERT_EQUAL_INT(0, p::kBoot);
  TEST_ASSERT_EQUAL_INT(1, p::kWatchdog);
  TEST_ASSERT_EQUAL_INT(2, p::kSpare);
  TEST_ASSERT_EQUAL_INT(4, p::kCoolingSsr);
  TEST_ASSERT_EQUAL_INT(5, p::kHeater);
  TEST_ASSERT_EQUAL_INT(6, p::kRelayK1);
  TEST_ASSERT_EQUAL_INT(7, p::kHumidifier);
  TEST_ASSERT_EQUAL_INT(8, p::k12vSense);
  TEST_ASSERT_EQUAL_INT(9, p::kLcdDc);
  TEST_ASSERT_EQUAL_INT(10, p::kLcdCs);
  TEST_ASSERT_EQUAL_INT(11, p::kLcdMosi);
  TEST_ASSERT_EQUAL_INT(12, p::kLcdSck);
  TEST_ASSERT_EQUAL_INT(13, p::kLcdBacklight);
  TEST_ASSERT_EQUAL_INT(14, p::kI2c1Scl);
  TEST_ASSERT_EQUAL_INT(15, p::kBlowerPwm);
  TEST_ASSERT_EQUAL_INT(16, p::kFanTach);
  TEST_ASSERT_EQUAL_INT(17, p::kIon);
  TEST_ASSERT_EQUAL_INT(18, p::kFanPwm);
  TEST_ASSERT_EQUAL_INT(21, p::kI2c1Sda);
  TEST_ASSERT_EQUAL_INT(35, p::kI2cSda);
  TEST_ASSERT_EQUAL_INT(36, p::kI2cScl);
  TEST_ASSERT_EQUAL_INT(37, p::kMuxReset);
  TEST_ASSERT_EQUAL_INT(38, p::kSdMiso);
  TEST_ASSERT_EQUAL_INT(39, p::kSdPower);
  TEST_ASSERT_EQUAL_INT(40, p::kSdSck);
  TEST_ASSERT_EQUAL_INT(41, p::kSdMosi);
  TEST_ASSERT_EQUAL_INT(42, p::kSdCs);
  TEST_ASSERT_EQUAL_INT(43, p::kConsoleTx);
  TEST_ASSERT_EQUAL_INT(44, p::kConsoleRx);
  TEST_ASSERT_EQUAL_INT(47, p::kTouchInt);
  TEST_ASSERT_EQUAL_INT(48, p::kLcdTouchReset);
}

void test_revq_channel_n_is_mux_n() {
  for (int i = 0; i < kNumSensors; i++) TEST_ASSERT_EQUAL_UINT(i, board_revq::kSensorMuxChannel[i]);
  TEST_ASSERT_EQUAL_STRING("rev-q (channel n = mux n)", board_revq::kLogBoard);
  TEST_ASSERT_FALSE(board_revq::kWifiOnlyForNtp);      // 5.4: WiFi stays connected
  TEST_ASSERT_FALSE(board_revq::kSdPauseWhileWifi);    // 3A.11: card writes not paused for it
}

// ---------------------------------------------------------------- no pin twice
static bool noDuplicates(const int* p, int n) {
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++)
      if (p[i] == p[j]) return false;
  return true;
}

void test_revn_no_pin_twice() {
  namespace p = pins_revn;
  const int all[] = {p::kI2cSda, p::kI2cScl, p::kMuxReset, p::kFanPwm, p::kFanTach, p::kSdCs, p::kSdSck,
                     p::kSdMiso, p::kSdMosi, p::kCoolingSsr, p::kHeater, p::kRelayK1, p::kHumidifier};
  TEST_ASSERT_TRUE(noDuplicates(all, sizeof(all) / sizeof(all[0])));
}

void test_revq_no_pin_twice_and_none_on_reserved_pins() {
  namespace p = pins_revq;
  const int all[] = {p::kBoot, p::kWatchdog, p::kSpare, p::kCoolingSsr, p::kHeater, p::kRelayK1, p::kHumidifier,
                     p::k12vSense, p::kLcdDc, p::kLcdCs, p::kLcdMosi, p::kLcdSck, p::kLcdBacklight, p::kI2c1Scl,
                     p::kBlowerPwm, p::kFanTach, p::kIon, p::kFanPwm, p::kI2c1Sda, p::kI2cSda, p::kI2cScl,
                     p::kMuxReset, p::kSdMiso, p::kSdPower, p::kSdSck, p::kSdMosi, p::kSdCs, p::kConsoleTx,
                     p::kConsoleRx, p::kTouchInt, p::kLcdTouchReset};
  const int n = sizeof(all) / sizeof(all[0]);
  TEST_ASSERT_TRUE(noDuplicates(all, n));
  // 3A.4: GPIO3, 45, 46 not connected; 19/20 are the native USB. GPIO26-32 belong to the
  // module's flash and PSRAM; 33-34 are free on the N8R2 but not wired.
  for (int i = 0; i < n; i++) {
    TEST_ASSERT_TRUE(all[i] != 3 && all[i] != 19 && all[i] != 20 && all[i] != 45 && all[i] != 46);
    TEST_ASSERT_TRUE(all[i] < 26 || all[i] > 34);
  }
}

// ---------------------------------------------------------------- channels 0-5
void test_sensor_table_channels_are_0_to_5_in_order() {
  for (int i = 0; i < kNumSensors; i++) TEST_ASSERT_EQUAL_UINT(i, kSensors[i].channel);
  // the roles of spec 2.3, same serials as the build log
  TEST_ASSERT_EQUAL_STRING("top_front", kSensors[0].position);
  TEST_ASSERT_EQUAL_HEX32(0x0AD409ED, kSensors[0].serial);
  TEST_ASSERT_EQUAL_STRING("chamber", kSensors[kChamberIndex].position);
  TEST_ASSERT_EQUAL_HEX32(0x11D7FDF3, kSensors[kChamberIndex].serial);
  TEST_ASSERT_EQUAL_STRING("ambient", kSensors[kAmbientIndex].position);
  TEST_ASSERT_EQUAL_HEX32(0x0E42EFBA, kSensors[kAmbientIndex].serial);
}

void test_channel_commands_take_0_to_5_only() {
  CommandArgs a;
  for (int c = 0; c <= 5; c++) {
    char line[24];
    snprintf(line, sizeof(line), "exclude ch%d on", c);
    TEST_ASSERT_TRUE(parseCommand(line, false, a) == Command::Exclude);
    TEST_ASSERT_EQUAL_INT(c, a.arg);
  }
  TEST_ASSERT_TRUE(parseCommand("exclude ch6 on", false, a) == Command::Exclude);
  TEST_ASSERT_EQUAL_INT(-2, a.arg);
  TEST_ASSERT_TRUE(parseCommand("crc ch7", true, a) == Command::InjectCrc);
  TEST_ASSERT_EQUAL_INT(-2, a.arg);
  TEST_ASSERT_TRUE(parseCommand("timeout 0", true, a) == Command::InjectTimeout);
  TEST_ASSERT_EQUAL_INT(0, a.arg);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_revn_pins_unchanged);
  RUN_TEST(test_revn_board_settings_unchanged);
  RUN_TEST(test_revn_channel_n_is_mux_n_plus_2);
  RUN_TEST(test_revq_pins_per_spec_3A4);
  RUN_TEST(test_revq_channel_n_is_mux_n);
  RUN_TEST(test_revn_no_pin_twice);
  RUN_TEST(test_revq_no_pin_twice_and_none_on_reserved_pins);
  RUN_TEST(test_sensor_table_channels_are_0_to_5_in_order);
  RUN_TEST(test_channel_commands_take_0_to_5_only);
  return UNITY_END();
}
