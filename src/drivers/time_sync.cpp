#include "time_sync.h"
#include <Arduino.h>
#include <WiFi.h>
#include "esp_sntp.h"
#include "wineador/clock_state.h"

// The credentials file also holds a blocking syncTime() for test sketches; the
// production firmware doesn't use it (5.2), only the two credential strings.
// Its unused functions would otherwise warn.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "wifi_time.h"
#pragma GCC diagnostic pop

namespace timesync {

static volatile bool vSynced = false;     // set by the SNTP task, taken by the loop
static volatile uint32_t vSyncs = 0;

// Runs in the SNTP (network) task, not the loop: only sets flags.
static void onSync(struct timeval*) {
  vSyncs = vSyncs + 1;
  vSynced = true;
}

void begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_TIME_SSID, WIFI_TIME_PASSWORD);      // returns at once; connects in the background

  // UTC, no daylight saving (5.4). SNTP waits for the network by itself.
  sntp_set_time_sync_notification_cb(onSync);
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  sntp_set_sync_interval(wineador::kNtpIntervalMs);   // every 24 h after the first sync
}

bool wifiUp() { return WiFi.status() == WL_CONNECTED; }

bool takeSync() {
  if (!vSynced) return false;
  vSynced = false;
  return true;
}

uint32_t syncCount() { return vSyncs; }

}  // namespace timesync
