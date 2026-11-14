#pragma once
// The wording of the events (spec 5.4), defined once. Everything that writes an event, and
// everything that reads one back by its words, uses these:
//   - the firmware's event writers (src/main.cpp, src/drivers, lib/core);
//   - the weekly counts (log_read.h: weekKindOf);
//   - the native tests;
//   - tools/check_logs.py, through tools/event_text.json. That file is made from this list by
//     `python3 tools/make_event_text.py`, and the native test test_event_text fails if it is out
//     of date. So a change of wording here either reaches every reader or a test fails.
//
// Each type is the events file's "type" column. Each text is how that event's detail starts
// ("starts"), or a fragment found inside it ("contains").

#include <stdint.h>
#include <stddef.h>

namespace wineador {

// ---------------------------------------------------------------- types: X(Name, "type")
#define WINEADOR_EVENT_TYPES(X)                                    \
  X(Compressor, "compressor") X(Heater, "heater")                  \
  X(Humidifier, "humidifier") X(Interlock, "interlock")            \
  X(Harvest, "harvest") X(Guard, "guard") X(Crc, "crc")            \
  X(Timeout, "timeout") X(Sensor, "sensor") X(Fan, "fan")          \
  X(Settings, "settings") X(Boot, "boot") X(Sd, "sd") X(I2c, "i2c") \
  X(Rtc, "rtc") X(Ntp, "ntp") X(Crossover, "crossover")            \
  X(Strip, "strip") X(Humidify, "humidify") X(Exercise, "exercise") \
  X(Ineffective, "ineffective") X(Disturbance, "disturbance")      \
  X(Heap, "heap") X(Psram, "psram") X(Hwdog, "hwdog") X(Lcd, "lcd") \
  X(Supply12v, "12v") X(Wifi, "wifi") X(Door, "door")              \
  X(Ota, "ota") X(Blowers, "blowers") X(Ion, "ion")                \
  X(Scenario, "scenario")

#define WINEADOR_EV_TYPE_CONST(name, text) constexpr const char* kEvType##name = text;
WINEADOR_EVENT_TYPES(WINEADOR_EV_TYPE_CONST)
#undef WINEADOR_EV_TYPE_CONST

// ---------------------------------------------------------------- texts: X(Name, type, "text", match)
enum class EvMatch : uint8_t { Starts, Contains };

#define WINEADOR_EVENT_TEXTS(X)                                                                     \
  X(CompressorOn,   Compressor, "compressor ON",            Starts)                                 \
  X(CompressorOff,  Compressor, "compressor OFF",           Starts)                                 \
  X(HeaterOn,       Heater,     "heater ON",                Starts)                                 \
  X(HeaterOff,      Heater,     "heater OFF",               Starts)                                 \
  X(HumidifierOn,   Humidifier, "humidifier ON",            Starts)                                 \
  X(HumidifierOff,  Humidifier, "humidifier OFF",           Starts)                                 \
  X(MaxRun,         Guard,      "max-run",                  Contains) /* the guard's name, also in "... OFF (max-run)" */ \
  X(K1Heating,      Interlock,  "K1 to the heating side",   Starts)                                 \
  X(K1Cooling,      Interlock,  "K1 to the cooling side",   Starts)                                 \
  X(StripStart,     Strip,      "RH STRIP START",           Starts)                                 \
  X(StripStop,      Strip,      "RH STRIP STOP",            Starts)                                 \
  X(CrossoverNo,    Crossover,  "CROSSOVER",                Starts)                                 \
  X(HumidifyStart,  Humidify,   "HUMIDIFY CALL START",      Starts)                                 \
  X(HumidifyEnd,    Humidify,   "HUMIDIFY CALL END",        Starts)                                 \
  X(BootLine,       Boot,       "BOOT: reason",             Starts)                                 \
  X(BootProfile,    Boot,       "; profile ",               Contains) /* then REAL or BENCH */      \
  X(ChangeArrow,    Settings,   " -> ",                     Contains) /* old -> new */              \
  X(SettingsChanged, Settings,  "settings changed",         Starts)                                 \
  X(RtcLostTime,    Rtc,        "RTC LOST TIME",            Starts)                                 \
  X(NtpCorrection,  Ntp,        "NTP correction",           Starts)                                 \
  X(NtpWifiOn,      Ntp,        "WiFi on for the NTP sync", Starts)   /* Rev N's logs (read only) */ \
  X(NtpWifiOff,     Ntp,        "WiFi off:",                Starts)   /* Rev N's logs (read only) */ \
  X(SdFlushFailure, Sd,         "SD FLUSH FAILURE",         Starts)                                 \
  X(SdFailNo,       Sd,         "SD FAIL #",                Starts)                                 \
  X(SdRetention,    Sd,         "retention: free space under 10%, deleted", Starts)                 \
  X(SdCardBack,     Sd,         "SD card back: remounted; writing what was kept", Starts)           \
  X(SdPowerCycled,  Sd,         "SD remount for the retry failed: card power-cycled", Starts)       \
  X(SdRetriedOk,    Sd,         "SD write error, retried after a remount: OK", Starts)              \
  X(SdStressStart,  Sd,         "SD STRESS START",          Starts)                                 \
  X(SdStressDone,   Sd,         "SD STRESS DONE:",          Starts)                                 \
  X(SdStressChecks, Sd,         "SD STRESS DONE checks:",   Starts)                                 \
  X(Dip12v,         Supply12v,  "12V DIP",                  Starts)                                 \
  X(Recovered12v,   Supply12v,  "12V recovered",            Starts)                                 \
  X(WifiLost,       Wifi,       "WiFi lost",                Starts)                                 \
  X(WifiBack,       Wifi,       "WiFi back after",          Starts)                                 \
  X(HeapFloor,      Heap,       "FREE HEAP BELOW THE",      Starts)                                 \
  X(DoorTest,       Door,       "door test",                Starts)                                 \
  X(OtaStarted,     Ota,        "OTA UPDATE STARTED",       Starts)                                 \
  X(OtaInstalled,   Ota,        "OTA UPDATE INSTALLED",     Starts)                                 \
  X(OtaAbandoned,   Ota,        "OTA UPDATE ABANDONED",     Starts)                                 \
  X(OtaFailed,      Ota,        "OTA UPDATE FAILED",        Starts)                                 \
  X(OtaInterrupted, Ota,        "OTA UPDATE INTERRUPTED",   Starts)                                 \
  X(OtaRefused,     Ota,        "OTA upload refused",       Starts)                                 \
  X(OtaRefusedMore, Ota,        "more OTA uploads refused", Contains) /* "N more ... this hour" */   \
  X(OtaWaiting,     Ota,        "OTA: new image running",   Starts)                                 \
  X(OtaGood,        Ota,        "OTA: started properly",    Starts)                                 \
  X(OtaRolledBack,  Ota,        "OTA ROLLED BACK",          Starts)                                 \
  X(OtaSelfRollback, Ota,       "OTA: not started properly within 4 min 30 s", Starts)             \
  X(OtaRollbackTest, Ota,       "ROLLBACK TEST IMAGE",      Starts)                                 \
  X(BlowersBoost,   Blowers,    "BLOWERS BOOST",            Starts)                                 \
  X(BlowersIdle,    Blowers,    "BLOWERS IDLE",             Starts)                                 \
  X(BlowersFullEnd, Blowers,    "BLOWERS FULL SPEED ENDED", Starts)                                 \
  X(BlowersFull,    Blowers,    "BLOWERS FULL SPEED:",      Starts) /* the colon: not "...ENDED" */ \
  X(IonStart,       Ion,        "ION RUN START",            Starts)                                 \
  X(IonEnd,         Ion,        "ION RUN END",              Starts)                                 \
  X(IonCutShort,    Ion,        "ION RUN CUT SHORT",        Starts)                                 \
  X(IonSkipped,     Ion,        "ION RUN SKIPPED",          Starts)                                 \
  X(IonStopped,     Ion,        "ION RUN STOPPED",          Starts)                                 \
  X(IonRecordFailed, Ion,       "ION CAP RECORD NOT SAVED", Starts)                                 \
  X(IonRecordSaved, Ion,        "ION CAP RECORD SAVED again", Starts)                               \
  X(ScenarioResult, Scenario,   "======== run",             Contains) /* then the number, PASS ... */ \
  X(ScenarioLine,   Scenario,   "[run ",                    Contains)                               \
  X(ScenarioPass,   Scenario,   "PASS",                     Contains)

#define WINEADOR_EV_TEXT_CONST(name, type, text, match) constexpr const char* kEv##name = text;
WINEADOR_EVENT_TEXTS(WINEADOR_EV_TEXT_CONST)
#undef WINEADOR_EV_TEXT_CONST

struct EventText {
  const char* name;      // "StripStart"
  const char* type;      // "strip"
  const char* text;      // "RH STRIP START"
  EvMatch match;
};
extern const EventText kEventTexts[];
extern const int kEventTextCount;
struct EventType { const char* name; const char* type; };
extern const EventType kEventTypes[];
extern const int kEventTypeCount;

// True if an event of this type and detail is this text (by its match rule).
bool eventIs(const char* type, const char* detail, const EventText& t);

// tools/event_text.json, as the tool writes it and the test compares it.
int formatEventTextJson(char* out, size_t n);

// ---------------------------------------------------------------- events that carry numbers
// tools/check_logs.py reads numbers back out of these (test_event_numbers proves it reads what
// these write). The firmware writes them only through these functions.

// "12V recovered: back above 11.0 V after 5300 ms; lowest 10.52 V"
int formatRecovered12v(char* out, size_t n, float belowV, uint32_t durationMs, float lowestV);
// "WiFi back after 61 s"
int formatWifiBack(char* out, size_t n, uint32_t seconds);

// The SD stress test's figures (test builds, B4 / BP2 test 38).
struct SdStressFigures {
  uint32_t khz = 0;
  bool retry = false;
  uint32_t writes = 0, target = 0, failures = 0, retries = 0;
  uint32_t atFolder = 0, atOpen = 0, atHeader = 0, atData = 0, atVerify = 0;
  uint32_t worstMs = 0;
};
// done: "SD STRESS DONE: 1000 kHz, retry on: 6000 of 6000 writes, 0 failures, 3 retries (first
// attempt failed at: folder 0, open 0, header 0, data 3, verify 0); worst write 412 ms";
// otherwise the same as "SD STRESS progress: ...".
int formatSdStressLine(char* out, size_t n, bool done, const SdStressFigures& f);
// "SD STRESS DONE checks: size mismatches 0; heap lowest after a write 61234 (largest block then
// 30000), lowest since boot 58000". (Rev N's cards end "; paused for WiFi N s": card writes paused
// while its NTP-only WiFi was on. check_logs.py reads both.)
int formatSdStressChecks(char* out, size_t n, uint32_t sizeMismatches, uint32_t heapMin, uint32_t heapMinLargest,
                         uint32_t heapMinEver);

// The load events (spec 5.4), "compressor ON (temperature call, control 20.62 C)". load: 0
// compressor, 1 heater, 2 humidifier.
int formatLoadEvent(char* out, size_t n, int load, bool on, const char* why);
const char* loadEventType(int load);

}  // namespace wineador
