#pragma once
// The six SHT40s as built — build log section 1, "SHT40 sensors".
// Calibration belongs to the SERIAL, not the channel (spec 5.3).
// Stage 1 only reports the serials; checking and calibration arrive at stage 3.

#include "wineador/types.h"

namespace wineador {

struct SensorInfo {
  uint8_t channel;
  uint32_t serial;
  const char* label;      // last four hex digits, written on the probe
  const char* position;   // the code the logs, the API and the serial console use ("mid_rear")
  const char* name;       // what the LCD and the dashboard say ("Middle rear"; spec 5.5 word list).
                          // The one source: web/words.js is made from it (tools/make_web_words.py)
  float rhGain;           // corrected RH = gain x raw + offset
  float rhOffset;
};

extern const SensorInfo kSensors[kNumSensors];

// Index (0..5) of the sensor with this serial, or -1 if it is not one of the six.
int sensorIndexForSerial(uint32_t serial);

}  // namespace wineador
