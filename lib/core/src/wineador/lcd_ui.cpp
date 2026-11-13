#include "wineador/lcd_ui.h"
#include <stdio.h>
#include <string.h>
#include "wineador/settings.h"
#include "wineador/uk_time.h"

namespace wineador {

static constexpr const char* kDeg = "\xC2\xB0";   // UTF-8 degree sign

const char* modeName(ModeIcon m) {
  switch (m) {
    case ModeIcon::Cooling: return "Cooling";
    case ModeIcon::Heating: return "Heating";
    case ModeIcon::Humidifying: return "Humidifying";
    case ModeIcon::Drying: return "Drying";
    case ModeIcon::Standby: return "Standby";
    case ModeIcon::Starting: return "Starting";
    case ModeIcon::Updating: return "Updating";
    case ModeIcon::Fault: return "Fault";
  }
  return "?";
}

const char* formatMss(char* out, size_t n, uint32_t ms) {
  const uint32_t s = (ms + 999) / 1000;
  snprintf(out, n, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
  return out;
}

// ---------------------------------------------------------------- Allow update window
bool UpdateWindow::active(uint32_t now) {
  if (open_ && now - openedMs_ >= kUpdateWindowMs) open_ = false;
  return open_;
}

uint32_t UpdateWindow::remainingMs(uint32_t now) {
  if (!active(now)) return 0;
  return kUpdateWindowMs - (now - openedMs_);
}

bool UpdateWindow::consume(uint32_t now) {
  const bool ok = active(now);
  open_ = false;
  return ok;
}

// ---------------------------------------------------------------- boot splash
UiView splashView(const char* version) {
  UiView v;
  v.screen = LcdScreen::Splash;
  snprintf(v.version, sizeof(v.version), "%s", version ? version : "");
  return v;
}

BootSplash::Phase BootSplash::update(bool painted, bool lcdIdle, uint32_t nowMs) {
  switch (phase_) {
    case Phase::Splash:
      // Fully on the screen: painted, and the LCD has sent every piece. Only then is the
      // picture free for the walk-past view (the driver reads it while it sends).
      if (painted && lcdIdle) { phase_ = Phase::Holding; shownMs_ = nowMs; }
      break;
    case Phase::Holding: {
      const uint32_t up = nowMs - shownMs_;
      if (up >= kSplashMaxMs || (painted && up >= kSplashMinMs)) {
        phase_ = Phase::Done;
        endedMs_ = nowMs;
        release_ = true;
      }
      break;
    }
    case Phase::Done: break;
  }
  return phase_;
}

bool BootSplash::takeRelease() {
  const bool r = release_;
  release_ = false;
  return r;
}

// ---------------------------------------------------------------- session
void LcdUi::begin(uint32_t nowMs, uint8_t dimPct) {
  dimPct_ = dimPct;
  screen_ = LcdScreen::WalkPast;
  lastTouchMs_ = nowMs;                 // full brightness for the first minute after boot
  down_ = false;
  window_.close();
}

bool LcdUi::dimmed(uint32_t nowMs) const { return nowMs - lastTouchMs_ >= kUiIdleMs; }

void LcdUi::touch(bool down, int x, int y, uint32_t nowMs) {
  const bool pressed = down && !down_;
  down_ = down;
  if (!pressed) return;
  const bool wasDim = dimmed(nowMs);
  lastTouchMs_ = nowMs;
  if (wasDim) return;                   // 5.5: the first touch only wakes the screen
  press(x, y, nowMs);
}

static float clampStep(float v, float step, float lo, float hi) {
  v = roundToStep(v, step);
  return v < lo ? lo : v > hi ? hi : v;
}

void LcdUi::press(int x, int y, uint32_t nowMs) {
  using namespace layout;
  constexpr int16_t kSlop = 6;
  switch (screen_) {
    case LcdScreen::WalkPast:
      if (v_.alertLine && grow(kAlertPill, kSlop).contains(x, y)) { screen_ = LcdScreen::Alerts; page_ = 0; }
      else if (!in_.updating && grow(kSettingsPill, kSlop).contains(x, y)) {
        screen_ = LcdScreen::Settings;
        draftT_ = clampStep(in_.targetC, kTargetTempStepC, kTargetTempMinC, kTargetTempMaxC);
        draftRh_ = clampStep(in_.rhSetPct, kRhSetpointStepPct, kRhSetpointMinPct, kRhSetpointMaxPct);
      }
      break;
    case LcdScreen::Settings:
      if (grow(kTempMinus, kSlop).contains(x, y)) draftT_ = clampStep(draftT_ - kTargetTempStepC, kTargetTempStepC, kTargetTempMinC, kTargetTempMaxC);
      else if (grow(kTempPlus, kSlop).contains(x, y)) draftT_ = clampStep(draftT_ + kTargetTempStepC, kTargetTempStepC, kTargetTempMinC, kTargetTempMaxC);
      else if (grow(kRhMinus, kSlop).contains(x, y)) draftRh_ = clampStep(draftRh_ - kRhSetpointStepPct, kRhSetpointStepPct, kRhSetpointMinPct, kRhSetpointMaxPct);
      else if (grow(kRhPlus, kSlop).contains(x, y)) draftRh_ = clampStep(draftRh_ + kRhSetpointStepPct, kRhSetpointStepPct, kRhSetpointMinPct, kRhSetpointMaxPct);
      else if (grow(kAllowUpdate, kSlop).contains(x, y)) { window_.open(nowMs); windowOpened_ = true; }
      else if (grow(kCancel, kSlop).contains(x, y)) toWalkPast();
      else if (grow(kSave, kSlop).contains(x, y)) {
        if (draftT_ != in_.targetC || draftRh_ != in_.rhSetPct) {
          saveReady_ = true;
          saveT_ = draftT_;
          saveRh_ = draftRh_;
        }
        toWalkPast();
      }
      break;
    case LcdScreen::Alerts: {
      const int pages = alerts_.count() == 0 ? 1 : (alerts_.count() + kAlertRows - 1) / kAlertRows;
      if (grow(kBack, kSlop).contains(x, y)) toWalkPast();
      else if (pages > 1 && grow(kPageUp, kSlop).contains(x, y)) { if (page_ > 0) page_--; }
      else if (pages > 1 && grow(kPageDown, kSlop).contains(x, y)) { if (page_ + 1 < pages) page_++; }
      break;
    }
    case LcdScreen::Splash: break;     // never LcdUi's own screen
  }
}

bool LcdUi::takeSave(float* t, float* rh) {
  if (!saveReady_) return false;
  saveReady_ = false;
  if (t) *t = saveT_;
  if (rh) *rh = saveRh_;
  return true;
}

bool LcdUi::takeWindowOpened() {
  const bool w = windowOpened_;
  windowOpened_ = false;
  return w;
}

void LcdUi::update(const UiInputs& in) {
  in_ = in;
  const uint32_t now = in.nowMs;
  alerts_.update(in.alerts, in.uptimeMs);

  // Leaving a screen (5.5): 60 s without a touch, or an update starting (both close).
  if (screen_ != LcdScreen::WalkPast && now - lastTouchMs_ >= kUiIdleMs) toWalkPast();
  if (in.updating && !wasUpdating_) toWalkPast();
  if (in.updating && screen_ == LcdScreen::Settings) toWalkPast();
  wasUpdating_ = in.updating;

  UiView& v = v_;
  v = UiView{};
  v.screen = screen_;
  v.tag = in.tag;
  v.wifi = in.wifi;
  v.backlightPct = backlightPct(now);

  // Time (5.5): UK local, "--:--" while the clock is not trusted.
  if (in.clockOk) formatHhMm(v.timeText, sizeof(v.timeText), ukLocal(in.utcNowS));

  // Readings: "--" with fewer than 3 healthy cabinet sensors or no fresh reading (5.5).
  v.readingsOk = in.cabinetOk >= 3 && in.fresh;
  if (v.readingsOk) {
    snprintf(v.tempText, sizeof(v.tempText), "%.1f", (double)in.meanC);
    snprintf(v.rhText, sizeof(v.rhText), "%.0f", (double)in.meanRhPct);
  }
  snprintf(v.setTempText, sizeof(v.setTempText), "Set %.1f %sC", (double)in.targetC, kDeg);
  snprintf(v.setRhText, sizeof(v.setRhText), "Set %.0f%%", (double)in.rhSetPct);

  // Mode icons.
  const bool fault = alerts_.count() > 0 && alerts_.at(0).rank == 0;
  auto addMode = [&](ModeIcon m, const char* text) {
    if (v.nModes >= kMaxModes) return;
    v.modes[v.nModes] = m;
    snprintf(v.modeText[v.nModes], sizeof(v.modeText[0]), "%s", text);
    v.nModes++;
  };
  char t[24];
  if (fault) addMode(ModeIcon::Fault, "Fault");
  else if (in.updating) {
    snprintf(t, sizeof(t), "Updating %u%%", (unsigned)in.updatePct);
    addMode(ModeIcon::Updating, t);
  } else if (in.bootHold) {
    char c[12];
    snprintf(t, sizeof(t), "Starting %s", formatMss(c, sizeof(c), in.bootHoldLeftMs));
    addMode(ModeIcon::Starting, t);
  } else {
    if (in.cool) addMode(in.stripping ? ModeIcon::Drying : ModeIcon::Cooling, in.stripping ? "Drying" : "Cooling");
    if (in.heat) addMode(ModeIcon::Heating, "Heating");
    if (in.humid) addMode(ModeIcon::Humidifying, "Humidifying");
    if (v.nModes == 0) addMode(ModeIcon::Standby, "Standby");
  }

  // Allow update window: its own line on the walk-past screen; on the settings button too.
  if (window_.active(now)) {
    char c[12];
    formatMss(c, sizeof(c), window_.remainingMs(now));
    v.updateWindow = true;
    snprintf(v.updateText, sizeof(v.updateText), "Update allowed - %s left", c);
    snprintf(v.allowText, sizeof(v.allowText), "Update allowed");
    snprintf(v.allowSub, sizeof(v.allowSub), "%s", c);
  }

  // Alert line: the most serious, "+N more".
  v.alertCount = (uint8_t)alerts_.count();
  if (alerts_.any()) {
    v.alertLine = true;
    snprintf(v.alertText, sizeof(v.alertText), "%s", alerts_.at(0).title);
    v.alertRank = alerts_.at(0).rank;
    if (alerts_.count() > 1) snprintf(v.moreText, sizeof(v.moreText), "+%u more", (unsigned)(alerts_.count() - 1) % 100u);
  }
  v.settingsEnabled = !in.updating;

  // Settings draft.
  snprintf(v.draftTemp, sizeof(v.draftTemp), "%.1f", (double)draftT_);
  snprintf(v.draftRh, sizeof(v.draftRh), "%.0f", (double)draftRh_);
  v.tempDown = draftT_ > kTargetTempMinC;
  v.tempUp = draftT_ < kTargetTempMaxC;
  v.rhDown = draftRh_ > kRhSetpointMinPct;
  v.rhUp = draftRh_ < kRhSetpointMaxPct;
  v.draftChanged = draftT_ != in.targetC || draftRh_ != in.rhSetPct;

  // Alerts screen: pages of five; the page stays inside the list as alerts clear.
  const int pages = alerts_.count() == 0 ? 1 : (alerts_.count() + layout::kAlertRows - 1) / layout::kAlertRows;
  if (page_ >= pages) page_ = (uint8_t)(pages - 1);
  v.pages = (uint8_t)pages;
  v.page = page_;
  for (int i = 0; i < layout::kAlertRows; i++) {
    const int k = page_ * layout::kAlertRows + i;
    if (k >= alerts_.count()) break;
    const Alert& a = alerts_.at(k);
    AlertRowView& r = v.row[v.rows++];
    snprintf(r.title, sizeof(r.title), "%s", a.title);
    formatAlertWhen(r.when, sizeof(r.when), a.sinceMs, in.uptimeMs, in.clockOk, in.utcNowS);
    r.rank = a.rank;
  }
}

}  // namespace wineador
