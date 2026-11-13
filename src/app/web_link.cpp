#include "web_link.h"
#if defined(WINEADOR_BOARD_REVQ)
#include <Arduino.h>
#include <new>
#include <string.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace weblink {

using namespace wineador;

static State* gStaging = nullptr;          // the loop's (PSRAM)
static State* gShared = nullptr;           // the published copy (PSRAM), under gLock
static SemaphoreHandle_t gLock = nullptr;  // the loop only ever tries it (0 ticks)
static Mailbox gBox;                       // under gBoxMux: a few instructions at a time
static portMUX_TYPE gBoxMux = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t gAnswered = nullptr;   // given by the loop when it has answered
static SemaphoreHandle_t gAskers = nullptr;     // the server's tasks take turns

static State* psramState() {
  void* p = heap_caps_malloc(sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? new (p) State() : nullptr;
}

bool begin() {
  gStaging = psramState();
  gShared = psramState();
  gLock = xSemaphoreCreateMutex();
  gAnswered = xSemaphoreCreateBinary();
  gAskers = xSemaphoreCreateMutex();
  return gStaging && gShared && gLock && gAnswered && gAskers;
}

State* staging() { return gStaging; }

bool publish() {
  if (!gStaging || xSemaphoreTake(gLock, 0) != pdTRUE) return false;   // never waits
  memcpy((void*)gShared, (const void*)gStaging, sizeof(State));
  xSemaphoreGive(gLock);
  return true;
}

void copy(State* out) {
  if (!gShared) { *out = State(); return; }
  xSemaphoreTake(gLock, portMAX_DELAY);   // held by the loop only for one memcpy
  memcpy((void*)out, (const void*)gShared, sizeof(State));
  xSemaphoreGive(gLock);
}

bool takeMail(MailRequest* out) {
  portENTER_CRITICAL(&gBoxMux);
  const bool got = gBox.take(out);
  portEXIT_CRITICAL(&gBoxMux);
  return got;
}

void answerMail(const MailAnswer& a) {
  portENTER_CRITICAL(&gBoxMux);
  gBox.answer(a);
  portEXIT_CRITICAL(&gBoxMux);
  xSemaphoreGive(gAnswered);              // never waits
}

bool ask(MailRequest* r, MailAnswer* a) {
  if (!gAskers || xSemaphoreTake(gAskers, pdMS_TO_TICKS(kMailWaitMs)) != pdTRUE) return false;
  xSemaphoreTake(gAnswered, 0);           // an old signal (an answer nobody waited for)
  portENTER_CRITICAL(&gBoxMux);
  const bool posted = gBox.post(r);
  portEXIT_CRITICAL(&gBoxMux);
  bool got = false;
  if (posted) {
    const uint32_t start = millis();
    for (;;) {
      xSemaphoreTake(gAnswered, pdMS_TO_TICKS(50));
      portENTER_CRITICAL(&gBoxMux);
      got = gBox.collect(r->id, a);
      bool gaveUp = false;
      if (!got && millis() - start >= kMailWaitMs) gaveUp = gBox.withdraw(r->id) || millis() - start >= 2 * kMailWaitMs;
      portEXIT_CRITICAL(&gBoxMux);
      // Taken but not yet answered: the loop is reading the request (the body it points to stays
      // put); it answers within its pass. Only if the loop has stopped altogether (the watchdog
      // then resets the board) does the server give up after twice the wait.
      if (got || gaveUp) break;
    }
  }
  xSemaphoreGive(gAskers);
  return got;
}

}  // namespace weblink

#endif  // WINEADOR_BOARD_REVQ
