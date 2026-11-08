// The test-build cabinet model (spec B3) and the scripted-readings swap.
#include <unity.h>
#include "wineador/cabinet_model.h"
#include "wineador/sensor_health.h"
#include "wineador/settings.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

namespace {
LoadOutputs cooling()  { LoadOutputs l; l.cool = true; return l; }
LoadOutputs heating()  { LoadOutputs l; l.heat = true; l.relayHeating = true; return l; }
LoadOutputs humid()    { LoadOutputs l; l.humid = true; return l; }
void run(CabinetModel& m, uint32_t ms, const LoadOutputs& l) {
  for (uint32_t t = 0; t < ms; t += 1000) m.step(1000, l);   // one-second steps, like the board
}
}

void test_cooling_rate() {
  CabinetModel m; m.begin(ModelRates{});
  run(m, minutes(15), cooling());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.5f, m.tempC(0));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.5f, m.tempC(kChamberIndex));
  // Chamber falls >= 1.0 C in 15 min: the cooling-ineffective check passes
  TEST_ASSERT_TRUE(20.0f - m.tempC(kChamberIndex) >= 1.0f);
}

void test_heating_clearly_passes_ineffective_check() {
  Settings s = settingsFor(Profile::Real);
  CabinetModel m; m.begin(ModelRates{});
  run(m, s.tuning.heaterIneffectiveWindowMs, heating());
  float rise = m.tempC(kChamberIndex) - 20.0f;
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.5f, rise);
  TEST_ASSERT_TRUE(rise >= 2.0f * s.tuning.heaterIneffectiveRiseC);   // clearly, not just
}

void test_humidifying_clearly_passes_ineffective_check() {
  Settings s = settingsFor(Profile::Real);
  CabinetModel m; m.begin(ModelRates{});
  run(m, s.tuning.humidIneffectiveWindowMs, humid());
  float rise = m.rhPct(0) - 62.0f;
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, rise);
  TEST_ASSERT_TRUE(rise >= 2.0f * s.tuning.humidIneffectiveRisePct);
}

void test_drift_back_to_room_and_stops_there() {
  CabinetModel m; m.begin(ModelRates{}, 19.0f);
  run(m, minutes(100), LoadOutputs{});
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 19.6f, m.tempC(0));    // +0.006 C/min x 100 min
  run(m, minutes(200), LoadOutputs{});
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, m.tempC(0));   // reaches 20 C and stays
  CabinetModel w; w.begin(ModelRates{}, 21.0f);
  run(w, minutes(100), LoadOutputs{});
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.4f, w.tempC(0));    // drifts down from above too
}

void test_interlock_respected() {
  CabinetModel m; m.begin(ModelRates{});
  LoadOutputs coolWrongSide; coolWrongSide.cool = true; coolWrongSide.relayHeating = true;
  run(m, minutes(10), coolWrongSide);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, m.tempC(0));   // SSR has no feed: no cooling
  LoadOutputs heatWrongSide; heatWrongSide.heat = true;
  run(m, minutes(10), heatWrongSide);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, m.tempC(0));   // heater has no feed
}

void test_response_switched_off() {
  ModelRates r; r.heatResponds = false; r.humidResponds = false; r.coolResponds = false;
  CabinetModel m; m.begin(r);
  run(m, minutes(15), heating());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, m.tempC(kChamberIndex));
  run(m, minutes(15), humid());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 62.0f, m.rhPct(0));
  run(m, minutes(15), cooling());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, m.tempC(kChamberIndex));
}

void test_offsets_and_ambient() {
  CabinetModel m; m.begin(ModelRates{});
  m.setOffset(2, 6.0f, 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 26.0f, m.tempC(2));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 63.0f, m.rhPct(2));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, m.tempC(kAmbientIndex));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f, m.rhPct(kAmbientIndex));
}

void test_scripted_swap_keeps_bus_failures() {
  CabinetModel m; m.begin(ModelRates{}, 20.0f, 62.0f);
  Readings r;
  for (int i = 0; i < kNumSensors; i++) { r.s[i].bus = BusResult::Ok; r.s[i].tempC = 11.0f; r.s[i].rhRawPct = 11.0f; }
  r.s[1].bus = BusResult::NoAck;     // unplugged
  r.s[3].bus = BusResult::CrcFail;
  applyScripted(r, m);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, r.s[0].tempC);
  // RH carries the raw value that this sensor's calibration turns back into 62 %
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 62.0f, correctedRh(0, r.s[0].rhRawPct));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, rawForCorrectedRh(0, 62.0f), r.s[0].rhRawPct);
  TEST_ASSERT_TRUE(r.s[1].bus == BusResult::NoAck);     // still failed
  TEST_ASSERT_EQUAL_FLOAT(11.0f, r.s[1].tempC);         // untouched
  TEST_ASSERT_TRUE(r.s[3].bus == BusResult::CrcFail);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_cooling_rate);
  RUN_TEST(test_heating_clearly_passes_ineffective_check);
  RUN_TEST(test_humidifying_clearly_passes_ineffective_check);
  RUN_TEST(test_drift_back_to_room_and_stops_there);
  RUN_TEST(test_interlock_respected);
  RUN_TEST(test_response_switched_off);
  RUN_TEST(test_offsets_and_ambient);
  RUN_TEST(test_scripted_swap_keeps_bus_failures);
  return UNITY_END();
}
