#include "time_sync.h"
#include <Arduino.h>
#include <WiFi.h>
#include "esp_sntp.h"
#include "wineador/clock_state.h"
#include "wineador/ntp_window.h"
#include "board.h"

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

static wineador::NtpWindow gWindow;
static bool gRadio = false;
static char gNote[120] = {};
static bool gSntpStarted = false;         // in this window
static uint32_t gSntpAtMs = 0;

static void radioUp() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_TIME_SSID, WIFI_TIME_PASSWORD);      // returns at once; connects in the background
  gRadio = true;
}

static void radioDown() {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);                                  // frees the driver's buffers
  gRadio = false;
}

static void startSntp() {
  // UTC, no daylight saving (5.4). SNTP waits for the network by itself.
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  sntp_set_sync_interval(wineador::kNtpIntervalMs);   // every 24 h after the first sync
}

void begin() {
  sntp_set_time_sync_notification_cb(onSync);
  if (board::kWifiOnlyForNtp) return;                   // update() opens the first window
  radioUp();
  startSntp();
}

void update(uint32_t nowMs) {
  if (!board::kWifiOnlyForNtp) return;
  const uint32_t openMs = gWindow.openForMs(nowMs);
  switch (gWindow.update(nowMs, vSyncs)) {
    case wineador::NtpWindow::Action::Open:
      radioUp();
      gSntpStarted = false;               // SNTP starts once WiFi has connected (below)
      snprintf(gNote, sizeof(gNote), "WiFi on for the NTP sync (Rev N: WiFi only for NTP; off after the sync or 2 min)");
      break;
    case wineador::NtpWindow::Action::CloseSynced:
      esp_sntp_stop();
      radioDown();
      snprintf(gNote, sizeof(gNote), "WiFi off: NTP synced after %lu s; next sync in 24 h", (unsigned long)(openMs / 1000));
      break;
    case wineador::NtpWindow::Action::CloseTimedOut:
      esp_sntp_stop();
      radioDown();
      snprintf(gNote, sizeof(gNote), "WiFi off: no NTP sync within 2 min; next try in 1 h");
      break;
    default:
      break;
  }
  // SNTP's first requests are lost if sent before WiFi has connected, and it then
  // backs off past the window (seen at boot, 2026-11-11). So start it once WiFi is
  // up, and again every 10 s until the sync comes.
  if (gWindow.isOpen() && WiFi.status() == WL_CONNECTED && (!gSntpStarted || nowMs - gSntpAtMs >= 10000)) {
    startSntp();
    gSntpStarted = true;
    gSntpAtMs = nowMs;
  }
}

bool radioOn() { return board::kWifiOnlyForNtp ? gRadio : true; }

bool takeNote(char* out, size_t n) {
  if (!gNote[0]) return false;
  snprintf(out, n, "%s", gNote);
  gNote[0] = 0;
  return true;
}

void syncNow() { gWindow.requestNow(); }

bool wifiUp() { return WiFi.status() == WL_CONNECTED; }

bool takeSync() {
  if (!vSynced) return false;
  vSynced = false;
  return true;
}

uint32_t syncCount() { return vSyncs; }

}  // namespace timesync
