#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include "esp_system.h"
#include "wifi_time.h"   // in include/ — sets the RTC from NTP at boot
#include "soc/soc.h"
#include "soc/gpio_reg.h"

// ================================================================ CONFIG
// true  = B1.2 test 12: fan running at 100% during the soak
// false = B1.2 tests 10+11: fan off
constexpr bool     FAN_ENABLED          = true;
constexpr uint8_t  FAN_DUTY_PERCENT     = 100;

constexpr uint32_t RUN_DURATION_MS      = 24UL * 60 * 60 * 1000;  // 24 hours (BP17)
// BP17: heater chopped at 1Hz (50%) for the first hour, through the relay's heating side.
constexpr bool     HEATER_HOUR          = true;
constexpr uint32_t HEATER_HOUR_MS       = 60UL * 60 * 1000;
constexpr uint32_t READ_INTERVAL_MS     = 15000;
constexpr uint32_t STATUS_INTERVAL_MS   = 60000;                  // one-line status every minute
constexpr uint32_t SUMMARY_INTERVAL_MS  = 300000;                 // full table every 5 min

// ================================================================ PINS
constexpr uint8_t MUX_ADDR = 0x70;
constexpr uint8_t SHT_ADDR = 0x44;
constexpr uint8_t RTC_ADDR = 0x68;
// All six sensors. Channels 2-6 compare like-for-like with tests 10+11, which ran
// before the ambient sensor on channel 7 was added.
constexpr uint8_t CHANNELS[] = {2, 3, 4, 5, 6, 7};
constexpr int NUM_SENSORS = 6;
const char* POSITIONS[] = {"top_front", "bottom_front", "mid_front", "mid_rear", "chamber", "ambient"};
// Serial numbers from the build log's device register. Pre-flight refuses to
// start unless each sensor is on its own channel.
constexpr uint32_t EXPECTED_SERIAL[] = {
  0x0AD409ED,   // ch2 top_front
  0x128EA036,   // ch3 bottom_front
  0x0ADBCB62,   // ch4 mid_front
  0x10EDA1C5,   // ch5 mid_rear
  0x11D7FDF3,   // ch6 chamber
  0x0E42EFBA    // ch7 ambient
};

constexpr int SDA_PIN = 21, SCL_PIN = 22;
constexpr int SD_CS = 17, SD_SCK = 18, SD_MISO = 19, SD_MOSI = 23;
constexpr int LED_PIN = 2;
constexpr int FAN_PWM_PIN  = 25;
constexpr int FAN_TACH_PIN = 34;
constexpr int MUX_RST_PIN  = 16;   // TCA9548A RST, pulsed once at boot
constexpr int SSR_PIN = 26, HEATER_PIN = 27, RELAY_PIN = 32, HUMID_PIN = 33;   // load outputs
constexpr uint32_t FAN_PWM_FREQ = 25000;   // Noctua spec
constexpr uint8_t  FAN_PWM_BITS = 8;

SPIClass spi(VSPI);

// ================================================================ FAN TACH
volatile uint32_t tachPulses = 0;   // accepted pulses
volatile uint32_t tachEdges  = 0;   // every transition, real or chatter
volatile uint32_t lowStartUs = 0;
volatile bool     tachWasLow = false;

// A pulse is counted only when the tach line has stayed LOW for at
// least 1ms. Real lows last half a cycle — 6ms or more even at 2420 rpm.
// Chatter on the 1µF filter's slow edges, with no input hysteresis,
// produces lows of microseconds, which never qualify. Works at every
// speed, unlike a fixed time window between pulses.
constexpr uint32_t MIN_LOW_US = 1000;
static inline bool IRAM_ATTR tachIsLow() {
  return ((REG_READ(GPIO_IN1_REG) >> (FAN_TACH_PIN - 32)) & 1) == 0;   // GPIO34
}
void IRAM_ATTR onTach() {
  uint32_t now = micros();
  tachEdges = tachEdges + 1;
  bool low = tachIsLow();
  if (low && !tachWasLow) { lowStartUs = now; tachWasLow = true; }
  else if (!low && tachWasLow) {
    if (now - lowStartUs >= MIN_LOW_US) tachPulses = tachPulses + 1;
    tachWasLow = false;
  }
}
uint32_t edgesTotal = 0, pulsesTotal = 0;

