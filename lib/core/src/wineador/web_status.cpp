#include "wineador/web_status.h"
#include "wineador/sensor_table.h"
#include "wineador/uk_time.h"
#include "wineador/log_read.h"
#include <stdio.h>
#include <math.h>

namespace wineador {

const char* zoneName(float controlC, float targetC, float hyst, float deadband) {
  if (controlC > targetC + hyst) return "cool";
  if (controlC < targetC - deadband) return "heat";
  return "dead";
}

const char* sensorStateName(SensorState s) {
  switch (s) {
    case SensorState::NotRead:  return "not_read";
    case SensorState::Healthy:  return "healthy";
    case SensorState::Excluded: return "excluded";
    case SensorState::Manual:   return "manual";
  }
  return "?";
}

const char* modeKey(ModeIcon m) {
  switch (m) {
    case ModeIcon::Cooling:     return "cooling";
    case ModeIcon::Heating:     return "heating";
    case ModeIcon::Humidifying: return "humidifying";
    case ModeIcon::Drying:      return "drying";
    case ModeIcon::Standby:     return "standby";
    case ModeIcon::Starting:    return "starting";
    case ModeIcon::Updating:    return "updating";
    case ModeIcon::Fault:       return "fault";
  }
  return "?";
}

void copyFromLcd(StatusSnapshot* s, const LcdUi& ui, uint64_t uptimeMs, bool clockOk, int64_t utcNowS) {
  const UiView& v = ui.view();
  s->nModes = v.nModes;
  for (int i = 0; i < v.nModes && i < kMaxModes; i++) s->modes[i] = v.modes[i];
  const AlertTracker& a = ui.alerts();
  s->nAlerts = (uint8_t)(a.count() < kAlertSlots ? a.count() : kAlertSlots);
  for (int i = 0; i < s->nAlerts; i++) {
    const Alert& x = a.at(i);
    AlertStatus& o = s->alerts[i];
    snprintf(o.title, sizeof(o.title), "%s", x.title);
    formatAlertWhen(o.when, sizeof(o.when), x.sinceMs, uptimeMs, clockOk, utcNowS);
    o.rank = x.rank;
  }
  s->clockOk = clockOk;
  s->utcNowS = utcNowS;
}

static const char* roleOf(int ch) {
  if (ch < kCabinetSensors) return "cabinet";
  return ch == kChamberIndex ? "chamber" : "ambient";
}

static const char* loadName(uint8_t l) {
  return l == 0 ? "compressor" : l == 1 ? "heater" : "humidifier";
}

// phases: 0 head, 1 modes, 2 cabinet, 3 loads, 4 guards, 5 alerts (one step each), 6 sensors (one
// step each), 7 week + update + card, 8 end
bool StatusReply::step(JsonWriter& w) {
  const StatusSnapshot& s = s_;
  switch (phase_) {
    case 0: {
      w.beginObject();
      w.kInt("api", kApiVersion);
      w.key("time");
      w.beginObject();
      w.kInt("utc_s", s.utcNowS);
      char t[24];
      if (s.clockOk) {
        const LocalTime lt = ukLocal(s.utcNowS);
        formatHhMm(t, sizeof(t), lt);
        w.kStr("uk", t);
        snprintf(t, sizeof(t), "%04d-%02d-%02d", lt.year, lt.month, lt.day);
        w.kStr("uk_date", t);
      } else {
        w.kStr("uk", "--:--");
        w.kNull("uk_date");
      }
      w.kBool("trusted", s.clockOk);
      w.endObject();
      writeBuild(w, s.build);
      w.kInt("uptime_s", (int64_t)s.uptimeS);
      phase_ = 1;
      return true;
    }
    case 1: {
      w.key("modes");
      w.beginArray();
      for (int i = 0; i < s.nModes && i < kMaxModes; i++) w.str(modeKey(s.modes[i]));
      w.endArray();
      const bool fault = s.nAlerts > 0 && s.alerts[0].rank == 0;
      if (fault) w.kStr("fault", s.alerts[0].title);
      else w.kNull("fault");
      w.kInt("boot_hold_s", s.bootHold ? (int64_t)s.bootHoldLeftS : 0);
      phase_ = 2;
      return true;
    }
    case 2: {
      w.key("cabinet");
      w.beginObject();
      if (s.readingsOk) {
        w.kNum("t", s.meanC, 2);
        w.kNum("rh", s.meanRhPct, 2);
        w.kNum("t_min", s.minC, 2);
        w.kNum("t_max", s.maxC, 2);
        w.kNum("spread_t", s.spreadC, 2);
        w.kNum("control_t", s.controlC, 2);
        w.kStr("zone", zoneName(s.controlC, s.targetC, s.coolingHysteresisC, s.heatingDeadbandC));
      } else {
        w.kNull("t"); w.kNull("rh"); w.kNull("t_min"); w.kNull("t_max");
        w.kNull("spread_t"); w.kNull("control_t"); w.kNull("zone");
      }
      w.kInt("healthy", s.cabinetOk);
      w.kInt("age_s", s.readingsAgeS);
      w.endObject();
      w.key("setpoints");
      w.beginObject();
      w.kNum("t", s.targetC, 1);
      w.kNum("rh", s.rhSetPct, 0);
      w.endObject();
      phase_ = 3;
      return true;
    }
    case 3: {
      w.key("loads");
      w.beginObject();
      w.key("compressor");
      w.beginObject();
      w.kBool("on", s.cool);
      w.kBool("stripping", s.cool && s.stripping);
      if (s.cool) w.kInt("for_s", s.compressorOnForS);
      if (s.compressorStoppedSinceBoot) w.kInt("stopped_ago_s", s.compressorStoppedAgoS);
      else w.kNull("stopped_ago_s");
      w.endObject();
      w.key("heater");
      w.beginObject();
      w.kBool("on", s.heat);
      w.kInt("duty_pct", s.heat ? s.heaterDutyPct : 0);
      w.endObject();
      w.key("humidifier"); w.beginObject(); w.kBool("on", s.humid); w.endObject();
      w.key("fan"); w.beginObject(); w.kInt("pwm", s.fanPct); w.kInt("rpm", s.fanRpm); w.endObject();
      w.kStr("interlock", s.relayHeating ? "heating" : "cooling");
      w.key("blowers"); w.beginObject(); w.kInt("pwm", s.blowerPct); w.kBool("boost", s.blowerBoost); w.endObject();
      w.key("ion"); w.beginObject(); w.kBool("on", s.ionOn); w.endObject();
      w.endObject();
      phase_ = 4;
      return true;
    }
    case 4: {
      w.key("guards");
      w.beginArray();
      for (int i = 0; i < s.nGuards && i < 3; i++) {
        w.beginObject();
        w.kStr("load", loadName(s.guards[i].load));
        w.kStr("reason", s.guards[i].reason);
        if (s.guards[i].leftS >= 0) w.kInt("left_s", s.guards[i].leftS);
        else w.kNull("left_s");
        w.endObject();
      }
      w.endArray();
      w.key("alerts");
      w.beginArray();
      phase_ = 5;
      i_ = 0;
      return true;
    }
    case 5: {
      if (i_ < s.nAlerts && i_ < kAlertSlots) {
        const AlertStatus& a = s.alerts[i_++];
        w.beginObject();
        w.kStr("title", a.title);
        w.kStr("since", a.when);
        w.kInt("rank", a.rank);
        w.kBool("fault", a.rank == 0);
        w.endObject();
        return true;
      }
      w.endArray();
      w.key("sensors");
      w.beginArray();
      phase_ = 6;
      i_ = 0;
      return true;
    }
    case 6: {
      if (i_ < kNumSensors) {
        const int c = i_++;
        const SensorStatus& x = s.sensors[c];
        w.beginObject();
        w.kInt("ch", c);
        w.kStr("name", kSensors[c].position);
        w.kStr("probe", kSensors[c].label);
        w.kStr("role", roleOf(c));
        w.kStr("state", sensorStateName(x.state));
        if (x.reason[0]) w.kStr("reason", x.reason);
        else w.kNull("reason");
        if (x.read) {
          w.kNum("t", x.tempC, 2);
          w.kNum("rh", x.rhPct, 2);
          w.kNum("rh_raw", x.rhRawPct, 2);
        } else {
          w.kNull("t"); w.kNull("rh"); w.kNull("rh_raw");
        }
        w.kInt("age_s", x.ageS);
        w.kInt("crc_hour", x.crcHour);
        w.kInt("timeouts_hour", x.timeoutsHour);
        w.endObject();
        return true;
      }
      w.endArray();
      phase_ = 7;
      return true;
    }
    case 7: {
      w.key("week");
      w.beginObject();
      if (s.weekReady) {
        w.kInt("crossovers", s.crossovers);
        w.kInt("strips", s.strips);
        w.kInt("humid_runs", s.humidRuns);
        if (s.disturbanceDetection) w.kInt("disturbances", s.disturbances);
        else w.kNull("disturbances");
      } else {
        w.kNull("crossovers"); w.kNull("strips"); w.kNull("humid_runs"); w.kNull("disturbances");
      }
      w.kBool("ready", s.weekReady);
      w.endObject();
      w.key("update");
      w.beginObject();
      w.kBool("window_open", s.windowOpen);
      w.kInt("window_s", s.windowOpen ? s.windowLeftS : 0);
      w.kBool("updating", s.updating);
      w.kInt("pct", s.updating ? s.updatePct : 0);
      w.endObject();
      w.key("card");
      w.beginObject();
      w.kBool("ok", s.sdOk);
      w.kInt("free_bytes", (int64_t)s.sdFreeBytes);
      w.endObject();
      w.key("wifi");
      w.beginObject();
      w.kInt("rssi", s.wifiRssi);
      w.endObject();
      w.key("board");
      w.beginObject();
      w.kStr("version", s.build.version);
      w.kInt("uptime_s", (int64_t)s.uptimeS);
      w.kStr("restart_reason", s.bootReason ? s.bootReason : "");
      if (s.hasV12) { w.kNum("v12_now", s.v12NowV, 2); w.kNum("v12_low_today", s.v12LowTodayV, 2); }
      else { w.kNull("v12_now"); w.kNull("v12_low_today"); }
      w.kInt("wifi_rssi", s.wifiRssi);
      w.kInt("heap_free_bytes", s.heapFreeBytes);
      if (s.sdOk) { w.kInt("card_free_bytes", (int64_t)s.sdFreeBytes); w.kInt("card_total_bytes", (int64_t)s.sdTotalBytes); }
      else { w.kNull("card_free_bytes"); w.kNull("card_total_bytes"); }
      w.endObject();
      phase_ = 8;
      return true;
    }
    case 8:
      w.endObject();
      phase_ = 9;
      return true;
    default:
      return false;
  }
}

}  // namespace wineador
