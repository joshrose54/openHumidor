#include "lcdgfx/screens.h"
#include <stdio.h>
#include <string.h>
#include "lcdgfx/palette.h"

namespace lcdgfx {

using namespace wineador;
namespace L = wineador::layout;

static constexpr const char* kDeg = "\xC2\xB0";
static constexpr const char* kDash = "\xE2\x80\x93";

// ---------------------------------------------------------------- places
Rect elementRect(Element e) {
  const int i = (int)e;
  if (i < kBgStrips) return Rect(0, (int16_t)(i * 40), kScreenW, 40);
  switch (e) {
    case Element::Time: return L::kTime;
    case Element::Tag: return L::kTag;
    case Element::Wifi: return L::kWifi;
    case Element::Temp: return L::kTemp;
    case Element::Rh: return L::kRh;
    case Element::Setpoints: return L::kSetpoints;
    case Element::Modes: return L::kModes;
    case Element::Update: return L::kUpdate;
    case Element::AlertLine: return L::kAlertLine;
    case Element::SettingsButton: return L::kSettingsButton;
    case Element::SetTitle: return L::kSetTitle;
    case Element::TempLabel: return L::kTempLabel;
    case Element::TempControl: return L::kTempControl;
    case Element::RhLabel: return L::kRhLabel;
    case Element::RhControl: return L::kRhControl;
    case Element::UpdateButton: return L::kUpdateButtonArea;
    case Element::CancelSave: return L::kCancelSaveArea;
    case Element::AlertsTitle: return L::kAlertsTitle;
    case Element::AlertsBar: return L::kAlertsBar;
    default:
      if (e >= Element::Row0 && e <= Element::Row4) return L::alertRow((int)e - (int)Element::Row0);
      return Rect();
  }
}

int elementsOf(LcdScreen s, Element* out, int max) {
  static const Element walk[] = {Element::Time, Element::Tag, Element::Wifi, Element::Temp, Element::Rh,
                                 Element::Setpoints, Element::Modes, Element::Update, Element::AlertLine,
                                 Element::SettingsButton};
  static const Element set[] = {Element::SetTitle, Element::TempLabel, Element::TempControl, Element::RhLabel,
                                Element::RhControl, Element::UpdateButton, Element::CancelSave};
  static const Element alerts[] = {Element::AlertsTitle, Element::Row0, Element::Row1, Element::Row2,
                                   Element::Row3, Element::Row4, Element::AlertsBar};
  const Element* list = s == LcdScreen::WalkPast ? walk : s == LcdScreen::Settings ? set : alerts;
  const int n = s == LcdScreen::WalkPast ? (int)(sizeof(walk) / sizeof(walk[0]))
              : s == LcdScreen::Settings ? (int)(sizeof(set) / sizeof(set[0]))
                                         : (int)(sizeof(alerts) / sizeof(alerts[0]));
  int k = 0;
  for (int i = 0; i < n && k < max; i++) out[k++] = list[i];
  return k;
}

// ---------------------------------------------------------------- small helpers
static uint16_t modeColour(ModeIcon m) {
  switch (m) {
    case ModeIcon::Cooling: return pal::kCool;
    case ModeIcon::Heating: return pal::kHeat;
    case ModeIcon::Humidifying: return pal::kHumid;
    case ModeIcon::Drying: return pal::kDry;
    case ModeIcon::Standby: return pal::kStandby;
    case ModeIcon::Starting: case ModeIcon::Updating: return pal::kAccent;
    case ModeIcon::Fault: return pal::kFault;
  }
  return pal::kText;
}

static const Icon& modeIcon(ModeIcon m) {
  switch (m) {
    case ModeIcon::Cooling: return kIconCooling;
    case ModeIcon::Heating: return kIconHeating;
    case ModeIcon::Humidifying: return kIconHumidifying;
    case ModeIcon::Drying: return kIconDrying;
    case ModeIcon::Standby: return kIconStandby;
    case ModeIcon::Starting: return kIconStarting;
    case ModeIcon::Updating: return kIconUpdating;
    case ModeIcon::Fault: return kIconFault;
  }
  return kIconStandby;
}

static void textCentred(Canvas& c, const Font& f, Rect r, int16_t baseline, const char* s, uint16_t col) {
  c.text(f, (int16_t)(r.x + (r.w - Canvas::measure(f, s)) / 2), baseline, s, col);
}

static void textRight(Canvas& c, const Font& f, int16_t right, int16_t baseline, const char* s, uint16_t col) {
  c.text(f, (int16_t)(right - Canvas::measure(f, s)), baseline, s, col);
}

// Vertically: the baseline that centres a font's capitals in a box.
static int16_t midBaseline(const Font& f, Rect r) {
  const Glyph* g = findGlyph(f, 'H');
  const int cap = g ? -g->yoff : f.ascent * 7 / 10;
  return (int16_t)(r.y + (r.h + cap) / 2);
}

static void button(Canvas& c, Rect r, uint16_t fill, const char* label, uint16_t textCol, const Font& f = kFontMedium) {
  c.roundRect(r, 10, fill);
  textCentred(c, f, r, midBaseline(f, r), label, textCol);
}

static void iconButton(Canvas& c, Rect r, const Icon& ic, bool enabled) {
  c.roundRect(r, 10, enabled ? pal::kSurface : pal::kBg);
  if (!enabled) {   // an outline only: there is nothing further to go
    c.roundRect(r, 10, pal::kLine);
    c.roundRect(Rect((int16_t)(r.x + 2), (int16_t)(r.y + 2), (int16_t)(r.w - 4), (int16_t)(r.h - 4)), 8, pal::kBg);
  }
  c.icon(ic, (int16_t)(r.x + (r.w - ic.w) / 2), (int16_t)(r.y + (r.h - ic.h) / 2), enabled ? pal::kText : pal::kFaint);
}

static const char* tagText(BuildTag t) {
  return t == BuildTag::Bench ? "BENCH" : t == BuildTag::Test ? "TEST" : t == BuildTag::BenchTest ? "BENCH TEST" : "";
}

// ---------------------------------------------------------------- painting
static void paintReading(Canvas& c, Rect r, const char* caption, const char* value, const char* unit, bool ok) {
  const int16_t x = (int16_t)(r.x + 16);
  c.text(kFontSmall, x, (int16_t)(r.y + 24), caption, pal::kMuted);
  const int16_t base = (int16_t)(r.y + 116);
  const int16_t w = c.text(kFontHuge, x, base, value, ok ? pal::kText : pal::kMuted);
  const Glyph* g = findGlyph(kFontHuge, '0');
  const int16_t top = (int16_t)(base + (g ? g->yoff : -73));          // the digits' top
  const Glyph* u = findGlyph(kFontLarge, 'C');
  c.text(kFontLarge, (int16_t)(x + w + 6), (int16_t)(top - (u ? u->yoff : -29)), unit, ok ? pal::kMuted : pal::kFaint);
}

static void paintRow(Canvas& c, const UiView& v, int i, Rect r) {
  if (i < v.rows) {
    const AlertRowView& a = v.row[i];
    c.roundRect(Rect(16, (int16_t)(r.y + 9), 5, 26), 2, a.rank == 0 ? pal::kFault : pal::kWarn);
    const int16_t whenW = Canvas::measure(kFontSmall, a.when);
    char t[48];
    fitText(t, sizeof(t), kFontMedium, a.title, (int16_t)(464 - whenW - 16 - 32));
    c.text(kFontMedium, 32, midBaseline(kFontMedium, r), t, pal::kText);
    textRight(c, kFontSmall, 464, midBaseline(kFontSmall, r), a.when, pal::kMuted);
  } else if (i == 1 && v.rows == 0) {
    textCentred(c, kFontMedium, r, midBaseline(kFontMedium, r), "No active alerts", pal::kMuted);
  }
  if (i < v.rows) c.fill(Rect(16, (int16_t)(r.y + r.h - 1), 448, 1), pal::kLine);
}

void paintElement(Canvas& c, const UiView& v, Element e) {
  const Rect r = elementRect(e);
  c.setClip(r);
  c.fill(r, pal::kBg);
  char buf[64];
  switch (e) {
    case Element::Time:
      c.text(kFontMedium, 16, midBaseline(kFontMedium, r), v.timeText, v.timeText[0] == '-' ? pal::kMuted : pal::kText);
      break;
    case Element::Tag:
      if (v.tag != BuildTag::None) {
        const char* t = tagText(v.tag);
        const int16_t w = (int16_t)(Canvas::measure(kFontSmall, t) + 20);
        const Rect p((int16_t)(r.x + (r.w - w) / 2), 8, w, 24);
        c.roundRect(p, 6, pal::kWarn);
        textCentred(c, kFontSmall, p, midBaseline(kFontSmall, p), t, pal::kBg);
      }
      break;
    case Element::Wifi:
      c.icon(v.wifi ? kIconWifi : kIconWifiOff, (int16_t)(r.x + r.w - 16 - 28), 6, v.wifi ? pal::kText : pal::kFaint);
      break;
    case Element::Temp: {
      snprintf(buf, sizeof(buf), "%sC", kDeg);
      paintReading(c, r, "Cabinet", v.tempText, buf, v.readingsOk);
      break;
    }
    case Element::Rh: paintReading(c, r, "Humidity", v.rhText, "%", v.readingsOk); break;
    case Element::Setpoints:
      c.text(kFontSmall, 16, (int16_t)(r.y + 18), v.setTempText, pal::kMuted);
      c.text(kFontSmall, 304, (int16_t)(r.y + 18), v.setRhText, pal::kMuted);
      break;
    case Element::Modes: {
      int16_t x = 16;
      for (int i = 0; i < v.nModes; i++) {
        const uint16_t col = modeColour(v.modes[i]);
        c.icon(modeIcon(v.modes[i]), x, (int16_t)(r.y + 4), col);
        x = (int16_t)(x + 56);
        const int16_t w = c.text(kFontMedium, x, midBaseline(kFontMedium, r), v.modeText[i],
                                 v.modes[i] == ModeIcon::Fault ? pal::kFault : pal::kText);
        x = (int16_t)(x + w + 28);
      }
      break;
    }
    case Element::Update:
      if (v.updateWindow) c.text(kFontSmall, 16, (int16_t)(r.y + 16), v.updateText, pal::kAccent);
      break;
    case Element::AlertLine:
      if (v.alertLine) {
        const Rect p = L::kAlertPill;
        const bool fault = v.alertRank == 0;
        c.roundRect(p, 8, fault ? pal::kFaultBg : pal::kWarnBg);
        c.icon(kIconAlert, (int16_t)(p.x + 8), (int16_t)(p.y + (p.h - 22) / 2), fault ? pal::kFault : pal::kWarn);
        const int16_t moreW = v.moreText[0] ? (int16_t)(Canvas::measure(kFontSmall, v.moreText) + 12) : 0;
        const int16_t room = (int16_t)(p.w - 38 - 10 - moreW);
        fitText(buf, sizeof(buf), kFontSmall, v.alertText, room);
        c.text(kFontSmall, (int16_t)(p.x + 38), midBaseline(kFontSmall, p), buf, pal::kText);
        if (moreW) textRight(c, kFontSmall, (int16_t)(p.x + p.w - 10), midBaseline(kFontSmall, p), v.moreText, pal::kMuted);
      }
      break;
    case Element::SettingsButton: {
      const Rect p = L::kSettingsPill;
      c.roundRect(p, 8, pal::kSurface);
      const uint16_t col = v.settingsEnabled ? pal::kText : pal::kFaint;
      const int16_t w = (int16_t)(24 + 6 + Canvas::measure(kFontSmall, "Settings"));
      const int16_t x = (int16_t)(p.x + (p.w - w) / 2);
      c.icon(kIconSettings, x, (int16_t)(p.y + (p.h - 24) / 2), col);
      c.text(kFontSmall, (int16_t)(x + 30), midBaseline(kFontSmall, p), "Settings", col);
      break;
    }
    // ---- settings
    case Element::SetTitle:
      c.text(kFontMedium, 16, midBaseline(kFontMedium, r), "Settings", pal::kText);
      if (v.draftChanged) textRight(c, kFontSmall, 464, midBaseline(kFontSmall, r), "Not saved yet", pal::kAccent);
      break;
    case Element::TempLabel:
    case Element::RhLabel: {
      const bool temp = e == Element::TempLabel;
      c.text(kFontMedium, 16, (int16_t)(r.y + 32), temp ? "Temperature" : "Humidity", pal::kText);
      if (temp) snprintf(buf, sizeof(buf), "15%s22 %sC", kDash, kDeg);
      else snprintf(buf, sizeof(buf), "55%s70%%", kDash);
      c.text(kFontSmall, 16, (int16_t)(r.y + 56), buf, pal::kMuted);
      c.text(kFontSmall, 16, (int16_t)(r.y + 78), temp ? "steps of 0.5" : "steps of 1", pal::kMuted);
      break;
    }
    case Element::TempControl:
    case Element::RhControl: {
      const bool temp = e == Element::TempControl;
      iconButton(c, temp ? L::kTempMinus : L::kRhMinus, kIconRemove, temp ? v.tempDown : v.rhDown);
      iconButton(c, temp ? L::kTempPlus : L::kRhPlus, kIconAdd, temp ? v.tempUp : v.rhUp);
      const Rect val(272, r.y, 136, r.h);
      textCentred(c, kFontLarge, val, (int16_t)(r.y + 52), temp ? v.draftTemp : v.draftRh, pal::kText);
      if (temp) snprintf(buf, sizeof(buf), "%sC", kDeg);
      else snprintf(buf, sizeof(buf), "%%");
      textCentred(c, kFontSmall, val, (int16_t)(r.y + 76), buf, pal::kMuted);
      break;
    }
    case Element::UpdateButton: {
      const Rect b = L::kAllowUpdate;
      if (v.allowSub[0]) {
        c.roundRect(b, 10, pal::kAccent);
        textCentred(c, kFontSmall, b, (int16_t)(b.y + 24), v.allowText, pal::kBg);
        textCentred(c, kFontMedium, b, (int16_t)(b.y + 50), v.allowSub, pal::kBg);
      } else {
        button(c, b, pal::kSurface, v.allowText, pal::kText);
      }
      break;
    }
    case Element::CancelSave:
      button(c, L::kCancel, pal::kSurface, "Cancel", pal::kText);
      button(c, L::kSave, v.draftChanged ? pal::kAccent : pal::kSurface, "Save", v.draftChanged ? pal::kBg : pal::kText);
      break;
    // ---- alerts
    case Element::AlertsTitle:
      c.text(kFontMedium, 16, midBaseline(kFontMedium, r), "Alerts", pal::kText);
      if (v.pages > 1) snprintf(buf, sizeof(buf), "%u active %s page %u of %u", (unsigned)v.alertCount,
                                "\xC2\xB7", (unsigned)(v.page + 1), (unsigned)v.pages);
      else if (v.alertCount) snprintf(buf, sizeof(buf), "%u active", (unsigned)v.alertCount);
      else buf[0] = 0;
      textRight(c, kFontSmall, 464, midBaseline(kFontSmall, r), buf, pal::kMuted);
      break;
    case Element::AlertsBar: {
      const Rect b = L::kBack;
      c.roundRect(b, 10, pal::kSurface);
      const int16_t w = (int16_t)(24 + 6 + Canvas::measure(kFontMedium, "Back"));
      const int16_t x = (int16_t)(b.x + (b.w - w) / 2);
      c.icon(kIconBack, x, (int16_t)(b.y + (b.h - 24) / 2), pal::kText);
      c.text(kFontMedium, (int16_t)(x + 30), midBaseline(kFontMedium, b), "Back", pal::kText);
      if (v.pages > 1) {
        iconButton(c, L::kPageUp, kIconPageUp, v.page > 0);
        iconButton(c, L::kPageDown, kIconPageDown, v.page + 1 < v.pages);
      }
      break;
    }
    default:
      if (e >= Element::Row0 && e <= Element::Row4) paintRow(c, v, (int)e - (int)Element::Row0, r);
      break;   // background strips: the fill above is all
  }
  c.clearClip();
}

// ---------------------------------------------------------------- what changed
namespace {
struct Hash {
  uint32_t h = 2166136261u;
  void byte(uint8_t b) { h = (h ^ b) * 16777619u; }
  void num(uint32_t v) { for (int i = 0; i < 4; i++) byte((uint8_t)(v >> (8 * i))); }
  void str(const char* s) { while (*s) byte((uint8_t)*s++); byte(0); }
};
}  // namespace

uint32_t elementKey(const UiView& v, Element e) {
  Hash h;
  h.num((uint32_t)e + 1);
  switch (e) {
    case Element::Time: h.str(v.timeText); break;
    case Element::Tag: h.num((uint32_t)v.tag); break;
    case Element::Wifi: h.num(v.wifi); break;
    case Element::Temp: h.str(v.tempText); h.num(v.readingsOk); break;
    case Element::Rh: h.str(v.rhText); h.num(v.readingsOk); break;
    case Element::Setpoints: h.str(v.setTempText); h.str(v.setRhText); break;
    case Element::Modes:
      h.num(v.nModes);
      for (int i = 0; i < v.nModes; i++) { h.num((uint32_t)v.modes[i]); h.str(v.modeText[i]); }
      break;
    case Element::Update: h.num(v.updateWindow); h.str(v.updateText); break;
    case Element::AlertLine: h.num(v.alertLine); h.str(v.alertText); h.str(v.moreText); h.num(v.alertRank); break;
    case Element::SettingsButton: h.num(v.settingsEnabled); break;
    case Element::SetTitle: h.num(v.draftChanged); break;
    case Element::TempLabel: case Element::RhLabel: break;     // fixed text
    case Element::TempControl: h.str(v.draftTemp); h.num(v.tempDown); h.num(v.tempUp); break;
    case Element::RhControl: h.str(v.draftRh); h.num(v.rhDown); h.num(v.rhUp); break;
    case Element::UpdateButton: h.str(v.allowText); h.str(v.allowSub); break;
    case Element::CancelSave: h.num(v.draftChanged); break;
    case Element::AlertsTitle: h.num(v.alertCount); h.num(v.page); h.num(v.pages); break;
    case Element::AlertsBar: h.num(v.page); h.num(v.pages); break;
    default:
      if (e >= Element::Row0 && e <= Element::Row4) {
        const int i = (int)e - (int)Element::Row0;
        h.num(v.rows);
        if (i < v.rows) { h.str(v.row[i].title); h.str(v.row[i].when); h.num(v.row[i].rank); }
      }
      break;
  }
  return h.h;
}

void paintScreen(Canvas& c, const UiView& v) {
  for (int i = 0; i < kBgStrips; i++) paintElement(c, v, (Element)i);
  Element list[kElements];
  const int n = elementsOf(v.screen, list, kElements);
  for (int i = 0; i < n; i++) paintElement(c, v, list[i]);
}

// ---------------------------------------------------------------- the painter
void Painter::plan(const UiView& v) {
  if (all_ || v.screen != screen_) {
    all_ = false;
    screen_ = v.screen;
    dirty_ = 0;
    for (int i = 0; i < kBgStrips; i++) dirty_ |= 1ull << i;
    for (int i = 0; i < kElements; i++) keys_[i] = 0;
  }
  Element list[kElements];
  const int n = elementsOf(v.screen, list, kElements);
  for (int i = 0; i < n; i++) {
    const int k = (int)list[i];
    if (elementKey(v, list[i]) != keys_[k]) dirty_ |= 1ull << k;
  }
}

bool Painter::idle(const UiView& v) {
  plan(v);
  return dirty_ == 0;
}

bool Painter::step(Canvas& c, const UiView& v, Rect* painted, Element* which) {
  plan(v);
  if (dirty_ == 0) return false;
  int k = 0;
  while (!(dirty_ & (1ull << k))) k++;          // the background first, then in screen order
  const Element e = (Element)k;
  paintElement(c, v, e);
  dirty_ &= ~(1ull << k);
  if (k >= kBgStrips) keys_[k] = elementKey(v, e);
  if (painted) *painted = elementRect(e);
  if (which) *which = e;
  return true;
}

// ---------------------------------------------------------------- BP2 test 29
int cornerOf(int x, int y) {
  constexpr int kNear = 110;
  const bool left = x < kNear, right = x >= kScreenW - kNear, top = y < kNear, bottom = y >= kScreenH - kNear;
  if (top && left) return 0;
  if (top && right) return 1;
  if (bottom && left) return 2;
  if (bottom && right) return 3;
  return -1;
}

void paintLandscapeCheck(Canvas& c, const char* wayName, uint8_t done) {
  c.clearClip();
  c.fill(Rect(0, 0, kScreenW, kScreenH), pal::kBg);
  c.icon(kIconArrowUp, (int16_t)((kScreenW - kIconArrowUp.w) / 2), 40, pal::kText);
  textCentred(c, kFontMedium, Rect(0, 0, kScreenW, 40), 30, "UP", pal::kText);
  char t[48];
  snprintf(t, sizeof(t), "%s: touch each corner", wayName);
  textCentred(c, kFontSmall, Rect(0, 0, kScreenW, kScreenH), 220, t, pal::kMuted);
  static const char* const names[4] = {"top left", "top right", "bottom left", "bottom right"};
  for (int i = 0; i < 4; i++) {
    const int16_t x = (i & 1) ? (int16_t)(kScreenW - 16 - 56) : 16;
    const int16_t y = (i & 2) ? (int16_t)(kScreenH - 16 - 56) : 16;
    const bool d = done & (1u << i);
    c.roundRect(Rect(x, y, 56, 56), 28, d ? pal::kHumid : pal::kSurface);
    const int16_t tx = (i & 1) ? (int16_t)(x - 8 - Canvas::measure(kFontSmall, names[i])) : (int16_t)(x + 56 + 8);
    c.text(kFontSmall, tx, (int16_t)(y + 34), names[i], d ? pal::kHumid : pal::kMuted);
  }
}

}  // namespace lcdgfx