// ================================================================ STATS
struct ChannelStats {
  uint32_t attempts = 0, good = 0;
  uint32_t crcFail = 0, noAck = 0, shortRd = 0, muxFail = 0;
  float tMin = 999, tMax = -999, tSum = 0;
  float hMin = 999, hMax = -999, hSum = 0;
};
ChannelStats stats[NUM_SENSORS];

uint32_t sdWrites = 0, sdFails = 0, sdWorstMs = 0, sdTotalMs = 0;
uint32_t rtcReads = 0, rtcFails = 0;
uint32_t cycles = 0;
// Errors split: during the heater hour vs after it
bool heaterRunning = false, heaterDone = false;
uint32_t heaterStartMs = 0, heaterToggleMs = 0;
bool heaterLevel = false;
uint32_t errHeat = 0, readsHeat = 0, errQuiet = 0, readsQuiet = 0;

// Heap is sampled ONLY in the main loop, before the SD write, at the same
// point every cycle. The baseline is taken after all initialisation is done.
// This fixes the false 36KB "drop" in the first soak run, which came from
// sampling before SD init and again during the summary write.
uint32_t heapBaseline = 0, heapLast = 0;
uint32_t heapMin = UINT32_MAX, heapMax = 0;

uint32_t rpmLast = 0, rpmMin = UINT32_MAX, rpmMax = 0, rpmSum = 0, rpmCount = 0;

// Latest cycle, for the serial status line
float cycTMin = 0, cycTMax = 0, cycHMin = 0, cycHMax = 0;
bool  cycValid = false;
char  lastTs[32] = "--";

char startTs[32] = "unknown";
char logPath[48], sumPath[48];
const char* bootReason = "unknown";
bool sdOk = false, finished = false;

// ================================================================ HELPERS
const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
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

uint8_t crc8(const uint8_t* d, int n) {
  uint8_t c = 0xFF;
  for (int i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) c = (c & 0x80) ? (c << 1) ^ 0x31 : (c << 1);
  }
  return c;
}

bool muxSelect(uint8_t ch) {
  Wire.beginTransmission(MUX_ADDR);
  Wire.write(1 << ch);
  return Wire.endTransmission() == 0;
}
void muxOff() {
  Wire.beginTransmission(MUX_ADDR);
  Wire.write(0);
  Wire.endTransmission();
}

uint8_t bcd2dec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }

bool rtcNow(char* out, size_t n) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)RTC_ADDR, 7) != 7) return false;
  uint8_t s  = bcd2dec(Wire.read() & 0x7F);
  uint8_t mi = bcd2dec(Wire.read());
  uint8_t h  = bcd2dec(Wire.read() & 0x3F);
  Wire.read();
  uint8_t d  = bcd2dec(Wire.read());
  uint8_t mo = bcd2dec(Wire.read() & 0x1F);
  uint8_t y  = bcd2dec(Wire.read());
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 59) return false;
  snprintf(out, n, "20%02u-%02u-%02uT%02u:%02u:%02uZ", y, mo, d, h, mi, s);
  return true;
}

int readSensor(float& t, float& rh) {
  Wire.beginTransmission(SHT_ADDR);
  Wire.write(0xFD);
  if (Wire.endTransmission() != 0) return 1;
  delay(10);
  if (Wire.requestFrom((int)SHT_ADDR, 6) != 6) return 2;
  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();
  if (crc8(b, 2) != b[2] || crc8(b + 3, 2) != b[5]) return 3;
  uint16_t tt = (b[0] << 8) | b[1];
  uint16_t ht = (b[3] << 8) | b[4];
  t  = -45.0f + 175.0f * tt / 65535.0f;
  rh = constrain(-6.0f + 125.0f * ht / 65535.0f, 0.0f, 100.0f);
  return 0;
}

bool readSerial(uint32_t& sn) {
  Wire.beginTransmission(SHT_ADDR);
  Wire.write(0x89);
  if (Wire.endTransmission() != 0) return false;
  delay(10);
  if (Wire.requestFrom((int)SHT_ADDR, 6) != 6) return false;
  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();
  if (crc8(b, 2) != b[2] || crc8(b + 3, 2) != b[5]) return false;
  sn = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[3] << 8) | b[4];
  return true;
}

// ================================================================ PRE-FLIGHT
// Checks everything before the run starts, so a missing sensor or a
// dead fan shows up now rather than two hours later.
uint32_t fanStartMs = 0;

