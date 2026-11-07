#include <Arduino.h>

// =================================================================
// BP step 12 — each output alone, Rev P board
//
// Loads connected before you start (power off while wiring):
//   J10 pins 1-2 : SSR control input, pin 1 -> SSR +A1, pin 2 -> SSR -A2
//                  (NO mains on the SSR's L13 / LOAD side)
//   J10 pins 3-4 : 12V bulb with the KSD9700 in series
//   J11          : humidifier fan (red = pin 1, black = pin 2)
//
// STEP-BY-STEP. Nothing changes until you type a command:
//   n + Enter  -> apply the next step (holds there until you type again)
//   x + Enter  -> ABORT: everything off immediately, relay released
//
// Heater bulb: switches itself off after 5 minutes on, whatever you type.
// =================================================================

constexpr int SSR_PIN    = 26;   // -> U9 -> Q2 -> SSR -A2
constexpr int HEATER_PIN = 27;   // -> U9 -> Q3 -> heater -
constexpr int RELAY_PIN  = 32;   // -> U9 -> Q5 -> K1 coil (HIGH = heating side)
constexpr int HUMID_PIN  = 33;   // -> U9 -> Q4 -> humidifier fan -

constexpr uint32_t HEATER_LIMIT_MS = 5UL * 60UL * 1000UL;

enum HeaterMode { H_OFF, H_ON, H_PWM };

struct Step {
  const char* title;
  const char* check;
  bool relay;
  HeaterMode heater;
  float hz;       // for H_PWM
  bool ssr;
  bool humid;
};

const Step STEPS[] = {
  // ---- (a) relay alone ----
  {"a1. All off, relay de-energised",
   "Meter (black TP7): J10 pin 1 ~12V, J10 pin 3 ~0V. Bulb off, fan off",            false, H_OFF, 0, false, false},
  {"a2. Relay ENERGISED (nothing else on)",
   "Relay CLICKS. J10 pin 1 ~0V, J10 pin 3 ~12V. Bulb stays off (Q3 off)",           true,  H_OFF, 0, false, false},
  {"a3. Relay de-energised again",
   "Relay clicks back. J10 pin 1 ~12V, pin 3 ~0V",                                   false, H_OFF, 0, false, false},
  // ---- (b) heater ----
  {"b1. Relay energised, heater signal OFF",
   "Bulb OFF",                                                                       true,  H_OFF, 0, false, false},
  {"b2. Heater PWM 1Hz",
   "Bulb flashes once a second",                                                     true,  H_PWM, 1, false, false},
  {"b3. Heater PWM 5Hz",
   "Bulb flickers at 5Hz",                                                           true,  H_PWM, 5, false, false},
  {"b4. Heater PWM 10Hz",
   "Bulb flickers fast / looks dimmed",                                              true,  H_PWM, 10, false, false},
  {"b5. Heater steady ON",
   "Bulb fully on",                                                                  true,  H_ON,  0, false, false},
  {"b6. Relay DE-ENERGISED with the heater signal still ON",
   "Relay clicks. Bulb goes OFF — no heater feed with the relay off",                false, H_ON,  0, false, false},
  {"b7. Heater signal off",
   "Bulb off",                                                                       false, H_OFF, 0, false, false},
  // ---- (c) SSR control ----
  {"c1. Relay de-energised, SSR signal OFF",
   "Meter red on SSR +A1, black on SSR -A2: ~0V",                                    false, H_OFF, 0, false, false},
  {"c2. SSR signal ON",
   "+A1 to -A2: ~12V (SSR's LED on, if it has one)",                                 false, H_OFF, 0, true,  false},
  {"c3. Relay ENERGISED with the SSR signal still ON",
   "Relay clicks. +A1 to -A2: ~0V — no SSR feed with the relay energised",          true,  H_OFF, 0, true,  false},
  {"c4. SSR signal off, relay de-energised",
   "+A1 to -A2: ~0V",                                                                false, H_OFF, 0, false, false},
  // ---- (d) humidifier fan ----
  {"d1. Humidifier fan ON",
   "Fan starts cleanly. Meter across the fan (J11 pin 1 to pin 2): >= 11.8V",       false, H_OFF, 0, false, true},
  {"d2. Humidifier fan OFF",
   "Fan stops. Across the fan ~0V",                                                  false, H_OFF, 0, false, false},
  {"d3. Humidifier fan ON again",
   "Starts cleanly again",                                                           false, H_OFF, 0, false, true},
  {"d4. Humidifier fan OFF",
   "Fan stops",                                                                      false, H_OFF, 0, false, false},
};
constexpr int N = sizeof(STEPS) / sizeof(STEPS[0]);

HeaterMode heaterMode = H_OFF;
float heaterHz = 0;
bool heaterLevel = false;
unsigned long heaterToggleAt = 0;
unsigned long heaterOnSince = 0;
bool heaterLimited = false;

void allOff() {
  digitalWrite(HEATER_PIN, LOW);
  digitalWrite(SSR_PIN, LOW);
  digitalWrite(HUMID_PIN, LOW);
  heaterMode = H_OFF;
  delay(20);
  digitalWrite(RELAY_PIN, LOW);
}

