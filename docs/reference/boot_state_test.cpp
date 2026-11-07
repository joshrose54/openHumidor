#include <Arduino.h>

// =================================================================
// BP step 11 — boot state on the Rev P board
//
// This sketch NEVER drives a load pin. P26, P27, P32 and P33 are left
// as plain inputs, so the board's pull-downs alone decide their state.
// It just reports what it reads every 2 seconds, so you can meter the
// MOSFET gates and J10 while it runs.
//
// Flash it with the ESP32 OFF the board first, then fit the ESP32.
// =================================================================

constexpr int PINS[]       = {26, 27, 32, 33, 34};
const char*  NAMES[]       = {"P26 cooling (SSR)", "P27 heater", "P32 relay K1", "P33 humidifier", "P34 fan tach"};
const char*  EXPECT[]      = {"LOW", "LOW", "LOW", "LOW", "HIGH"};
constexpr int N = sizeof(PINS) / sizeof(PINS[0]);

void setup() {
  for (int i = 0; i < N; i++) pinMode(PINS[i], INPUT);   // inputs only — nothing is driven
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=================================================");
  Serial.println("BP11 BOOT STATE — no load pin is driven by this sketch");
  Serial.println("Expect: P26, P27, P32, P33 LOW; P34 HIGH (tach pull-up)");
  Serial.println("Meter the Q2-Q5 gates and J10 pins 1 and 3 while it runs.");
  Serial.println("=================================================");
}

void loop() {
  bool allOk = true;
  for (int i = 0; i < N; i++) {
    int v = digitalRead(PINS[i]);
    const char* got = v ? "HIGH" : "LOW";
    bool ok = (strcmp(got, EXPECT[i]) == 0);
    allOk &= ok;
    Serial.printf("  %-20s %-4s  (expect %s)%s\n", NAMES[i], got, EXPECT[i], ok ? "" : "   <-- CHECK");
  }
  Serial.println(allOk ? "  ALL AS EXPECTED\n" : "  SOMETHING NOT AS EXPECTED\n");
  delay(2000);
}
