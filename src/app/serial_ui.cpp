#include "serial_ui.h"
#include "wineador/logbook.h"
#include <Arduino.h>
#include "wineador/sensor_table.h"
#include "wineador/psychro.h"
#include "wineador/status.h"
#include "app/eventlog.h"
#include "drivers/sd_log.h"
#include "board.h"
#include "drivers/heap.h"
#include "drivers/sd_power.h"

namespace serialui {

using namespace wineador;

namespace {
const char* onOff(bool b) { return b ? "ON" : "off"; }
const char* dur(char* out, size_t n, uint64_t ms) { return formatDuration(out, n, ms); }

const char* zoneName(const Settings& s, const CabinetSummary& c) {
  if (c.cabinetOk == 0) return "unknown (no cabinet sensors)";
  const float sp = s.user.targetTempC, x = c.controlC;
  if (x > sp + s.tuning.coolingHysteresisC) return "cooling zone";
  if (x < sp - s.tuning.heatingDeadbandC) return "heating zone";
  return "dead zone";
}
}  // namespace

void printStatus(const View& v) {
  const Settings& s = *v.s;
  const CabinetSummary c = v.health->summary();
  char a[24], b[24];
  Serial.printf("\n  STATUS  %s  up %s  %s%s\n", v.timestamp, dur(a, sizeof(a), v.uptimeMs), v.version,
                v.clock->trusted() ? "" : "  (TIME UNTRUSTED)");
  Serial.println("  ------------------------------------------------------------------");
  for (int i = 0; i < kNumSensors; i++) {
    const SensorInfo& si = kSensors[i];
    if (v.health->healthy(i))
      Serial.printf("    ch%u %-13s %6.2f C  %5.1f %%RH%s\n", (unsigned)si.channel, si.position, v.health->tempC(i),
                    v.health->rhPct(i), v.testBuild ? "  (scripted)" : "");
    else
      Serial.printf("    ch%u %-13s EXCLUDED (%s)\n", (unsigned)si.channel, si.position, exclusionName(v.health->reason(i)));
  }
  if (c.cabinetOk > 0)
    Serial.printf("    cabinet       mean %.2f C, min %.2f, max %.2f, spread %.2f; RH %.1f%%; vapour pressure %.3f kPa\n"
                  "    control       %.2f C (%s) - %s; setpoint %.1f C, RH %.0f%%\n",
                  c.meanC, c.minC, c.maxC, c.spreadC, c.meanRhPct, c.meanVpKpa, c.controlC,
                  c.controlWarmest ? "WARMEST sensor: spread over 2 C" : "mean", zoneName(s, c), s.user.targetTempC,
                  s.user.rhSetpointPct);
  else
    Serial.println("    cabinet       NO HEALTHY CABINET SENSORS");
  Serial.printf("    loads         compressor %s, heater %s, humidifier %s, K1 %s side\n", onOff(v.loads.cool),
                onOff(v.loads.heat), onOff(v.loads.humid), v.loads.relayHeating ? "heating" : "cooling");
  Serial.printf("    plenum fan    %u%%, %lu rpm%s\n", (unsigned)v.fanDuty, (unsigned long)v.fanRpm, v.fanStalled ? "  STALLED" : "");
  if (v.lastCompressorStopMs)
    Serial.printf("    compressor    last stopped %s ago\n", dur(a, sizeof(a), v.uptimeMs - v.lastCompressorStopMs));
  else
    Serial.printf("    compressor    has not run since boot\n");
  char guards[160];
  formatGuardText(guards, sizeof(guards), *v.ctl);
  Serial.printf("    guards        %s\n", guards);
  Serial.printf("    since boot    crossovers %lu, RH strips %lu, humidifier calls %lu / runs %lu (disturbance detection: off until B6)\n",
                (unsigned long)v.tc->crossovers(), (unsigned long)v.tc->stripCount(), (unsigned long)v.hc->calls(),
                (unsigned long)v.hc->runs());
  // active faults
  Serial.print("    faults        ");
  bool any = false;
  auto fault = [&](const char* f) { Serial.printf("%s%s", any ? "; " : "", f); any = true; };
  for (int i = 0; i < kNumSensors; i++)
    if (!v.health->healthy(i)) { snprintf(b, sizeof(b), "ch%u excluded", (unsigned)kSensors[i].channel); fault(b); }
  if (v.fanStalled) fault("FAN STALL");
  if (v.tc->heaterIneffective()) fault("HEATER INEFFECTIVE");
  if (v.tc->coolingIneffective()) fault("COOLING INEFFECTIVE");
  if (v.hc->ineffective()) fault("HUMIDIFIER INEFFECTIVE");
  if (!v.rtcOk) fault("RTC FAIL");
  if (v.clock->osfAtBoot && !v.clock->corrected) fault("RTC LOST TIME (waiting for NTP)");
  const sdlog::Stats sd = sdlog::stats();
  if (sd.failed) fault("SD FAIL");
  if (!any) Serial.print("none");
  Serial.println("\n");
}

void printSensors(const View& v) {
  Serial.println("\n  SENSORS  (bus counts are real in every build; values are scripted in a test build)");
  for (int i = 0; i < kNumSensors; i++) {
    const SensorInfo& si = kSensors[i];
    const SensorReading& r = v.bus->s[i];
    Serial.printf("    ch%u %-13s %s  ", (unsigned)si.channel, si.position, si.label);
    if (v.health->healthy(i)) Serial.print("healthy     ");
    else Serial.printf("EXCLUDED %-4s", exclusionName(v.health->reason(i)));
    Serial.printf("  last read %-10s  CRC %lu  timeouts %lu", busResultName(r.bus), (unsigned long)v.crc[i],
                  (unsigned long)v.timeouts[i]);
    if (r.bus == BusResult::Ok)
      Serial.printf("  real %.2f C %.2f%% raw -> %.2f%%", r.tempC, r.rhRawPct, correctedRh(i, r.rhRawPct));
    if (v.s->user.manualExcludeMask & (1u << i)) Serial.print("  [manual exclude]");
    Serial.println();
  }
  Serial.println();
}

void printEvents(int n) {
  const int have = eventlog::count();
  if (n <= 0 || n > have) n = have;
  Serial.printf("\n  EVENTS  last %d of %lu since boot (oldest first)\n", n, (unsigned long)eventlog::total());
  for (int i = have - n; i < have; i++) {
    const eventlog::Entry& e = eventlog::recent(i);
    Serial.printf("    %s%s %-11s %s\n", e.timestamp, e.trusted ? "" : "?", e.type, e.detail);
  }
  if (have == 0) Serial.println("    (none yet)");
  Serial.println();
}

void printLog() {
  const sdlog::Stats s = sdlog::stats();
  char a[24];
  Serial.println("\n  LOG (SD card)");
  Serial.printf("    card          %s%s%s%s\n", s.mounted ? "mounted" : "NOT MOUNTED", s.failed ? ", FAILED: " : "",
                s.failed ? s.lastError : "", s.paused ? "; writes PAUSED while WiFi is on (Rev N)" : "");
  Serial.printf("    writing       %s; readings to %s\n", sdlog::started() ? "started" : "waiting for the clock",
                s.readingsPath[0] ? s.readingsPath : "(none yet)");
  Serial.printf("    card clock    SPI %lu kHz; outage buffers: %u events, %u readings rows, %u hourly rows (board.h)\n",
                (unsigned long)s.spiKhz, (unsigned)board::kSdEventsKept, (unsigned)board::kSdRowsKept, (unsigned)board::kSdHourlyKept);
  Serial.printf("    readings      %lu rows in RAM (written every 16), %lu written in %lu batches, %lu dropped (most kept: %lu)\n",
                (unsigned long)s.rowsBuffered, (unsigned long)s.rowsWritten, (unsigned long)s.flushes,
                (unsigned long)s.rowsDropped, (unsigned long)s.rowsBufferedMax);
  if (s.flushes)
    Serial.printf("    last batch    %lu rows, at uptime %s\n", (unsigned long)s.lastFlushRows,
                  formatDuration(a, sizeof(a), s.lastFlushUptimeMs));
  Serial.printf("    events        %lu written, %lu kept in RAM, %lu dropped (most kept: %lu)\n", (unsigned long)s.eventsWritten,
                (unsigned long)s.eventsPending, (unsigned long)s.eventsDropped, (unsigned long)s.eventsPendingMax);
  Serial.printf("    hourly        %lu rows written, %lu kept in RAM, %lu dropped (most kept: %lu)\n", (unsigned long)s.hourlyWritten,
                (unsigned long)s.hourlyPending, (unsigned long)s.hourlyDropped, (unsigned long)s.hourlyPendingMax);
  Serial.printf("    write time    last %lu ms, worst %lu ms since boot\n", (unsigned long)s.lastWriteMs,
                (unsigned long)s.worstWriteMs);
  if (s.totalBytes)
    Serial.printf("    space         %.1f GB free of %.1f GB (%.1f%%)\n", s.freeBytes / 1073741824.0, s.totalBytes / 1073741824.0,
                  100.0 * s.freeBytes / s.totalBytes);
  if (s.retentionDeleted >= 0) Serial.printf("    retention     boot check done: %d old month(s) deleted\n", s.retentionDeleted);
  Serial.printf("    retries       %lu writes recovered by a retry%s%s\n", (unsigned long)s.retries, s.retries ? "; last: " : "",
                s.retries ? s.lastRetry : "");
  Serial.printf("    failures      %lu since boot\n", (unsigned long)s.failures);
  if (sdpower::fitted())
    Serial.printf("    power cycles  %lu (the retry's remount failed; 3A.11), %lu of them saved the write\n",
                  (unsigned long)s.powerCycles, (unsigned long)s.powerCycleSaves);
  const uint32_t lowest = heap::lowest();               // internal RAM only (5.2)
  Serial.printf("    heap          internal RAM now %lu, lowest since boot %lu, largest block %lu (floor %lu KB: %s)\n",
                (unsigned long)heap::freeNow(), (unsigned long)lowest, (unsigned long)heap::largestBlock(),
                (unsigned long)(wineador::kHeapFloorBytes / 1024), lowest >= wineador::kHeapFloorBytes ? "ok" : "BELOW");
  if (heap::psramTotal() > 0)
    Serial.printf("    PSRAM         %lu KB: now %lu free, lowest since boot %lu; outage buffers %s\n",
                  (unsigned long)(heap::psramTotal() / 1024), (unsigned long)heap::psramFree(),
                  (unsigned long)heap::psramLowest(), s.buffersInPsram ? "in PSRAM" : "in internal RAM");
  else if (s.psramMissing)
    Serial.println("    PSRAM         NOT FOUND: outage buffers at Rev N's sizes in internal RAM");
  Serial.println();
}

void printSettings(const View& v) {
  const Settings& s = *v.s;
  const UserSettings& u = s.user;
  const TuningSettings& t = s.tuning;
  const SafetySettings& f = s.safety;
  Serial.println("\n  SETTINGS");
  Serial.println("    USER (change with 'set ...' / 'exclude ...'; saved)");
  Serial.printf("      target temperature %.1f C (%.0f-%.0f) · RH setpoint %.0f%% (%.0f-%.0f) · heater %s · humidifier %s · "
                "manual excludes 0x%02X\n", u.targetTempC, kTargetTempMinC, kTargetTempMaxC, u.rhSetpointPct,
                kRhSetpointMinPct, kRhSetpointMaxPct, u.heaterEnable ? "on" : "OFF", u.humidifierEnable ? "on" : "OFF",
                (unsigned)u.manualExcludeMask);
  Serial.println("    TUNING (B6 sets these; read-only here)");
  Serial.printf("      cooling hysteresis %.1f C · heating deadband %.1f C · heating ends %.1f C below · crossover %lu s\n",
                t.coolingHysteresisC, t.heatingDeadbandC, t.heatingStopBelowSetpointC, (unsigned long)(t.crossoverDelayMs / 1000));
  Serial.printf("      fan idle %u%% · compressor %u%% · cooling harvest %u->%u%% · heater %u%% · convergence %.1f C · backstop %lu s\n",
                t.fanIdlePct, t.fanCompressorPct, t.fanCoolHarvestStartPct, t.fanCoolHarvestEndPct, t.fanHeatingPct,
                t.harvestConvergenceC, (unsigned long)(t.harvestBackstopMs / 1000));
  Serial.printf("      strip %s: offset %.0f%%, persistence %lu s, floor %.1f C below · humidify band %.0f, persistence %lu s, "
                "during cooling %s\n", t.rhStripEnable ? "on" : "off", t.rhStripOffsetPct, (unsigned long)(t.rhStripPersistMs / 1000),
                t.rhStripFloorBelowSetpointC, t.humidifyBandPct, (unsigned long)(t.humidifyPersistMs / 1000),
                t.humidifyDuringCooling ? "on" : "off");
  Serial.printf("      heater cap %u%% · ineffective: heater +%.1f C/%lu s retry %lu s, cooling -%.1f C/%lu s, humidifier "
                "+%.1f pt/%lu s retry %lu s\n", t.heaterPwmCapPct, t.heaterIneffectiveRiseC,
                (unsigned long)(t.heaterIneffectiveWindowMs / 1000), (unsigned long)(t.heaterIneffectiveRetryMs / 1000),
                t.coolingIneffectiveFallC, (unsigned long)(t.coolingIneffectiveWindowMs / 1000), t.humidIneffectiveRisePct,
                (unsigned long)(t.humidIneffectiveWindowMs / 1000), (unsigned long)(t.humidIneffectiveRetryMs / 1000));
  Serial.printf("      dosed control %s · disturbance detection %s · log %lu s / %lu s active · sensor read %lu s\n",
                t.dosedControl ? "on" : "off", t.disturbanceDetection ? "on" : "off", (unsigned long)(t.logSteadyMs / 1000),
                (unsigned long)(t.logActiveMs / 1000), (unsigned long)(t.sensorReadMs / 1000));
  Serial.printf("    SAFETY (fixed in firmware) — profile %s\n", s.profile == Profile::Bench ? "BENCH" : "REAL");
  Serial.printf("      compressor off/on/max %lu/%lu/%lu s · heater %lu/%lu/%lu s · humidifier off/max %lu/%lu s · boot hold %lu s\n",
                (unsigned long)(f.compressorMinOffMs / 1000), (unsigned long)(f.compressorMinOnMs / 1000),
                (unsigned long)(f.compressorMaxRunMs / 1000), (unsigned long)(f.heaterMinOffMs / 1000),
                (unsigned long)(f.heaterMinOnMs / 1000), (unsigned long)(f.heaterMaxRunMs / 1000),
                (unsigned long)(f.humidMinOffMs / 1000), (unsigned long)(f.humidMaxRunMs / 1000),
                (unsigned long)(f.bootHoldMs / 1000));
  Serial.printf("      ceilings: cabinet setpoint+%.0f C, chamber %.0f C, RH %.0f%% · sensor hold %lu s · fan min %u%%, stall retry %lu s\n\n",
                f.cabinetCeilingAboveSetpointC, f.chamberCeilingC, f.rhHardCeilingPct, (unsigned long)(f.sensorRecoveryHoldMs / 1000),
                f.fanMinDutyPct, (unsigned long)(f.fanStallRetryMs / 1000));
}

}  // namespace serialui