bool preflight() {
  bool ok = true;
  Serial.println("\nPRE-FLIGHT");

  char ts[32];
  bool rtcOk = rtcNow(ts, sizeof(ts));
  Serial.printf("  RTC              %s\n", rtcOk ? ts : "NOT RESPONDING");
  if (!rtcOk) ok = false;

  for (int i = 0; i < NUM_SENSORS; i++) {
    float t = 0, rh = 0;
    uint32_t sn = 0;
    int e = 1;
    bool snOk = false;
    if (muxSelect(CHANNELS[i])) {
      delay(2);
      snOk = readSerial(sn);
      e = readSensor(t, rh);
    }
    muxOff();

    if (e != 0 || !snOk) {
      Serial.printf("  ch%u %-13s NOT RESPONDING\n", CHANNELS[i], POSITIONS[i]);
      ok = false;
    } else if (sn != EXPECTED_SERIAL[i]) {
      // Whose sensor is this?
      int owner = -1;
      for (int j = 0; j < NUM_SENSORS; j++) if (EXPECTED_SERIAL[j] == sn) owner = j;
      Serial.printf("  ch%u %-13s WRONG SENSOR — found 0x%08lX, expected 0x%08lX\n",
                    CHANNELS[i], POSITIONS[i], sn, EXPECTED_SERIAL[i]);
      if (owner >= 0)
        Serial.printf("                    that is the ch%u %s sensor — swapped\n",
                      CHANNELS[owner], POSITIONS[owner]);
      else
        Serial.println("                    not one of the six registered sensors");
      ok = false;
    } else {
      Serial.printf("  ch%u %-13s 0x%08lX  %6.2f C  %6.2f %%RH  ok\n",
                    CHANNELS[i], POSITIONS[i], sn, t, rh);
    }
  }

  if (FAN_ENABLED) {
    while (millis() - fanStartMs < 4000) delay(50);   // let it reach full speed
    noInterrupts(); tachPulses = 0; tachEdges = 0; interrupts();
    delay(2000);
    noInterrupts(); uint32_t p = tachPulses; tachPulses = 0; tachEdges = 0; interrupts();
    uint32_t rpm = p * 60000UL / 2 / 2000;
    bool fanOk = rpm > 1800;   // B1.1.8 measured 2,280 at 100%
    Serial.printf("  Fan              %lu rpm  %s\n", rpm,
                  fanOk ? "ok" : "NOT AT SPEED — bench PSU on? tach on P34?");
    if (!fanOk) ok = false;
  }
  return ok;
}

// ================================================================ SERIAL OUTPUT
uint32_t totalSensorErrors() {
  uint32_t e = 0;
  for (int i = 0; i < NUM_SENSORS; i++)
    e += stats[i].crcFail + stats[i].noAck + stats[i].shortRd + stats[i].muxFail;
  return e;
}

// One line, every minute
void printStatus() {
  uint32_t el = millis() / 1000;
  uint32_t rem = (RUN_DURATION_MS / 1000) > el ? (RUN_DURATION_MS / 1000) - el : 0;
  Serial.printf("[%3lu:%02lu  left %3lu:%02lu]  %s  cyc %-4lu",
                el / 60, el % 60, rem / 60, rem % 60, lastTs, cycles);
  if (cycValid)
    Serial.printf("  T %5.2f-%5.2f  RH %4.1f-%4.1f", cycTMin, cycTMax, cycHMin, cycHMax);
  else
    Serial.print("  T   no data    RH  no data  ");
  if (FAN_ENABLED) Serial.printf("  fan %4lu rpm", rpmLast);
  Serial.printf("  err %lu  sd %lu/%lu  heap %lu\n",
                totalSensorErrors(), sdWrites, sdFails, heapLast);
}

