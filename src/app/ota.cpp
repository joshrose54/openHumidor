#include "ota.h"

#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include <string.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Arduino's own start-up would mark a new image good straight away (esp32-hal-misc.c, initArduino:
// "if (!verifyRollbackLater()) ... esp_ota_mark_app_valid_cancel_rollback()"). This firmware marks it
// good only once it has started properly (5.2): main.cpp, ota::markGood(). C linkage: the weak
// default is in a C file, so a C++ name would never replace it.
extern "C" bool verifyRollbackLater() { return true; }

namespace ota {

using namespace wineador;

namespace {

std::atomic<uint32_t> gPasses{0};
std::atomic<uint32_t> gBytes{0};
std::atomic<bool> gCancel{false};

constexpr uint32_t kPassWaitMs = 2000;     // the loop runs every few ms; 2 s without a pass: give up
constexpr size_t kChunk = 256;             // internal-RAM copy size for each flash write call
uint8_t gChunk[kChunk];                    // internal RAM (.bss): the piece is in PSRAM

class EspFlash : public OtaFlash {
 public:
  bool begin(uint32_t) override {
    part_ = esp_ota_get_next_update_partition(nullptr);
    if (!part_) return false;
    open_ = esp_ota_begin(part_, OTA_WITH_SEQUENTIAL_WRITES, &h_) == ESP_OK;
    return open_;
  }
  bool write(const uint8_t* p, size_t n) override {
    // The first call of a piece (at a 4 KB boundary) erases that sector; the rest only program.
    for (size_t at = 0; at < n; at += kChunk) {
      const size_t k = n - at < kChunk ? n - at : kChunk;
      memcpy(gChunk, p + at, k);
      if (esp_ota_write(h_, gChunk, k) != ESP_OK) return false;
    }
    return true;
  }
  End end() override {
    open_ = false;
    const esp_err_t e = esp_ota_end(h_);   // esp_image_verify: checksum, and the appended SHA-256
    if (e == ESP_OK) return End::Ok;
    return e == ESP_ERR_OTA_VALIDATE_FAILED ? End::Corrupt : End::Failed;
  }
  void abort() override {
    if (open_) esp_ota_abort(h_);
    open_ = false;
  }
  bool setBoot() override { return part_ && esp_ota_set_boot_partition(part_) == ESP_OK; }

 private:
  const esp_partition_t* part_ = nullptr;
  esp_ota_handle_t h_ = 0;
  bool open_ = false;
};

class LoopPacer : public OtaPacer {
 public:
  bool nextPass() override {
    const uint32_t seen = gPasses.load();
    const uint32_t start = millis();
    while (gPasses.load() == seen) {
      if (gCancel.load() || millis() - start >= kPassWaitMs) return false;
      vTaskDelay(1);
    }
    return !gCancel.load();
  }
  void progress(uint32_t bytes) override { gBytes.store(bytes); }
};

EspFlash gFlash;
LoopPacer gPacer;

}  // namespace

OtaFlash& flash() { return gFlash; }
OtaPacer& pacer() { return gPacer; }

void loopPass() { gPasses.fetch_add(1); }
uint32_t bytesWritten() { return gBytes.load(); }
void cancel() { gCancel.store(true); }
void clearCancel() { gCancel.store(false); gBytes.store(0); }

OtaBootFacts bootFacts(const char* runningVersion) {
  OtaBootFacts f;
  f.runningVersion = runningVersion;
  esp_ota_img_states_t st;
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running && esp_ota_get_state_partition(running, &st) == ESP_OK)
    f.running = st == ESP_OTA_IMG_PENDING_VERIFY ? RunningState::PendingVerify
              : st == ESP_OTA_IMG_VALID ? RunningState::Valid : RunningState::Other;
  else
    f.running = RunningState::Other;       // e.g. flashed over USB: no state kept for it
  f.runningSlot = running ? (int)running->subtype - (int)ESP_PARTITION_SUBTYPE_APP_OTA_MIN : -1;
  // the slot the bootloader rolled back from: its version from its own identity block (0x120)
  static ImageIdentityBlock invalid;
  const esp_partition_t* bad = esp_ota_get_last_invalid_partition();
  if (bad && esp_partition_read(bad, kIdentityOffset, &invalid, sizeof(invalid)) == ESP_OK) {
    f.haveInvalid = true;
    invalid.version[sizeof(invalid.version) - 1] = 0;
    f.invalidVersion = invalid.version;
  }
  return f;
}

bool loadRecord(OtaRecord* r) {
  Preferences p;
  if (!p.begin("ota", true)) return false;
  const size_t n = p.getBytes("rec", r, sizeof(*r));
  p.end();
  return n == sizeof(*r);
}

bool saveRecord(const OtaRecord& r) {
  Preferences p;
  if (!p.begin("ota", false)) return false;
  const size_t n = p.putBytes("rec", &r, sizeof(r));
  p.end();
  return n == sizeof(r);
}

bool markGood() { return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK; }
void rollBackNow() { esp_ota_mark_app_invalid_rollback_and_reboot(); }

uint8_t nextSlot() {
  const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
  return p ? (uint8_t)(p->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_MIN) : 0;
}

uint32_t slotBytes() {
  const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
  return p ? (uint32_t)p->size : 0;
}

}  // namespace ota

