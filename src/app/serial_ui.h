#pragma once
// The serial interface's longer printouts (spec 5.5 — the dashboard's job until
// B6): status, sensors, events, log, settings. Each prints a block and returns;
// called only when typed, so a few ms of printing is fine (the UART sends it in
// the background from an 8 KB buffer).

#include <stdint.h>
#include "wineador/settings.h"
#include "wineador/controller.h"
#include "wineador/temp_control.h"
#include "wineador/humid_control.h"
#include "wineador/sensor_health.h"
#include "wineador/clock_state.h"

namespace serialui {

struct View {
  const wineador::Settings* s = nullptr;
  const wineador::Controller* ctl = nullptr;
  const wineador::TemperatureControl* tc = nullptr;
  const wineador::HumidControl* hc = nullptr;
  const wineador::SensorHealth* health = nullptr;
  const wineador::ClockState* clock = nullptr;
  const wineador::Readings* bus = nullptr;      // the real readings (last sweep)
  const wineador::Readings* logic = nullptr;    // what the logic used (scripted in a test build)
  const uint32_t* crc = nullptr;                // per channel since boot
  const uint32_t* timeouts = nullptr;
  wineador::LoadOutputs loads;
  uint8_t fanDuty = 0;
  uint32_t fanRpm = 0;
  bool fanStalled = false;
  uint64_t uptimeMs = 0;
  uint64_t lastCompressorStopMs = 0;            // uptime; 0 = not since boot
  bool rtcOk = true;
  bool testBuild = false;
  const char* version = "";
  const char* timestamp = "";
};

void printStatus(const View& v);
void printSensors(const View& v);
void printEvents(int n);
void printLog();
void printSettings(const View& v);

}  // namespace serialui
