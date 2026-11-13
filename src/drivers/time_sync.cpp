#include "time_sync.h"
#include <Arduino.h>
#include <WiFi.h>
#include "esp_sntp.h"
#include "wineador/event_text.h"
#include "wineador/clock_state.h"
#include "wineador/ntp_window.h"
#include "wineador/wifi_watch.h"
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

// Rev Q (5.4): WiFi stays connected. The same NTP window as Rev N decides when SNTP runs:
// at boot, 24 h after a sync, 1 h after a window without one. The window only starts and
// stops SNTP; the radio stays on. SNTP is (re)started every 10 s while the window is open
// and WiFi is connected, until the sync comes (the Rev N lesson, 2026-11-11).
static volatile bool gSntpOn = false;
static void sntpTask(void*) {
  uint32_t sntpAtMs = 0;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));      // woken by the loop, or every 0.5 s
    if (gWant && WiFi.status() == WL_CONNECTED && (!gSntpOn || millis() - sntpAtMs >= 10000)) {
      startSntp();
      gSntpOn = true;
      sntpAtMs = millis();
    } else if (!gWant && gSntpOn) {
      esp_sntp_stop();
      gSntpOn = false;
    }
  }
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
  // Rev Q: always connected, and it reconnects by itself (setAutoReconnect).
  radioUp();
  // Core 0, priority 1, as Rev N's radio task: above the SD task and the idle task, below WiFi.
  xTaskCreatePinnedToCore(sntpTask, "sntp", 4096, nullptr, 1, &gRadioTask, 0);
}

static wineador::WifiWatch gWatch;
static wineador::WifiWatch::Event gWifiEvent = wineador::WifiWatch::Event::None;

static void want(bool on) {
  gWant = on;
  if (gRadioTask) xTaskNotifyGive(gRadioTask);
}

void update(uint32_t nowMs) {
  if (!board::kWifiOnlyForNtp) {
    // Rev Q: watch the connection for the "WiFi lost" / "back after N s" events (5.4).
    const wineador::WifiWatch::Event e = gWatch.update(WiFi.status() == WL_CONNECTED, nowMs);
    if (e == wineador::WifiWatch::Event::Lost || e == wineador::WifiWatch::Event::Back) gWifiEvent = e;
  }
  const uint32_t openMs = gWindow.openForMs(nowMs);
  const bool revN = board::kWifiOnlyForNtp;
  switch (gWindow.update(nowMs, vSyncs)) {
    case wineador::NtpWindow::Action::Open:
      want(true);
      if (revN) snprintf(gNote, sizeof(gNote), "%s (Rev N: WiFi only for NTP; off after the sync or 2 min)",
                         wineador::kEvNtpWifiOn);
      else snprintf(gNote, sizeof(gNote), "NTP sync started (WiFi stays on; SNTP stops after the sync or 2 min)");
      break;
    case wineador::NtpWindow::Action::CloseSynced:
      want(false);
      if (revN) snprintf(gNote, sizeof(gNote), "%s NTP synced after %lu s; next sync in 24 h", wineador::kEvNtpWifiOff,
                         (unsigned long)(openMs / 1000));
      else snprintf(gNote, sizeof(gNote), "NTP synced after %lu s; next sync in 24 h", (unsigned long)(openMs / 1000));
      break;
    case wineador::NtpWindow::Action::CloseTimedOut:
      want(false);
      if (revN) snprintf(gNote, sizeof(gNote), "%s no NTP sync within 2 min; next try in 1 h", wineador::kEvNtpWifiOff);
      else snprintf(gNote, sizeof(gNote), "no NTP sync within 2 min (WiFi %s); next try in 1 h",
                    WiFi.status() == WL_CONNECTED ? "connected" : "not connected");
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

int takeWifiEvent(uint32_t* outageMs) {
  const wineador::WifiWatch::Event e = gWifiEvent;
  gWifiEvent = wineador::WifiWatch::Event::None;
  if (e == wineador::WifiWatch::Event::Lost) return -1;
  if (e == wineador::WifiWatch::Event::Back) { if (outageMs) *outageMs = gWatch.lastOutageMs(); return 1; }
  return 0;
}

bool takeSync() {
  if (!vSynced) return false;
  vSynced = false;
  return true;
}

uint32_t syncCount() { return vSyncs; }

}  // namespace timesync
