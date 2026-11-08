#include "console.h"
#include <Arduino.h>
#include "esp_system.h"
#include "wineador/build_info.h"
#include "wineador/sensor_table.h"
#include "wineador/sensor_health.h"
#include "drivers/i2c_bus.h"

using namespace wineador;

namespace console {

const char* resetReasonName(int r) {
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

static void secs(char* out, size_t n, uint32_t ms) {
  if (ms % 60000 == 0) snprintf(out, n, "%lu min", (unsigned long)(ms / 60000));
  else                 snprintf(out, n, "%lu s", (unsigned long)(ms / 1000));
}

void printBanner(const Settings& s, const BootInfo& b) {
  const SafetySettings& f = s.safety;
  char a[16], c[16], d[16];

  Serial.println();
  Serial.println("=====================================================================");
  Serial.printf ("  WINEADOR CONTROLLER    firmware %s\n", b.version);
  Serial.printf ("  Board Rev N   |   profile %s   |   %s\n", profileName(s.profile),
                 kTestBuild ? "TEST BUILD" : "cabinet build");
  Serial.printf ("  Boot reason: %s\n", b.bootReason);
  Serial.println("=====================================================================");

  if (s.profile == Profile::Bench) {
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println("!!                                                                 !!");
    Serial.println("!!   BENCH PROFILE  -  SHORT TEST TIMERS                           !!");
    Serial.println("!!   NOT FOR THE CABINET. Before installing, flash the esp32dev    !!");
    Serial.println("!!   build (REAL profile) and check this banner has gone.          !!");
    Serial.println("!!                                                                 !!");
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
  }
  if (kTestBuild) {
    Serial.println("*********************************************************************");
    Serial.println("**   TEST BUILD  -  READINGS ARE SCRIPTED, NOT REAL                **");
    Serial.println("**   A cabinet model replaces sensor values. Bus checks stay real. **");
    Serial.println("**   NOT FOR THE CABINET.                                          **");
    Serial.println("*********************************************************************");
  }
  if (kBootDemand) {
    Serial.println("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");
    Serial.println("++   BOOT-DEMAND BUILD  -  HEAT AND HUMIDIFY ASKED FOR FROM BOOT    ++");
    Serial.println("++   For B3 tests 11 and 12. The bulb (J10 3-4) and the humidifier ++");
    Serial.println("++   fan (J11) start when the boot hold ends, then cycle by their  ++");
    Serial.println("++   guards (max run, min off) until power is removed.             ++");
    Serial.println("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");
  }

  if (b.settingsProblem)
    Serial.printf("\n  !! SETTINGS PROBLEM: %s. All loads stay off.\n", b.settingsProblem);

  Serial.println("\n  Timers in force:");
  secs(a, sizeof(a), f.compressorMinOffMs); secs(c, sizeof(c), f.compressorMinOnMs); secs(d, sizeof(d), f.compressorMaxRunMs);
  Serial.printf ("    compressor  min off %-7s min on %-7s max run %s\n", a, c, d);
  secs(a, sizeof(a), f.heaterMinOffMs); secs(c, sizeof(c), f.heaterMinOnMs); secs(d, sizeof(d), f.heaterMaxRunMs);
  Serial.printf ("    heater      min off %-7s min on %-7s max run %s\n", a, c, d);
  secs(a, sizeof(a), f.humidMinOffMs); secs(d, sizeof(d), f.humidMaxRunMs);
  Serial.printf ("    humidifier  min off %-7s                max run %s\n", a, d);
  secs(a, sizeof(a), f.bootHoldMs); secs(c, sizeof(c), s.tuning.crossoverDelayMs);
  Serial.printf ("    boot hold   %-7s crossover %s\n", a, c);
  Serial.printf ("    relay exercise every %lu days: %u cycles, %lu ms energised / %lu ms released\n",
                 (unsigned long)(f.relayExercisePeriodMs / 86400000UL), (unsigned)f.relayExerciseCycles,
                 (unsigned long)f.relayExerciseOnMs, (unsigned long)f.relayExerciseOffMs);

  Serial.println("\n  Hardware:");
  Serial.printf ("    watchdog    %s\n", b.watchdogOk ? "5 s, armed" : "!! FAILED TO ARM");
  Serial.printf ("    I2C         25 kHz, ESP32 internal pull-ups %s\n",
                 b.pullupsOff ? "OFF (correct)" : "!! ON - SHOULD BE OFF");
  if (b.sdOk) Serial.printf("    SD card     mounted, %llu MB, mount took %lu ms at boot (logging arrives at stage 6)\n",
                            (unsigned long long)b.sdMB, (unsigned long)b.sdMountMs);
  else        Serial.printf("    SD card     NOT MOUNTED after %lu ms (logging arrives at stage 6; control unaffected)\n",
                            (unsigned long)b.sdMountMs);
  if (kBootDemand)
    Serial.println("    loads       off now; heat + humidify asked for from boot. Guards and interlock decide");
  else if (kTestBuild)
    Serial.println("    loads       off. Only a scenario (run <n>) asks for a load; guards and interlock decide");
  else
    Serial.println("    loads       off. No control logic yet (stage 4), so nothing asks for a load");
  Serial.printf ("    plenum fan  kickstart 100%% for 1 s, then idle %u%%; on a stall, a kickstart at once and every %lu s\n",
                 (unsigned)s.tuning.fanIdlePct, (unsigned long)(f.fanStallRetryMs / 1000));
  Serial.println("    WiFi / NTP  connecting in the background (pool.ntp.org, time.google.com; every 24 h)");
  Serial.println();
  printHelp(kTestBuild);
  Serial.println("  Sensor serials print when the boot sensor check finishes (a second or so).");
  Serial.println("=====================================================================");
}

void printSensorCheck() {
  Serial.println();
  Serial.println("  SENSOR CHECK (boot) - serial read on each channel, values raw and real");
  for (int i = 0; i < kNumSensors; i++) {
    const SensorInfo& info = kSensors[i];
    const i2cbus::SerialResult& sr = i2cbus::serial(i);
    const SensorReading& rd = i2cbus::readings().s[i];
    Serial.printf("    ch%u %-13s ", info.channel, info.position);
    if (sr.bus != BusResult::Ok) {
      Serial.printf("serial: %s\n", busResultName(sr.bus));
      continue;
    }
    if (sr.serial == info.serial) {
      Serial.printf("0x%08lX matches %s", (unsigned long)sr.serial, info.label);
    } else {
      int owner = sensorIndexForSerial(sr.serial);
      Serial.printf("0x%08lX WRONG SENSOR - expected %s", (unsigned long)sr.serial, info.label);
      if (owner >= 0) Serial.printf(" (this is the ch%u %s probe)", kSensors[owner].channel, kSensors[owner].position);
      else            Serial.print(" (not one of the six)");
    }
    if (rd.bus == BusResult::Ok) Serial.printf("   %6.2f C  %6.2f %%RH raw -> %6.2f %%RH corrected\n", rd.tempC, rd.rhRawPct,
                                               correctedRh(i, rd.rhRawPct));
    else                         Serial.printf("   reading: %s\n", busResultName(rd.bus));
  }
  Serial.println("    A wrong or missing sensor is excluded (4.3, 5.3); exclusions and recoveries print as they happen.");
  Serial.println("    Values here are real. In a test build the logic uses scripted values; bus checks stay real.");
  Serial.println();
}

void printHelp(bool testBuild) {
  Serial.println("  Commands (type, then Enter):");
  Serial.println("    boot      reprint this banner and the boot reason");
  Serial.println("    help      this list");
  Serial.println("    exercise  relay exercise now: compressor and heater held off, 3 cycles of K1");
  if (testBuild) {
    Serial.println("    run <n>   TEST BUILD ONLY: play B3 test n's scenario (1, 2, 3, 4, 5, 6, 13, 15)");
    Serial.println("    stop      TEST BUILD ONLY: end the scenario; nothing asked for");
    Serial.println("    stall     TEST BUILD ONLY: lock up the main loop - the watchdog resets the board in 5 s");
    Serial.println("    resets    TEST BUILD ONLY: the reset record ('resets clear' empties it)");
  }
}

}  // namespace console
