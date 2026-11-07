#include <Arduino.h>
#include <Wire.h>
#include "wifi_time.h"   // in include/ — sets the RTC from NTP at boot

// =================================================================
// Quick sensor check, Rev N board — run before the BP17 soak
//
// Checks every I2C device through the board's own connectors:
//   - the TCA9548A mux answers, and its reset line (P16) works
//   - each SHT40 on J3-J8 answers on its own channel, with the right
//     serial number, and gives a sensible temperature and humidity
//   - the DS3231 RTC on J9 answers and its clock is running
// Then repeats the readings every 5 seconds so you can breathe on a
// sensor and watch its humidity rise — proves which plug is which.
//
// Nothing else is switched: load outputs are held LOW throughout.
// Power order: 12V on before USB in.
// =================================================================

constexpr int SDA_PIN = 21, SCL_PIN = 22, MUX_RST_PIN = 16;
constexpr uint8_t MUX_ADDR = 0x70, SHT_ADDR = 0x44, RTC_ADDR = 0x68;

constexpr uint8_t CHANNELS[] = {2, 3, 4, 5, 6, 7};
constexpr int NUM_SENSORS = 6;
const char* CONNECTOR[] = {"J3", "J4", "J5", "J6", "J7", "J8"};
const char* POSITIONS[] = {"top_front", "bottom_front", "mid_front", "mid_rear", "chamber", "ambient"};
constexpr uint32_t EXPECTED_SERIAL[] = {
  0x0AD409ED, 0x128EA036, 0x0ADBCB62, 0x10EDA1C5, 0x11D7FDF3, 0x0E42EFBA
};

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
bool present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
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
  uint16_t tt = (b[0] << 8) | b[1], ht = (b[3] << 8) | b[4];
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
uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
bool rtcSeconds(uint8_t& sec, char* out, size_t n) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)RTC_ADDR, 7) != 7) return false;
  uint8_t s = bcd2dec(Wire.read() & 0x7F), mi = bcd2dec(Wire.read()), h = bcd2dec(Wire.read() & 0x3F);
  Wire.read();
  uint8_t d = bcd2dec(Wire.read()), mo = bcd2dec(Wire.read() & 0x1F), y = bcd2dec(Wire.read());
  sec = s;
  snprintf(out, n, "20%02u-%02u-%02u %02u:%02u:%02u UTC", y, mo, d, h, mi, s);
  return true;
}

void line(const char* item, bool ok, const char* detail) {
  Serial.printf("  %-34s %-5s %s\n", item, ok ? "PASS" : "FAIL", detail);
}

void setup() {
  for (int p : {26, 27, 32, 33}) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n#################################################");
  Serial.println("  SENSOR CHECK - Rev N board");
  Serial.println("#################################################");

  pinMode(MUX_RST_PIN, OUTPUT);
  digitalWrite(MUX_RST_PIN, HIGH);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(25000);
  syncTime();                       // RTC from internet time; WiFi off afterwards

  int fails = 0;
  char buf[64];

  // --- mux and its reset line ---
  bool mux = present(MUX_ADDR);
  line("Mux (U2) answers at 0x70", mux, mux ? "" : "check U2 is seated on the back");
  if (!mux) fails++;
  if (mux) {
    muxSelect(3);
    digitalWrite(MUX_RST_PIN, LOW); delayMicroseconds(100);
    digitalWrite(MUX_RST_PIN, HIGH); delayMicroseconds(100);
    Wire.requestFrom((int)MUX_ADDR, 1);
    uint8_t reg = Wire.available() ? Wire.read() : 0xFF;
    bool rstOk = (reg == 0x00);
    snprintf(buf, sizeof(buf), "register after reset = 0x%02X (want 0x00)", reg);
    line("Mux reset line (P16) clears it", rstOk, buf);
    if (!rstOk) fails++;
  }
  muxOff();

  // --- RTC on the main bus ---
  uint8_t s1, s2;
  char ts[40] = "";
  bool rtc = rtcSeconds(s1, ts, sizeof(ts));
  if (rtc) { delay(2100); rtc = rtcSeconds(s2, ts, sizeof(ts)) && s2 != s1; }
  line("RTC (J9) answers and is ticking", rtc, rtc ? ts : "check the RTC lead in J9");
  if (!rtc) fails++;

  // --- each sensor ---
  Serial.println();
  for (int i = 0; i < NUM_SENSORS; i++) {
    char item[40];
    snprintf(item, sizeof(item), "%s ch%u %s", CONNECTOR[i], CHANNELS[i], POSITIONS[i]);
    if (!muxSelect(CHANNELS[i])) { line(item, false, "mux select failed"); fails++; continue; }
    uint32_t sn = 0; float t = 0, rh = 0;
    bool snOk = readSerial(sn);
    int e = readSensor(t, rh);
    muxOff();
    if (!snOk || e) {
      line(item, false, "no sensor answering - plug in? pins 1-4 in order?");
      fails++;
    } else if (sn != EXPECTED_SERIAL[i]) {
      int owner = -1;
      for (int j = 0; j < NUM_SENSORS; j++) if (EXPECTED_SERIAL[j] == sn) owner = j;
      if (owner >= 0) snprintf(buf, sizeof(buf), "WRONG PLUG: this is the %s sensor (belongs in %s)",
                               POSITIONS[owner], CONNECTOR[owner]);
      else            snprintf(buf, sizeof(buf), "unknown sensor 0x%08lX", (unsigned long)sn);
      line(item, false, buf);
      fails++;
    } else {
      bool sane = t > 0 && t < 50 && rh > 5 && rh < 95;
      snprintf(buf, sizeof(buf), "%.2f C  %.1f %%RH%s", t, rh, sane ? "" : "  <-- odd reading");
      line(item, sane, buf);
      if (!sane) fails++;
    }
  }

  Serial.println("\n-------------------------------------------------");
  if (fails == 0) Serial.println("  ALL PASS - ready for the soak.");
  else            Serial.printf ("  %d FAIL(S) - fix before the soak.\n", fails);
  Serial.println("-------------------------------------------------");
  Serial.println("  Live readings every 5 s below. Breathe on one sensor:");
  Serial.println("  its %RH should jump within a few seconds.\n");
}

void loop() {
  delay(5000);
  Serial.print("  ");
  for (int i = 0; i < NUM_SENSORS; i++) {
    float t, rh;
    bool ok = muxSelect(CHANNELS[i]) && readSensor(t, rh) == 0;
    muxOff();
    if (ok) Serial.printf("%s %5.1f%%  ", CONNECTOR[i], rh);
    else    Serial.printf("%s  ---   ", CONNECTOR[i]);
  }
  Serial.println();
}
