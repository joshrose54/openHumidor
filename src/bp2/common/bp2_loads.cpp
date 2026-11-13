#include "bp2_loads.h"
#include <Arduino.h>
#include "wineador/types.h"
#include "drivers/outputs.h"
#include "drivers/blowers.h"
#include "drivers/fan.h"
#include "wineador/bp2_steps.h"

namespace bp2 {

static float gHz = 0;
static bool gPwmHigh = true;
static uint32_t gEdgeMs = 0, gHeatOnMs = 0;
static bool gHeatAsked = false, gLimitHit = false;
static bool gFanStarted = false;

void loadsBegin() {
  outputs::begin();         // all four load signals LOW from the first instruction; ion LOW
  blowers::begin();         // full speed, as undriven
}

void loadsApply(const LoadStep& s, uint32_t nowMs) {
  wineador::LoadOutputs w;
  w.relayHeating = s.k1;
  w.heat = s.heat && !gLimitHit;
  w.cool = s.ssr;
  w.humid = s.humid;
  if (s.heat && !gHeatAsked) gHeatOnMs = nowMs;     // the 5 min limit counts from the first heater step
  gHeatAsked = s.heat;
  if (!s.heat) gLimitHit = false;
  gHz = s.heat ? s.heaterHz : 0;
  gPwmHigh = true;
  gEdgeMs = nowMs;
  outputs::heaterGate(true);
  outputs::apply(w);                                // the firmware's interlock order
  outputs::setIon(s.ion);
  if (s.blowerPct >= 0) blowers::setPct((uint8_t)s.blowerPct);
  if (s.fanPct >= 0) {
    if (!gFanStarted) { fan::begin(); gFanStarted = true; }   // undriven until now = full speed
    fan::setDuty((uint8_t)s.fanPct);
  }
}

void loadsAllOff() {
  outputs::allOff();                                // load signals low first, then the relay; ion off
  outputs::heaterGate(true);
  blowers::setPct(100);
  gHz = 0;
  gHeatAsked = false;
  gLimitHit = false;
}

void loadsUpdate(uint32_t nowMs) {
  const wineador::LoadOutputs now = outputs::current();
  if (now.heat && !gLimitHit && nowMs - gHeatOnMs >= kHeaterLimitMs) {
    gLimitHit = true;
    wineador::LoadOutputs w = now;
    w.heat = false;
    outputs::apply(w);
    Serial.println("\n*** HEATER TIME LIMIT (5 min) - heater signal switched OFF. Type n or x. ***");
  }
  if (now.heat && gHz > 0) {                        // 50 % PWM at gHz, timed from millis()
    const uint32_t half = (uint32_t)(500.0f / gHz);
    if (nowMs - gEdgeMs >= half) {
      gEdgeMs = nowMs;
      gPwmHigh = !gPwmHigh;
      outputs::heaterGate(gPwmHigh);
    }
  }
}

void loadsRestartHeaterLimit(uint32_t nowMs) { gHeatOnMs = nowMs; gLimitHit = false; }

Live loadsLive() {
  const wineador::LoadOutputs o = outputs::current();
  Live l;
  l.k1 = o.relayHeating;
  l.heat = o.heat;
  l.heaterHz = gHz;
  l.ssr = o.cool;
  l.humid = o.humid;
  l.ion = outputs::ionOn();
  l.blowerPct = blowers::pct();
  l.fanPct = gFanStarted ? (int)fan::duty() : 100;  // undriven = full speed
  return l;
}

// ---------------------------------------------------------------- the step-list sketch
static const StepSketch* gS = nullptr;
static wineador::StepSequencer gSeq;

static void show(int i, uint32_t) {
  Live l = loadsLive();
  if (gS->extraLive) l.extra = gS->extraLive();
  printStep(i, gS->count, gS->steps[i].title, l, gS->steps[i].check,
            i + 1 < gS->count ? gS->steps[i + 1].title : nullptr);
}

void stepSketchSetup(const StepSketch& s) {
  loadsBegin();
  gS = &s;
  gSeq.begin(s.count);
  begin(s.title, s.banner);
  Serial.println("  Everything is OFF. Nothing switches until you type  g  (meter first if you need to).");
  Serial.printf ("  Type  g  to start step 1: %s\n", s.steps[0].title);
}

void stepSketchLoop() {
  const uint32_t now = millis();
  loadsUpdate(now);
  if (gS->everyPass) gS->everyPass(now);
  char w[32];
  if (!readLine(w, sizeof(w))) return;
  if (gS->onLine && gS->onLine(w)) return;
  using E = wineador::StepSequencer::Event;
  switch (gSeq.handle(w)) {
    case E::Start:
    case E::Step:
      loadsApply(gS->steps[gSeq.step()], now);
      if (gS->onStep) gS->onStep(gSeq.step(), now);
      show(gSeq.step(), now);
      break;
    case E::Finish:
      loadsAllOff();
      if (gS->onStep) gS->onStep(-1, now);
      printFinished(gS->sendClaude);
      break;
    case E::Abort:
      loadsAllOff();
      if (gS->onStep) gS->onStep(-1, now);
      printAborted();
      break;
    case E::Unknown:
      Serial.printf("  '%s' - type %s (x = abort).\n", w, gSeq.waiting() ? "g to start" : "n for the next step");
      break;
    case E::None:
      break;
  }
}

}  // namespace bp2