// Per-channel table, every 5 minutes
void printTable() {
  Serial.println();
  Serial.println("  ---------------------------------------------------------------------------");
  Serial.println("  ch  position        good  err  T mean   T min-max     RH mean  RH min-max");
  for (int i = 0; i < NUM_SENSORS; i++) {
    ChannelStats& s = stats[i];
    uint32_t err = s.crcFail + s.noAck + s.shortRd + s.muxFail;
    Serial.printf("  %-3u %-14s %5lu %4lu", CHANNELS[i], POSITIONS[i], s.good, err);
    if (s.good)
      Serial.printf("   %5.2f   %5.2f-%5.2f    %4.1f   %4.1f-%4.1f\n",
                    s.tSum / s.good, s.tMin, s.tMax, s.hSum / s.good, s.hMin, s.hMax);
    else
      Serial.println("   no valid reads");
  }
  Serial.printf("  SD mean %.1f ms worst %lu ms   RTC fails %lu   heap %lu-%lu (baseline %lu)\n",
                sdWrites ? (float)sdTotalMs / sdWrites : 0.0f, sdWorstMs, rtcFails,
                heapMin == UINT32_MAX ? 0 : heapMin, heapMax, heapBaseline);
  if (FAN_ENABLED && rpmCount)
    Serial.printf("  Fan rpm min %lu max %lu mean %lu\n", rpmMin, rpmMax, rpmSum / rpmCount);
  Serial.println("  ---------------------------------------------------------------------------\n");
}

// ================================================================ SUMMARY
// Uses only values already captured in the loop. Never calls getFreeHeap()
// here, because the summary write itself allocates and would distort it.
void writeSummary(bool isFinal) {
  if (!sdOk) return;
  SD.remove(sumPath);
  File f = SD.open(sumPath, FILE_WRITE);
  if (!f) return;

  char nowTs[32] = "unknown";
  rtcNow(nowTs, sizeof(nowTs));

  f.printf("BP17 SOAK SUMMARY%s\n", isFinal ? "  --  FINAL" : "  --  in progress");
  f.printf("Test:        %s\n", FAN_ENABLED ? "12 (fan running)" : "10+11 (fan off)");
  f.printf("Started:     %s\n", startTs);
  f.printf("Written:     %s\n", nowTs);
  f.printf("Elapsed:     %lu min of %lu\n", millis() / 60000, RUN_DURATION_MS / 60000);
  f.printf("Cycles:      %lu\n", cycles);
  f.printf("Boot reason: %s\n", bootReason);
  if (FAN_ENABLED) f.printf("Fan duty:    %u%%\n", FAN_DUTY_PERCENT);
  f.println();

  f.println("PER CHANNEL");
  f.println("ch  position       good   CRC  NAK short  mux   T min-max    T mean   RH min-max   RH mean");
  for (int i = 0; i < NUM_SENSORS; i++) {
    ChannelStats& s = stats[i];
    f.printf("%-3u %-13s %5lu %5lu %4lu %5lu %4lu",
             CHANNELS[i], POSITIONS[i], s.good, s.crcFail, s.noAck, s.shortRd, s.muxFail);
    if (s.good)
      f.printf("   %5.2f-%5.2f  %6.2f   %5.1f-%5.1f   %5.1f\n",
               s.tMin, s.tMax, s.tSum / s.good, s.hMin, s.hMax, s.hSum / s.good);
    else
      f.println("   no valid reads");
  }

  if (FAN_ENABLED) {
    f.println("\nFAN");
    if (rpmCount)
      f.printf("  RPM last %lu, min %lu, max %lu, mean %lu\n",
               rpmLast, rpmMin, rpmMax, rpmSum / rpmCount);
    else
      f.println("  no tach readings");
    if (rpmCount && rpmMin == 0)
      f.println("  WARNING: RPM reached zero — tach lost or fan stalled");
    f.printf("  Tach transitions %lu, accepted pulses %lu — %s\n", edgesTotal, pulsesTotal,
             edgesTotal <= 2 * pulsesTotal + 2 ? "clean" : "chatter rejected by the 1ms width check");
  }

  f.println("\nSD CARD");
  f.printf("  writes %lu, failures %lu\n", sdWrites, sdFails);
  f.printf("  latency mean %.1f ms, worst %lu ms\n",
           sdWrites ? (float)sdTotalMs / sdWrites : 0.0f, sdWorstMs);

  f.println("\nRTC");
  f.printf("  reads %lu, failures %lu\n", rtcReads, rtcFails);

  f.println("\nHEAP  (sampled in the main loop only, baseline after init)");
  f.printf("  baseline %lu, last %lu, min %lu, max %lu\n",
           heapBaseline, heapLast, heapMin, heapMax);
  f.printf("  change since baseline %+ld bytes\n", (long)heapLast - (long)heapBaseline);

  uint32_t totalErr = 0, totalAttempts = 0;
  for (int i = 0; i < NUM_SENSORS; i++) {
    totalErr += stats[i].crcFail + stats[i].noAck + stats[i].shortRd + stats[i].muxFail;
    totalAttempts += stats[i].attempts;
  }

  if (HEATER_HOUR) {
    f.println("\nHEATER HOUR vs AFTER");
    f.printf("  heater %s\n", heaterRunning ? "RUNNING (first hour)" : heaterDone ? "hour completed" : "not started");
    f.printf("  errors during heater hour: %lu of %lu reads (%.3f%%)\n", errHeat, readsHeat,
             readsHeat ? errHeat * 100.0 / readsHeat : 0.0);
    f.printf("  errors after:              %lu of %lu reads (%.3f%%)\n", errQuiet, readsQuiet,
             readsQuiet ? errQuiet * 100.0 / readsQuiet : 0.0);
  }

  f.println("\nVERDICT");
  f.printf("  sensor errors: %lu of %lu reads (%.3f%%)\n",
           totalErr, totalAttempts, totalAttempts ? totalErr * 100.0 / totalAttempts : 0.0);
  f.printf("  SD failures:   %s\n", sdFails == 0 ? "none" : "PRESENT");
  f.printf("  RTC failures:  %s\n", rtcFails == 0 ? "none" : "PRESENT");
  f.printf("  Heap:          %s\n",
           (heapMax - heapMin) < 1024 ? "stable" : "VARIED BY OVER 1KB - check the CSV trend");
  f.printf("  Boot:          %s\n",
           strcmp(bootReason, "POWERON") == 0 ? "clean power-on" : "NOT A CLEAN POWER-ON");
  f.println("\nCheck resets.txt — any entry after the first means the board reset mid-test.");

  f.close();
}