// Runs the heater PWM and the 5-minute limit. Called continuously while waiting.
void serviceHeater() {
  unsigned long now = millis();
  if (heaterMode != H_OFF && now - heaterOnSince >= HEATER_LIMIT_MS) {
    heaterMode = H_OFF;
    digitalWrite(HEATER_PIN, LOW);
    heaterLimited = true;
    Serial.println("\n*** HEATER TIME LIMIT (5 min) — heater signal switched OFF. ***");
  }
  if (heaterMode == H_PWM) {
    unsigned long half = (unsigned long)(500.0f / heaterHz);   // 50% duty
    if (now - heaterToggleAt >= half) {
      heaterToggleAt = now;
      heaterLevel = !heaterLevel;
      digitalWrite(HEATER_PIN, heaterLevel ? HIGH : LOW);
    }
  }
}

bool waitForNext(const char* prompt) {
  while (Serial.available()) Serial.read();
  if (prompt[0]) Serial.println(prompt);
  while (true) {
    serviceHeater();
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'n' || c == 'N') { while (Serial.available()) Serial.read(); return true; }
      if (c == 'x' || c == 'X') { while (Serial.available()) Serial.read(); return false; }
    }
    delay(2);
  }
}

void applyStep(const Step& s) {
  // Relay change: this test deliberately leaves the load signal ON across the
  // change in b6 and c3 — that is what proves K1 alone isolates the load.
  bool relayNow = digitalRead(RELAY_PIN) == HIGH;
  if (relayNow != s.relay) {
    digitalWrite(RELAY_PIN, s.relay ? HIGH : LOW);
    delay(20);   // K1 settle
  }

  if (s.heater == H_OFF) {
    heaterMode = H_OFF;
    digitalWrite(HEATER_PIN, LOW);
  } else {
    if (heaterMode == H_OFF) heaterOnSince = millis();
    heaterMode = s.heater;
    heaterHz = s.hz;
    if (s.heater == H_ON) digitalWrite(HEATER_PIN, HIGH);
    else { heaterLevel = true; heaterToggleAt = millis(); digitalWrite(HEATER_PIN, HIGH); }
  }

  digitalWrite(SSR_PIN, s.ssr ? HIGH : LOW);
  digitalWrite(HUMID_PIN, s.humid ? HIGH : LOW);
}

const char* heaterText(const Step& s) {
  static char buf[24];
  if (s.heater == H_OFF) return "OFF";
  if (s.heater == H_ON)  return "ON (steady)";
  snprintf(buf, sizeof(buf), "FLASHING %.0fHz", s.hz);
  return buf;
}

void printNow(int i) {
  const Step& s = STEPS[i];
  Serial.println();
  Serial.println("#################################################");
  Serial.printf ("  STEP %d of %d  -  NOW ACTIVE\n", i + 1, N);
  Serial.printf ("  %s\n", s.title);
  Serial.println("#################################################");
  Serial.printf ("  RELAY K1 ........ %s\n", s.relay ? "ON  (heating side)" : "OFF (cooling side)");
  Serial.printf ("  HEATER signal ... %s\n", heaterText(s));
  Serial.printf ("  SSR signal ...... %s\n", s.ssr ? "ON" : "OFF");
  Serial.printf ("  HUMID FAN ....... %s\n", s.humid ? "ON" : "OFF");
  Serial.println("  -----------------------------------------------");
  Serial.printf ("  CHECK NOW:  %s\n", s.check);
  Serial.println("  -----------------------------------------------");
  if (i + 1 < N) Serial.printf("  When done, type  n  for step %d: %s\n", i + 2, STEPS[i + 1].title);
  else           Serial.println("  When done, type  n  to switch everything off and finish.");
  Serial.println("  (x = abort, everything off)");
}

void runTest() {
  Serial.printf("\n  Type  n  to start step 1: %s\n", STEPS[0].title);
  for (int i = 0; i < N; i++) {
    if (!waitForNext("")) {
      allOff();
      Serial.println("\n*** ABORTED — everything off, relay de-energised. ***");
      return;
    }
    applyStep(STEPS[i]);
    printNow(i);
  }
  waitForNext("");
  allOff();
  Serial.println("\n=================================================");
  Serial.println("  FINISHED — everything off, relay de-energised.");
  Serial.println("  Send Claude: pass/fail for a, b, c, d; J10 pin 1 & 3 volts in a1 and a2;");
  Serial.println("  +A1 to -A2 volts in c1, c2, c3; volts across the fan in d1.");
  Serial.println("=================================================");
}

void setup() {
  pinMode(SSR_PIN, OUTPUT);    digitalWrite(SSR_PIN, LOW);
  pinMode(HEATER_PIN, OUTPUT); digitalWrite(HEATER_PIN, LOW);
  pinMode(RELAY_PIN, OUTPUT);  digitalWrite(RELAY_PIN, LOW);
  pinMode(HUMID_PIN, OUTPUT);  digitalWrite(HUMID_PIN, LOW);

  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=================================================");
  Serial.println("BP12 EACH OUTPUT ALONE — Rev P board");
  Serial.println("Loads: SSR control on J10 1-2 (no mains), 12V bulb + KSD on J10 3-4,");
  Serial.println("humidifier fan on J11. Everything OFF while waiting.");
  Serial.println("Each  n  APPLIES the next step, then shows what is ON right now");
  Serial.println("and what you should see. x = abort (all off).");
  Serial.println("Right now: EVERYTHING OFF, relay off.");
  Serial.println("=================================================");
}

void loop() {
  runTest();
  Serial.println("\n  To run it again, type  n .");
  waitForNext("");
}
