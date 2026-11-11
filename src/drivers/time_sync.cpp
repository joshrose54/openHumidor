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
static char gNote[120] = {};

// The radio is switched by a small task of its own (core 0), never by the loop:
// turning WiFi on or off takes 30-110 ms (B4 SD proof run), and nothing in the
// loop may wait (5.2). The loop only says what it wants.
static TaskHandle_t gRadioTask = nullptr;
static volatile bool gWant = false;       // the loop wants the radio on
static volatile bool gUp = false;         // the task has it on, or is still switching it

static void radioUp() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_TIME_SSID, WIFI_TIME_PASSWORD);      // returns once started; connects in the background
}

static void radioDown() {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);                                  // frees the driver's buffers
}

static void startSntp() {
  // UTC, no daylight saving (5.4). SNTP waits for the network by itself.
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  sntp_set_sync_interval(wineador::kNtpIntervalMs);   // every 24 h after the first sync
}

static void radioTask(void*) {
  bool sntpStarted = false;
  uint32_t sntpAtMs = 0;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));      // woken by the loop, or every 0.5 s
    if (gWant && !gUp) {
      gUp = true;                                       // set first: card writes stay paused from here
      radioUp();
      sntpStarted = false;
    } else if (!gWant && gUp) {
      esp_sntp_stop();
      radioDown();
      gUp = false;                                      // cleared last: card writes resume only now
    }
    // SNTP's first requests are lost if sent before WiFi has connected, and it then
    // backs off past the window (seen at boot, 2026-11-11). So start it once WiFi is
    // up, and again every 10 s until the sync comes.
    if (gWant && gUp && WiFi.status() == WL_CONNECTED && (!sntpStarted || millis() - sntpAtMs >= 10000)) {
      startSntp();
      sntpStarted = true;
      sntpAtMs = millis();
    }
  }
}

void begin() {
  sntp_set_time_sync_notification_cb(onSync);
  if (board::kWifiOnlyForNtp) {
    // Core 0, priority 1: above the SD task and the idle task, below WiFi itself.
    xTaskCreatePinnedToCore(radioTask, "radio", 4096, nullptr, 1, &gRadioTask, 0);
    return;                                             // update() opens the first window
  }
  radioUp();                                            // the next board: always connected
  startSntp();
}

static void want(bool on) {
  gWant = on;
  if (gRadioTask) xTaskNotifyGive(gRadioTask);
}

void update(uint32_t nowMs) {
  if (!board::kWifiOnlyForNtp) return;
  const uint32_t openMs = gWindow.openForMs(nowMs);
  switch (gWindow.update(nowMs, vSyncs)) {
    case wineador::NtpWindow::Action::Open:
      want(true);
      snprintf(gNote, sizeof(gNote), "WiFi on for the NTP sync (Rev N: WiFi only for NTP; off after the sync or 2 min)");
      break;
    case wineador::NtpWindow::Action::CloseSynced:
      want(false);
      snprintf(gNote, sizeof(gNote), "WiFi off: NTP synced after %lu s; next sync in 24 h", (unsigned long)(openMs / 1000));
      break;
    case wineador::NtpWindow::Action::CloseTimedOut:
      want(false);
      snprintf(gNote, sizeof(gNote), "WiFi off: no NTP sync within 2 min; next try in 1 h");
      break;
    default:
      break;
  }
}

// On from the moment the loop asks for it until the task has fully switched it off.
bool radioOn() { return board::kWifiOnlyForNtp ? (gWant || gUp) : true; }

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
