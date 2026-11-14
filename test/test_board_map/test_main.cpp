// The board's pin and board tables (include/pins.h, include/board.h), checked on the Mac.
//   Rev Q: every pin as spec 3A.4; sensor channel n on mux channel n (3A.7); no pin used twice.
//   (Rev N, retired, had channel n on mux n+2: git tag rev-n-last.)
//   Console: a channel typed as a number, position name or probe label (spec 5.5).
#define WINEADOR_BOARD_ANY   // compile the tables without choosing a board
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
}

// ---------------------------------------------------------------- no pin twice
static bool noDuplicates(const int* p, int n) {
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++)
      if (p[i] == p[j]) return false;
  return true;
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

// Spec 5.5, serial console: a channel by number, position name or probe label; anything else refused.
void test_channel_by_position_name_or_probe_label() {
  CommandArgs a;
  for (int i = 0; i < kNumSensors; i++) {
    char line[40];
    snprintf(line, sizeof(line), "exclude %s on", kSensors[i].position);
    TEST_ASSERT_TRUE(parseCommand(line, false, a) == Command::Exclude);
    TEST_ASSERT_EQUAL_INT(i, a.arg);
    snprintf(line, sizeof(line), "exclude %s off", kSensors[i].label);
    TEST_ASSERT_TRUE(parseCommand(line, false, a) == Command::Exclude);
    TEST_ASSERT_EQUAL_INT(i, a.arg);
    TEST_ASSERT_EQUAL_INT(0, a.arg2);
  }
  // the examples in the spec, and any case
  TEST_ASSERT_TRUE(parseCommand("exclude A1C5 on", false, a) == Command::Exclude);
  TEST_ASSERT_EQUAL_INT(3, a.arg);
  TEST_ASSERT_TRUE(parseCommand("exclude mid_rear on", false, a) == Command::Exclude);
  TEST_ASSERT_EQUAL_INT(3, a.arg);
  TEST_ASSERT_EQUAL_INT(3, channelWord("a1c5"));
  TEST_ASSERT_EQUAL_INT(0, channelWord("TOP_FRONT"));
  TEST_ASSERT_EQUAL_INT(0, channelWord("09ed"));
  // test-build commands take them too
  TEST_ASSERT_TRUE(parseCommand("crc chamber", true, a) == Command::InjectCrc);
  TEST_ASSERT_EQUAL_INT(kChamberIndex, a.arg);
  TEST_ASSERT_TRUE(parseCommand("timeout EFBA", true, a) == Command::InjectTimeout);
  TEST_ASSERT_EQUAL_INT(kAmbientIndex, a.arg);
}

void test_unknown_sensor_names_are_refused() {
  CommandArgs a;
  TEST_ASSERT_TRUE(parseCommand("exclude mid_back on", false, a) == Command::Exclude);   // no such position
  TEST_ASSERT_EQUAL_INT(-2, a.arg);
  TEST_ASSERT_TRUE(parseCommand("exclude A1C6 on", false, a) == Command::Exclude);       // no such probe
  TEST_ASSERT_EQUAL_INT(-2, a.arg);
  TEST_ASSERT_TRUE(parseCommand("exclude top on", false, a) == Command::Exclude);        // part of a name is not a name
  TEST_ASSERT_EQUAL_INT(-2, a.arg);
  TEST_ASSERT_TRUE(parseCommand("exclude A1C on", false, a) == Command::Exclude);        // nor part of a label
  TEST_ASSERT_EQUAL_INT(-2, a.arg);
  TEST_ASSERT_TRUE(parseCommand("crc fridge", true, a) == Command::InjectCrc);
  TEST_ASSERT_EQUAL_INT(-2, a.arg);
  TEST_ASSERT_EQUAL_INT(-2, channelWord(""));
  TEST_ASSERT_EQUAL_INT(-2, channelWord("ch"));
}

// Every position and label is unique, and none could be read as a channel number.
void test_names_and_labels_are_unambiguous() {
  for (int i = 0; i < kNumSensors; i++) {
    TEST_ASSERT_EQUAL_INT(i, channelWord(kSensors[i].position));
    TEST_ASSERT_EQUAL_INT(i, channelWord(kSensors[i].label));
    for (const char* c = kSensors[i].label; *c; c++) {
      if (*c < '0' || *c > '9') goto has_letter;
    }
    TEST_FAIL_MESSAGE("a probe label is all digits: it would clash with channel numbers");
  has_letter:;
  }
}

void test_channel_echo_names_what_was_acted_on() {
  char e[96];
  formatChannelEcho(e, sizeof(e), 3, "excluded");
  TEST_ASSERT_EQUAL_STRING("ch3 = mid_rear (probe A1C5): excluded", e);    // the spec's example
  formatChannelEcho(e, sizeof(e), 0, "back in use");
  TEST_ASSERT_EQUAL_STRING("ch0 = top_front (probe 09ED): back in use", e);
  formatChannelEcho(e, sizeof(e), 6, "excluded");
  TEST_ASSERT_EQUAL_STRING("no such channel: excluded", e);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_revq_pins_per_spec_3A4);
  RUN_TEST(test_revq_channel_n_is_mux_n);
  RUN_TEST(test_revq_no_pin_twice_and_none_on_reserved_pins);
  RUN_TEST(test_sensor_table_channels_are_0_to_5_in_order);
  RUN_TEST(test_channel_commands_take_0_to_5_only);
  RUN_TEST(test_channel_by_position_name_or_probe_label);
  RUN_TEST(test_unknown_sensor_names_are_refused);
  RUN_TEST(test_names_and_labels_are_unambiguous);
  RUN_TEST(test_channel_echo_names_what_was_acted_on);
  return UNITY_END();
}