// ================================================================ SETUP
void setup() {
  // Load outputs held LOW from the first instruction: nothing switches by accident.
  pinMode(SSR_PIN, OUTPUT);    digitalWrite(SSR_PIN, LOW);
  pinMode(HEATER_PIN, OUTPUT); digitalWrite(HEATER_PIN, LOW);
  pinMode(RELAY_PIN, OUTPUT);  digitalWrite(RELAY_PIN, LOW);
  pinMode(HUMID_PIN, OUTPUT);  digitalWrite(HUMID_PIN, LOW);
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  // 10s grace period to open the serial monitor. LED blinks fast while
  // waiting so you can see it has booted. Nothing starts until this ends.
  for (int i = 10; i > 0; i--) {
    Serial.printf("Starting in %d...\n", i);
    for (int j = 0; j < 5; j++) {
      digitalWrite(LED_PIN, HIGH); delay(100);
      digitalWrite(LED_PIN, LOW);  delay(100);
    }
  }

  bootReason = resetReasonName(esp_reset_reason());

  Serial.println("\n\n########## BP17 24-HOUR SOAK ##########");
  Serial.printf("Test: %s\n", FAN_ENABLED ? "12 — fan running" : "10+11 — fan off");
  Serial.printf("Runs %lu hours then stops by itself.\n", RUN_DURATION_MS / 3600000);
  Serial.printf("Boot reason: %s\n", bootReason);
  Serial.println("LED slow blink = running.  Fast blink = finished, safe to unplug.");
  if (FAN_ENABLED)
    Serial.println("Bench PSU must be ON (12V into J1).");
  if (HEATER_HOUR)
    Serial.println("Heater on J10 3-4 will switch 1s on / 1s off for the FIRST HOUR, then stay off.");

  // ---- fan ----
  if (FAN_ENABLED) {
    pinMode(FAN_TACH_PIN, INPUT);   // required: GPIO34 input buffer is off by default
    tachWasLow = tachIsLow();
    attachInterrupt(digitalPinToInterrupt(FAN_TACH_PIN), onTach, CHANGE);
    ledcAttach(FAN_PWM_PIN, FAN_PWM_FREQ, FAN_PWM_BITS);
    // kickstart per spec: 100% for 1s, then target
    ledcWrite(FAN_PWM_PIN, 255);
    delay(1000);
    ledcWrite(FAN_PWM_PIN, (uint32_t)FAN_DUTY_PERCENT * 255 / 100);
    fanStartMs = millis();
    Serial.printf("Fan: kickstarted, now %u%% at %lu Hz\n", FAN_DUTY_PERCENT, FAN_PWM_FREQ);
  }

  // ---- I2C ----
  pinMode(MUX_RST_PIN, OUTPUT);
  digitalWrite(MUX_RST_PIN, LOW);  delayMicroseconds(100);   // start the mux clean,
  digitalWrite(MUX_RST_PIN, HIGH); delayMicroseconds(100);   // as the firmware will
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(25000);
  muxOff();
  syncTime();                       // RTC from internet time; WiFi off afterwards
  rtcNow(startTs, sizeof(startTs));

  // ---- SD ----
  spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  sdOk = SD.begin(SD_CS, spi, 4000000);
  if (!sdOk) {
    Serial.println("SD MOUNT FAILED. Solid LED.");
    digitalWrite(LED_PIN, HIGH);
    while (true) delay(1000);
  }

  if (!preflight()) {
    Serial.println("\nPRE-FLIGHT FAILED — fix the item above and reset the board.");
    Serial.println("Nothing has been logged. Solid LED.");
    digitalWrite(LED_PIN, HIGH);
    while (true) delay(1000);
  }
  Serial.println("  All present. Starting the run.\n");
  if (HEATER_HOUR) {
    digitalWrite(RELAY_PIN, HIGH);   // heating side of K1
    delay(20);
    heaterRunning = true;
    heaterStartMs = heaterToggleMs = millis();
    heaterLevel = true;
    digitalWrite(HEATER_PIN, HIGH);
    Serial.println("  HEATER HOUR STARTED: heater switching 1s on / 1s off for 60 minutes.\n");
  }

  // Boot log — appended, never overwritten. An entry after the first
  // means the board reset during the test.
  File bl = SD.open("/resets.txt", FILE_APPEND);
  if (bl) {
    bl.printf("%s  boot  reason=%s  test=%s\n",
              startTs, bootReason, FAN_ENABLED ? "12" : "10+11");
    bl.close();
  }

  const char* tag = FAN_ENABLED ? "soak_fan" : "soak";
  snprintf(logPath, sizeof(logPath), "/%s_%.10s.csv", tag, startTs);
  snprintf(sumPath, sizeof(sumPath), "/%s_%.10s_summary.txt", tag, startTs);

  File f = SD.open(logPath, FILE_APPEND);
  if (f) {
    f.print("timestamp,uptime_ms");
    for (int i = 0; i < NUM_SENSORS; i++)
      f.printf(",ch%u_t,ch%u_rh,ch%u_err", CHANNELS[i], CHANNELS[i], CHANNELS[i]);
    if (FAN_ENABLED) f.print(",fan_rpm");
    f.println(",heap_free");
    f.close();
  }

  Serial.printf("Data:    %s\nSummary: %s\nBoots:   /resets.txt\n\n", logPath, sumPath);

  // First summary write allocates buffers that persist. Do it BEFORE taking
  // the heap baseline so it isn't counted as a change.
  writeSummary(false);

  heapBaseline = ESP.getFreeHeap();
  Serial.printf("Heap baseline after init: %lu\n\n", heapBaseline);
  Serial.println("Status every minute. Per-channel table every 5 minutes.");
  Serial.println("Any error prints immediately, prefixed with !!\n");
  Serial.println("[elapsed  left]  timestamp  cycle  T range  RH range  fan  errors  sd ok/fail  heap");
}

