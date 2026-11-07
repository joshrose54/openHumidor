#include <Arduino.h>

// =================================================================
// BP step 14 — full load and the 12V rail, Rev P board
//
// Brings the 12V loads on ONE AT A TIME so you can note the PSU current
// at each step, ending with everything on at once.
//
// Loads (PSU off while wiring, extension lead UNPLUGGED — no mains):
//   J12-J15 : four blowers (always on whenever 12V is on)
//   J2      : Noctua plenum fan
//   J10 3-4 : 12V bulb with the KSD9700 in series
//   J11     : humidifier fan
//   J10 1-2 : SSR control input, mains side disconnected
// PSU current limit: 3A.
//
// STEP-BY-STEP. Nothing changes until you type a command:
//   n + Enter  -> apply the next step, then shows what is live NOW
//   x + Enter  -> ABORT: everything off immediately, relay released
//
// Heater bulb: switched off automatically after 5 minutes on.
// =================================================================

constexpr int FAN_PWM_PIN = 25;
constexpr int SSR_PIN     = 26;   // held LOW throughout
constexpr int HEATER_PIN  = 27;
constexpr int RELAY_PIN   = 32;
constexpr int HUMID_PIN   = 33;

constexpr uint32_t FAN_PWM_FREQ = 25000;
constexpr uint8_t  FAN_PWM_BITS = 8;
constexpr uint32_t HEATER_LIMIT_MS = 5UL * 60UL * 1000UL;

struct Step {
  const char* title;
  bool fan, relay, heater, humid;
  const char* check;
};

const Step STEPS[] = {
  {"1. Baseline: board on, outputs off",      false, false, false, false,
   "Only the four blowers run. Note the PSU current"},
  {"2. + Plenum fan 100%",                     true,  false, false, false,
   "Noctua runs at full speed. Note the PSU current"},
  {"3. + Relay energised (heater still off)",  true,  true,  false, false,
   "Relay clicks, bulb stays OFF. Note the PSU current"},
  {"4. + Heater bulb ON",                      true,  true,  true,  false,
   "Bulb fully on. Note the PSU current"},
  {"5. + Humidifier fan ON - EVERYTHING ON",   true,  true,  true,  true,
   "Note the PSU current. Meter (black TP7): TP6 within 0.3V of the PSU, TP1 4.9-5.1V. "
   "Wait 2 minutes, then touch Q1, Q3, U8, D1, D7: warm at most"},
  {"6. Everything off",                        false, false, false, false,
   "Only the blowers run again. Note the PSU current"},
};
constexpr int N = sizeof(STEPS) / sizeof(STEPS[0]);

bool heaterOn = false;
unsigned long heaterOnSince = 0;

void setFan(bool full) { ledcWrite(FAN_PWM_PIN, full ? 255 : 0); }

void allOff() {
  digitalWrite(HEATER_PIN, LOW);
  digitalWrite(HUMID_PIN, LOW);
  digitalWrite(SSR_PIN, LOW);
  heaterOn = false;
  delay(20);
  digitalWrite(RELAY_PIN, LOW);
  setFan(false);
}

bool waitForNext() {
  while (Serial.available()) Serial.read();
  while (true) {
    if (heaterOn && millis() - heaterOnSince >= HEATER_LIMIT_MS) {
      digitalWrite(HEATER_PIN, LOW);
      heaterOn = false;
      Serial.println("\n*** HEATER TIME LIMIT (5 min) - heater switched OFF. ***");
    }
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'n' || c == 'N') { while (Serial.available()) Serial.read(); return true; }
      if (c == 'x' || c == 'X') { while (Serial.available()) Serial.read(); return false; }
    }
    delay(5);
  }
}

void applyStep(const Step& s) {
  setFan(s.fan);
  // Firmware rule: drop the heater signal before changing the relay, then settle.
  if ((digitalRead(RELAY_PIN) == HIGH) != s.relay) {
    digitalWrite(HEATER_PIN, LOW);
    heaterOn = false;
    delay(20);
    digitalWrite(RELAY_PIN, s.relay ? HIGH : LOW);
    delay(20);
  }
  if (s.heater && !heaterOn) { heaterOn = true; heaterOnSince = millis(); }
  if (!s.heater) heaterOn = false;
  digitalWrite(HEATER_PIN, s.heater ? HIGH : LOW);
  digitalWrite(HUMID_PIN,  s.humid  ? HIGH : LOW);
}

void printNow(int i) {
  const Step& s = STEPS[i];
  Serial.println();
  Serial.println("#################################################");
  Serial.printf("  STEP %d of %d  -  NOW ACTIVE\n", i + 1, N);
  Serial.printf("  %s\n", s.title);
  Serial.println("#################################################");
  Serial.println("  BLOWERS 1-4 ....... ON  (always, from 12V)");
  Serial.printf("  PLENUM FAN ........ %s\n", s.fan    ? "ON 100%" : "OFF");
  Serial.printf("  RELAY K1 .......... %s\n", s.relay  ? "ON  (heating side)" : "OFF (cooling side)");
  Serial.printf("  HEATER BULB ....... %s\n", s.heater ? "ON" : "OFF");
  Serial.printf("  HUMID FAN ......... %s\n", s.humid  ? "ON" : "OFF");
  Serial.println("  -----------------------------------------------");
  Serial.printf("  CHECK NOW:  %s\n", s.check);
  Serial.println("  -----------------------------------------------");
  if (i + 1 < N) Serial.printf("  When done, type  n  for step %d: %s\n", i + 2, STEPS[i + 1].title);
  else           Serial.println("  When done, type  n  to finish.");
  Serial.println("  (x = abort, everything off)");
}

void runTest() {
  Serial.printf("\n  Type  n  to start step 1: %s\n", STEPS[0].title);
  for (int i = 0; i < N; i++) {
    if (!waitForNext()) {
      allOff();
      Serial.println("\n*** ABORTED - everything off, relay de-energised. ***");
      return;
    }
    applyStep(STEPS[i]);
    printNow(i);
  }
  waitForNext();
  allOff();
  Serial.println("\n=================================================");
  Serial.println("  FINISHED - outputs off, relay de-energised (blowers still run).");
  Serial.println("  Send Claude: the PSU current at steps 1-6, the PSU setting,");
  Serial.println("  TP6 and TP1 at step 5, and how warm Q1, Q3, U8, D1, D7 were.");
  Serial.println("=================================================");
}

void setup() {
  pinMode(SSR_PIN, OUTPUT);    digitalWrite(SSR_PIN, LOW);
  pinMode(HEATER_PIN, OUTPUT); digitalWrite(HEATER_PIN, LOW);
  pinMode(RELAY_PIN, OUTPUT);  digitalWrite(RELAY_PIN, LOW);
  pinMode(HUMID_PIN, OUTPUT);  digitalWrite(HUMID_PIN, LOW);
  ledcAttach(FAN_PWM_PIN, FAN_PWM_FREQ, FAN_PWM_BITS);
  setFan(false);

  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=================================================");
  Serial.println("BP14 FULL LOAD AND THE 12V RAIL - Rev P board");
  Serial.println("No mains. PSU current limit 3A.");
  Serial.println("Each  n  APPLIES the next step, then shows what is live right now.");
  Serial.println("Right now: outputs OFF, relay off. Blowers run whenever 12V is on.");
  Serial.println("=================================================");
}

void loop() {
  runTest();
  Serial.println("\n  To run it again, type  n .");
  waitForNext();
}
