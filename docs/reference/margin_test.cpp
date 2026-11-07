#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <SD.h>
#include <esp_system.h>

// =================================================================
// BP step 15 — 3.3V margin under load, Rev P board
//
// Loads the ESP32's 3.3V regulator as hard as the real firmware will:
//   - WiFi transmitting continuously (its own access point + UDP
//     broadcasts, so no network or password is needed)
//   - SD card writing continuously
//   - plenum fan at 100%
// and reports the RESET REASON at every boot, so a brownout is caught.
//
// Run 10 minutes. Meter TP2 (3.3V) and the ESP32's 5V pin to TP7 while
// it runs.
//
// Run it on 12V ONLY, USB UNPLUGGED — otherwise USB feeds the ESP32 and
// D7 is not tested. With no serial monitor, the PLENUM FAN is the indicator:
//   - starts by itself 15 s after power-up and runs at 100% for 10 minutes
//   - a reset restarts the sketch: the fan STOPS for 15 s, then restarts
//   - after 10 minutes it stops for good
// So: fan runs the whole 10 minutes, then stops = PASS.
// If USB is plugged in, typing g starts it at once (for a quick look only).
// =================================================================

constexpr int FAN_PWM_PIN = 25;
constexpr int SD_CS = 17;
constexpr uint32_t RUN_MS = 10UL * 60UL * 1000UL;

WiFiUDP udp;
File logFile;
uint8_t packet[1400];
uint8_t block[4096];

const char* resetName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "POWER-ON (normal)";
    case ESP_RST_SW:       return "SOFTWARE reset";
    case ESP_RST_PANIC:    return "PANIC / crash";
    case ESP_RST_INT_WDT:  return "INTERRUPT WATCHDOG";
    case ESP_RST_TASK_WDT: return "TASK WATCHDOG";
    case ESP_RST_WDT:      return "WATCHDOG";
    case ESP_RST_BROWNOUT: return "BROWNOUT  <-- 3.3V DIPPED TOO LOW: FAIL";
    case ESP_RST_EXT:      return "EXTERNAL (EN pin / reset button)";
    case ESP_RST_DEEPSLEEP:return "DEEP SLEEP wake";
    default:               return "UNKNOWN";
  }
}

void setup() {
  pinMode(26, OUTPUT); digitalWrite(26, LOW);
  pinMode(27, OUTPUT); digitalWrite(27, LOW);
  pinMode(32, OUTPUT); digitalWrite(32, LOW);
  pinMode(33, OUTPUT); digitalWrite(33, LOW);
  ledcAttach(FAN_PWM_PIN, 25000, 8);
  ledcWrite(FAN_PWM_PIN, 0);

  Serial.begin(115200);
  delay(1500);
  esp_reset_reason_t rr = esp_reset_reason();
  Serial.println("\n=================================================");
  Serial.println("BP15 3.3V MARGIN TEST - Rev P board");
  Serial.printf ("  RESET REASON for this boot: %s\n", resetName(rr));
  Serial.println("=================================================");
  if (rr == ESP_RST_BROWNOUT) {
    Serial.println("  *** The last run ended in a BROWNOUT. That is a FAIL - tell Claude. ***");
  }
  Serial.println("  SD card in? 12V on? Meter on DC volts, black on TP7.");
  Serial.println("  Starting automatically in 15 s (or type g).");
  unsigned long t0 = millis();
  while (millis() - t0 < 15000) {
    if (Serial.available() && (Serial.read() | 0x20) == 'g') break;
    delay(10);
  }

  if (!SD.begin(SD_CS)) {
    Serial.println("  SD card NOT FOUND - check it's inserted. Continuing with WiFi + fan only.");
  } else {
    logFile = SD.open("/margin.bin", FILE_WRITE);
  }
  memset(block, 0xA5, sizeof(block));
  memset(packet, 0x5A, sizeof(packet));

  WiFi.mode(WIFI_AP);
  WiFi.softAP("wineador-margin-test");
  WiFi.setTxPower(WIFI_POWER_19_5dBm);   // maximum transmit power
  udp.begin(4210);
  ledcWrite(FAN_PWM_PIN, 255);

  Serial.println("\n  LOAD ON: WiFi transmitting, SD writing, fan 100%.");
  Serial.println("  Now meter TP2 and the ESP32 5V pin (left row, hole 19) to TP7.");
  Serial.println("  Watch for the LOWEST TP2 reading over the 10 minutes.\n");
}

void loop() {
  static unsigned long start = millis(), lastReport = 0;
  static uint32_t packets = 0, kb = 0, sdErrors = 0;

  // Continuous WiFi transmit
  for (int i = 0; i < 4; i++) {
    udp.beginPacket(IPAddress(192, 168, 4, 255), 4210);
    udp.write(packet, sizeof(packet));
    if (udp.endPacket()) packets++;
  }
  // Continuous SD write
  if (logFile) {
    if (logFile.write(block, sizeof(block)) == sizeof(block)) kb += 4;
    else sdErrors++;
    if (kb % 512 == 0) logFile.flush();
    if (kb >= 20UL * 1024UL) {            // keep the file to ~20MB
      logFile.close();
      logFile = SD.open("/margin.bin", FILE_WRITE);
      kb = 0;
    }
  }

  unsigned long now = millis();
  if (now - lastReport >= 10000) {
    lastReport = now;
    unsigned long sec = (now - start) / 1000;
    Serial.printf("  %3lu:%02lu  running  |  WiFi packets sent: %lu  |  SD: %s, errors %lu\n",
                  sec / 60, sec % 60, (unsigned long)packets,
                  logFile ? "writing" : "not found", (unsigned long)sdErrors);
  }
  if (now - start >= RUN_MS) {
    if (logFile) logFile.close();
    ledcWrite(FAN_PWM_PIN, 0);
    WiFi.mode(WIFI_OFF);
    Serial.println("\n=================================================");
    Serial.println("  10 MINUTES DONE - no reset happened during the run.");
    Serial.println("  Send Claude: the lowest TP2 reading, the ESP32 5V-pin reading,");
    Serial.println("  and the RESET REASON shown at the top (should be POWER-ON).");
    Serial.println("=================================================");
    while (true) delay(1000);
  }
}
