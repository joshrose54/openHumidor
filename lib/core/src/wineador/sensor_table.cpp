#include "wineador/sensor_table.h"

namespace wineador {

// From the build log, section 1. Do not edit without a new calibration.
// Channel 0-5 (spec 2.3). The build log's B1-B4 records number them by Rev N's mux, 2-7.
const SensorInfo kSensors[kNumSensors] = {
  {0, 0x0AD409ED, "09ED", "top_front",    "Top front",     1.0067f, -2.556f},
  {1, 0x128EA036, "A036", "bottom_front", "Bottom front",  0.9998f, -2.842f},
  {2, 0x0ADBCB62, "CB62", "mid_front",    "Middle front",  1.0274f, -3.878f},
  {3, 0x10EDA1C5, "A1C5", "mid_rear",     "Middle rear",   0.9826f, -2.528f},
  {4, 0x11D7FDF3, "FDF3", "chamber",      "Chamber",       1.0158f, -3.298f},
  {5, 0x0E42EFBA, "EFBA", "ambient",      "Room",          1.0151f, -3.475f},
};

int sensorIndexForSerial(uint32_t serial) {
  for (int i = 0; i < kNumSensors; i++)
    if (kSensors[i].serial == serial) return i;
  return -1;
}

}  // namespace wineador
