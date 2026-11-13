#pragma once
// GET /api/status (spec 5.5): everything the Overview and Sensors pages and every page's header
// show, in one reply. The control loop fills a StatusSnapshot about once a second (stage Q5c) and
// the web server writes the JSON from the copy, so the server never touches the control objects.
// The mode icons and the alerts come from the LCD's own logic (lcd_ui.h, alerts.h), so the LCD and
// the dashboard can't disagree.

#include <stdint.h>
#include <stddef.h>
#include "wineador/types.h"
#include "wineador/alerts.h"
#include "wineador/lcd_ui.h"
#include "wineador/web_api.h"
#include "wineador/json_out.h"
#include "wineador/blower_rule.h"
#include "wineador/ion_schedule.h"

namespace wineador {

// Where the control temperature sits (4.5): "cool" above setpoint + hysteresis, "heat" below
// setpoint - deadband, "dead" between.
const char* zoneName(float controlC, float targetC, float coolingHysteresisC, float heatingDeadbandC);

enum class SensorState : uint8_t { NotRead, Healthy, Excluded, Manual };
const char* sensorStateName(SensorState s);   // "not_read", "healthy", "excluded", "manual"

struct SensorStatus {
  SensorState state = SensorState::NotRead;
  char reason[24] = "";           // why excluded: "crc", "absent", "divergence" ... ("" if none)
  bool read = false;              // values from the last sweep
  float tempC = 0, rhPct = 0, rhRawPct = 0;
  uint32_t ageS = 0;              // since its last good read
  uint16_t crcHour = 0, timeoutsHour = 0;
};

struct GuardHold {
  uint8_t load = 0;               // 0 compressor, 1 heater, 2 humidifier
  const char* reason = "";        // guardBlockName(): "crossover", "min off" ...
  int32_t leftS = -1;             // -1: no countdown
};

struct AlertStatus {
  char title[44] = "";
  char when[20] = "";             // formatAlertWhen(): "14:32", "13 Nov 14:32", "for 12 min"
  uint8_t rank = 0;
};

struct StatusSnapshot {
  BuildFacts build;
  uint64_t uptimeS = 0;
  // time (5.5): UK local from the board; "--:--" while the clock isn't trusted
  bool clockOk = false;
  int64_t utcNowS = 0;
  // what the LCD shows
  uint8_t nModes = 0;
  ModeIcon modes[kMaxModes] = {};
  uint8_t nAlerts = 0;
  AlertStatus alerts[kAlertSlots];
  bool bootHold = false;
  uint32_t bootHoldLeftS = 0;
  // the cabinet (4.3): "--" when readingsOk is false (fewer than 3 healthy, or not fresh)
  bool readingsOk = false;
  int cabinetOk = 0;
  float meanC = 0, minC = 0, maxC = 0, spreadC = 0, meanRhPct = 0, controlC = 0;
  uint32_t readingsAgeS = 0;
  float targetC = 20, rhSetPct = 62;
  float coolingHysteresisC = 0.5f, heatingDeadbandC = 1.5f;   // for the zone
  // the loads, as running
  bool cool = false, stripping = false, heat = false, humid = false, relayHeating = false;
  uint8_t heaterDutyPct = 0;
  uint8_t fanPct = 0;
  uint32_t fanRpm = 0;
  // the blowers (4.6b) and the ion generator (4.6c), Rev Q (fillBlowerIon)
  bool hasBlowers = false;
  uint8_t blowerPct = 100;
  BlowerMode blowerMode = BlowerMode::Full;
  BlowerWhy blowerWhy = BlowerWhy::Boot;
  float blowerSpreadC = 0, blowerSpreadRhPct = 0;   // from the last sweep the rule judged
  int32_t blowerIdleInS = -1;                        // boost: holding, idle in this many s (-1: not holding)
  bool blowerIdleIsFull = true;                      // the idle duty is still full speed (until BP2 test 22)
  bool ionOn = false;
  IonState ionState = IonState::NotYet;
  IonWhy ionHeldWhy = IonWhy::None;
  uint32_t ionLeftS = 0;                             // running
  bool ionNextFromClock = false;
  int64_t ionNextUtcS = 0;
  uint32_t ionNextInS = 0;
  IonWhy ionLastSkip = IonWhy::None;
  int64_t ionLastSkipUtcS = 0;
  uint32_t compressorOnForS = 0;        // while running
  bool compressorStoppedSinceBoot = false;
  uint32_t compressorStoppedAgoS = 0;   // since the last stop (when stopped since boot)
  uint8_t nGuards = 0;
  GuardHold guards[3];
  // the last 7 days (counted from the event files at boot, then live)
  bool weekReady = false;
  uint32_t crossovers = 0, strips = 0, humidRuns = 0, disturbances = 0;
  bool disturbanceDetection = false;    // off until B6: disturbances shown as null
  // OTA
  bool windowOpen = false;
  uint32_t windowLeftS = 0;
  bool updating = false;
  uint8_t updatePct = 0;
  // card and WiFi
  bool sdOk = false;
  uint64_t sdFreeBytes = 0, sdTotalBytes = 0;
  int wifiRssi = 0;
  // the Board section (5.5): with the version, uptime, WiFi and card above
  const char* bootReason = "";        // the last restart's reason, as the boot event logs it
  bool hasV12 = false;                // Rev Q: the 12V sense
  float v12NowV = 0;                  // the last 1 s average
  float v12LowTodayV = 0;             // the lowest 1 s average since UK midnight (or since boot, if later)
  uint32_t heapFreeBytes = 0;         // free internal RAM now (5.2)
  // the six sensors
  SensorStatus sensors[kNumSensors];
};

const char* modeKey(ModeIcon m);   // "cooling", "heating", ... "fault"

// The blower and ion generator lines of the snapshot, from their rules (Rev Q).
void fillBlowerIon(StatusSnapshot* s, const BlowerRule& b, const IonSchedule& ion, const TuningSettings& t,
                   uint32_t nowMs);

// Copies what the LCD shows — the mode icons and every alert with when it started — into the
// snapshot, from the LCD's own logic after its update(), so the two can't disagree.
void copyFromLcd(StatusSnapshot* s, const LcdUi& ui, uint64_t uptimeMs, bool clockOk, int64_t utcNowS);

class StatusReply : public JsonPump {
 public:
  explicit StatusReply(const StatusSnapshot& s) : s_(s) {}
  size_t maxStep() const override { return 480; }
 protected:
  bool step(JsonWriter& w) override;
 private:
  const StatusSnapshot& s_;
  int phase_ = 0, i_ = 0;
};

}  // namespace wineador