// ================================================================ LOOP
void loop() {
  static uint32_t lastRead = 0, lastSummary = 0, lastBlink = 0, lastTach = 0;
  if (heaterRunning) {
    uint32_t now = millis();
    if (now - heaterStartMs >= HEATER_HOUR_MS) {
      digitalWrite(HEATER_PIN, LOW);
      delay(20);
      digitalWrite(RELAY_PIN, LOW);
      heaterRunning = false; heaterDone = true;
      Serial.println("\n  HEATER HOUR FINISHED: heater off, relay off. Soak continues.\n");
    } else if (now - heaterToggleMs >= 1000) {
      heaterToggleMs = now;
      heaterLevel = !heaterLevel;
      digitalWrite(HEATER_PIN, heaterLevel ? HIGH : LOW);
    }
  }

  uint32_t blinkPeriod = finished ? 150 : 1000;
  if (millis() - lastBlink >= blinkPeriod) {
    lastBlink = millis();
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }

  if (finished) return;

  if (millis() >= RUN_DURATION_MS) {
    digitalWrite(HEATER_PIN, LOW); delay(20); digitalWrite(RELAY_PIN, LOW);
    heaterRunning = false;
    writeSummary(true);
    printStatus();
    printTable();
    finished = true;
    if (FAN_ENABLED) ledcWrite(FAN_PWM_PIN, 0);
    Serial.println("\n########## FINISHED — safe to unplug ##########");
    return;
  }

  if (millis() - lastRead >= READ_INTERVAL_MS) {
    lastRead = millis();
    cycles++;

    // ---- fan RPM from tach, 2 pulses per revolution ----
    if (FAN_ENABLED) {
      noInterrupts();
      uint32_t pulses = tachPulses;
      uint32_t edges  = tachEdges;
      tachPulses = 0; tachEdges = 0;
      interrupts();
      pulsesTotal += pulses; edgesTotal += edges;
      uint32_t dt = millis() - lastTach;
      lastTach = millis();
      if (dt > 0 && cycles > 1) {
        rpmLast = (uint32_t)((uint64_t)pulses * 60000ULL / 2 / dt);
        rpmMin = min(rpmMin, rpmLast);
        rpmMax = max(rpmMax, rpmLast);
        rpmSum += rpmLast;
        rpmCount++;
      }
    }

    char ts[32];
    rtcReads++;
    if (!rtcNow(ts, sizeof(ts))) {
      rtcFails++; snprintf(ts, sizeof(ts), "RTC_FAIL");
      Serial.println("  !! RTC read failed");
    }
    strncpy(lastTs, ts, sizeof(lastTs));

    String row = String(ts) + "," + String(millis());

    cycValid = false;
    for (int i = 0; i < NUM_SENSORS; i++) {
      ChannelStats& st = stats[i];
      st.attempts++;
      if (heaterRunning) readsHeat++; else readsQuiet++;
      if (!muxSelect(CHANNELS[i])) {
        st.muxFail++; row += ",,,MUX";
        if (heaterRunning) errHeat++; else errQuiet++;
        Serial.printf("  !! %s  ch%u MUX select failed\n", ts, CHANNELS[i]);
        continue;
      }

      float t, rh;
      int err = readSensor(t, rh);
      if (err == 0) {
        st.good++;
        st.tSum += t; st.hSum += rh;
        st.tMin = min(st.tMin, t); st.tMax = max(st.tMax, t);
        st.hMin = min(st.hMin, rh); st.hMax = max(st.hMax, rh);
        row += "," + String(t, 2) + "," + String(rh, 2) + ",";
        if (!cycValid) { cycTMin = cycTMax = t; cycHMin = cycHMax = rh; cycValid = true; }
        else {
          cycTMin = min(cycTMin, t); cycTMax = max(cycTMax, t);
          cycHMin = min(cycHMin, rh); cycHMax = max(cycHMax, rh);
        }
      } else {
        if (err == 1) st.noAck++; else if (err == 2) st.shortRd++; else st.crcFail++;
        if (heaterRunning) errHeat++; else errQuiet++;
        row += ",,," + String(err);
        Serial.printf("  !! %s  ch%u %s\n", ts, CHANNELS[i],
                      err == 1 ? "no ACK" : err == 2 ? "short read" : "CRC fail");
      }
    }
    muxOff();

    if (FAN_ENABLED) row += "," + String(rpmLast);

    // ---- heap: sampled here, at the same point every cycle, before any write ----
    heapLast = ESP.getFreeHeap();
    heapMin = min(heapMin, heapLast);
    heapMax = max(heapMax, heapLast);
    row += "," + String(heapLast);

    uint32_t t0 = millis();
    File f = SD.open(logPath, FILE_APPEND);
    if (f) {
      f.println(row);
      f.close();
      sdWrites++;
      uint32_t dt = millis() - t0;
      sdTotalMs += dt;
      if (dt > sdWorstMs) sdWorstMs = dt;
    } else {
      sdFails++;
      Serial.printf("  !! %s  SD write failed\n", ts);
    }
  }

  static uint32_t lastStatus = 0;
  if (millis() - lastStatus >= STATUS_INTERVAL_MS && cycles > 0) {
    lastStatus = millis();
    printStatus();
  }

  if (millis() - lastSummary >= SUMMARY_INTERVAL_MS) {
    lastSummary = millis();
    writeSummary(false);
    printTable();
  }
}
