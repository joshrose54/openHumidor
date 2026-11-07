#include <Arduino.h>

// =================================================================
// BP step 13 — interlock with real loads, Rev P board
//
// BOTH load signals are switched ON together (P26 cooling, P27 heater),
// then the relay is flipped both ways twice. Only ONE load must ever be
// live: K1 alone decides which, because it feeds only one of them.
//
// Loads (power off and the extension UNPLUGGED while wiring):
//   J10 pins 1-2 : SSR control input (+A1 pin 1, -A2 pin 2)
//   SSR mains side: the bench extension lead (B2.4), mains lamp in its socket
//   J10 pins 3-4 : 12V bulb with the KSD9700 in series
//
// STEP-BY-STEP. Nothing changes until you type a command:
//   n + Enter  -> apply the next step, then shows what is live NOW
//   x + Enter  -> ABORT: everything off immediately, relay released
//
// Heater bulb: switched off automatically after 5 minutes on.
// =================================================================

constexpr int SSR_PIN    = 26;
constexpr int HEATER_PIN = 27;
constexpr int RELAY_PIN  = 32;
constexpr int HUMID_PIN  = 33;
constexpr uint32_t HEATER_LIMIT_MS = 5UL * 60UL * 1000UL;

struct Step {
  const char* title;
  bool relay;
  bool signals;   // P26 and P27 both on
  const char* check;
};

const Step STEPS[] = {
  {"1. All off, relay off",                           false, false,
   "Mains lamp OFF, bulb OFF. Nothing should glow"},
  {"2. BOTH signals ON, relay OFF (cooling side)",    false, true,
   "Mains lamp ON, 12V bulb OFF. Meter Q2 and Q3 gates to TP7: both ~4.4V"},
  {"3. BOTH signals ON, relay ON (heating side)",     true,  true,
   "Relay clicks. Mains lamp OFF, 12V bulb ON. Both gates still ~4.4V"},
  {"4. BOTH signals ON, relay OFF again",             false, true,
   "Relay clicks. Mains lamp ON, bulb OFF"},
  {"5. BOTH signals ON, relay ON again",              true,  true,
   "Relay clicks. Mains lamp OFF, bulb ON"},
  {"6. All off, relay off",                           false, false,
   "Both loads OFF"},
};
constexpr int N = sizeof(STEPS) / sizeof(STEPS[0]);

bool heaterOn = false;
unsigned long heaterOnSince = 0;

void allOff() {
  digitalWrite(HEATER_PIN, LOW);
  digitalWrite(SSR_PIN, LOW);
  digitalWrite(HUMID_PIN, LOW);
  heaterOn = false;
  delay(20);
  digitalWrite(RELAY_PIN, LOW);
}

bool waitForNext() {
  while (Serial.available()) Serial.read();
  while (true) {
    if (heaterOn && millis() - heaterOnSince >= HEATER_LIMIT_MS) {
      digitalWrite(HEATER_PIN, LOW);
      heaterOn = false;
      Serial.println("\n*** HEATER TIME LIMIT (5 min) — heater signal switched OFF. ***");
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
  // Deliberately leaves both signals ON across the relay change: this proves
  // K1 alone keeps the unselected load off.
  bool relayNow = digitalRead(RELAY_PIN) == HIGH;
  if (relayNow != s.relay) {
    digitalWrite(RELAY_PIN, s.relay ? HIGH : LOW);
    delay(20);
  }
  if (s.signals && !heaterOn) { heaterOn = true; heaterOnSince = millis(); }
  if (!s.signals) heaterOn = false;
  digitalWrite(SSR_PIN,    s.signals ? HIGH : LOW);
  digitalWrite(HEATER_PIN, s.signals ? HIGH : LOW);
}

void printNow(int i) {
  const Step& s = STEPS[i];
  Serial.println();
  Serial.println("#################################################");
  Serial.printf("  STEP %d of %d  -  NOW ACTIVE\n", i + 1, N);
  Serial.printf("  %s\n", s.title);
  Serial.println("#################################################");
  Serial.printf("  RELAY K1 .......... %s\n", s.relay ? "ON  (heating side fed)" : "OFF (cooling side fed)");
  Serial.printf("  SSR signal P26 .... %s\n", s.signals ? "ON" : "OFF");
  Serial.printf("  HEATER signal P27 . %s\n", s.signals ? "ON" : "OFF");
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
      Serial.println("\n*** ABORTED — everything off, relay de-energised. ***");
      return;
    }
    applyStep(STEPS[i]);
    printNow(i);
  }
  waitForNext();
  allOff();
  Serial.println("\n=================================================");
  Serial.println("  FINISHED — everything off, relay de-energised.");
  Serial.println("  UNPLUG THE EXTENSION AT THE WALL before touching any mains wiring.");
  Serial.println("  Send Claude: pass/fail for each step, and the two gate voltages.");
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
  Serial.println("BP13 INTERLOCK WITH REAL LOADS — Rev P board");
  Serial.println("Mains lamp on the SSR (extension lead), 12V bulb + KSD on J10 3-4.");
  Serial.println("Each  n  APPLIES the next step, then shows what is live right now.");
  Serial.println("Right now: EVERYTHING OFF, relay off.");
  Serial.println("=================================================");
}

void loop() {
  runTest();
  Serial.println("\n  To run it again, type  n .");
  waitForNext();
}
