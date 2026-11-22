// Suite Control — panel.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/panel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace suite {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHeaderH = 64.0;
constexpr double kStripY = 76.0;
constexpr double kStripW = 120.0;
constexpr double kStripH = 668.0;
constexpr double kStripGap = 6.0;
constexpr double kStripX0 = 12.0;
constexpr double kBusX = 1022.0;
constexpr double kBusW = 246.0;

struct Rgb {
  double r, g, b;
};

void setRgb(cairo_t* cr, Rgb c, double a = 1.0) {
  cairo_set_source_rgba(cr, c.r, c.g, c.b, a);
}

void roundRect(cairo_t* cr, double x, double y, double w, double h, double r) {
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -kPi / 2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, kPi / 2);
  cairo_arc(cr, x + r, y + h - r, r, kPi / 2, kPi);
  cairo_arc(cr, x + r, y + r, r, kPi, 3 * kPi / 2);
  cairo_close_path(cr);
}

double clamp01(double v) { return std::min(std::max(v, 0.0), 1.0); }

const Rgb kText{0.90, 0.91, 0.92};
const Rgb kDim{0.55, 0.57, 0.60};
const Rgb kAccent{0.86, 0.62, 0.28};
const Rgb kEdge{0.24, 0.25, 0.27};

const char* dbText(char* buf, size_t n, double v01, double lo, double hi) {
  std::snprintf(buf, n, "%+.1f", lo + (hi - lo) * clamp01(v01));
  return buf;
}

const char* pctText(char* buf, size_t n, double v01) {
  std::snprintf(buf, n, "%.0f%%", 100.0 * clamp01(v01));
  return buf;
}

} // namespace

Panel::Panel() {
  fallbackFont_ = cairo_toy_font_face_create(
      "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  font_ = fallbackFont_;
}

Panel::~Panel() {
  if (font_ && font_ != fallbackFont_) cairo_font_face_destroy(font_);
  if (fallbackFont_) cairo_font_face_destroy(fallbackFont_);
  if (ftFace_) FT_Done_Face(ftFace_);
  if (ft_) FT_Done_FreeType(ft_);
}

void Panel::loadFont(const std::string& dir) {
  FT_Library lib = nullptr;
  if (FT_Init_FreeType(&lib) != 0) return;
  FT_Face face = nullptr;
  const std::string path = dir + "/Jost-ExtraBold.ttf";
  if (FT_New_Face(lib, path.c_str(), 0, &face) != 0) {
    FT_Done_FreeType(lib);
    return;
  }
  cairo_font_face_t* cf = cairo_ft_font_face_create_for_ft_face(face, 0);
  if (font_ && font_ != fallbackFont_) cairo_font_face_destroy(font_);
  ft_ = lib;
  ftFace_ = face;
  font_ = cf;
}

void Panel::text(cairo_t* cr, const char* s, double x, double y, double size,
                 int align, double r, double g, double b) const {
  cairo_set_font_face(cr, font_);
  cairo_set_font_size(cr, size);
  cairo_text_extents_t ext;
  cairo_text_extents(cr, s, &ext);
  double tx = x;
  if (align == 1) {
    tx -= (ext.width * 0.5 + ext.x_bearing);
  } else if (align == 2) {
    tx -= (ext.width + ext.x_bearing);
  }
  cairo_set_source_rgb(cr, r, g, b);
  cairo_move_to(cr, tx, y);
  cairo_show_text(cr, s);
}

void Panel::refreshStrips() {
  strips_.clear();
  auto* seg = peer_.segment();
  if (!seg) return;
  const int wantBus = lastSerial_ == 0 ? 0 : 0;  // unused; bus filter in layout
  (void)wantBus;
  for (int i = 0; i < peer::kSlots; ++i) {
    if (!peer::Client::alive(seg->slot[i])) continue;
    strips_.push_back({i, seg->slot[i].serial.load(std::memory_order_relaxed)});
  }
}

double Panel::slotValue(const StripRef& s, int plus) const {
  auto* seg = peer_.raw();
  if (!seg || s.slot < 0) return 0.0;
  return seg->slot[s.slot].param[plus].value.load(std::memory_order_relaxed);
}

void Panel::writeSlot(const StripRef& s, int plus, double v) {
  auto* seg = peer_.raw();
  if (!seg || s.slot < 0) return;
  peer::Client::writeRemote(seg->slot[s.slot], plus, clamp01(v));
}

void Panel::tick(const Shared& shared, PanelState& state) {
  (void)shared;
  (void)state;
  peer_.segment();
  refreshStrips();
}

void Panel::layout(const Shared& shared, const PanelState& state) {
  widgets_.clear();
  const int bus = std::clamp(static_cast<int>(shared.get(P_BUS) + 0.5), 0, 8);

  // Header: bus tabs and capture.
  for (int i = 0; i <= 8; ++i) {
    Widget t;
    t.kind = Kind::Tab;
    t.param = P_BUS;
    t.value = static_cast<double>(i);
    t.x = 300.0 + i * 54.0;
    t.y = 18.0;
    t.w = 48.0;
    t.h = 28.0;
    t.label = kBusLabels[i];
    widgets_.push_back(t);
  }
  {
    Widget c;
    c.kind = Kind::Capture;
    c.x = 800.0;
    c.y = 18.0;
    c.w = 132.0;
    c.h = 28.0;
    c.label = "CAPTURE";
    widgets_.push_back(c);
  }

  // Remote strips for the selected bus.
  int shown = 0;
  for (const StripRef& s : strips_) {
    if (shown >= 8) break;
    auto* seg = peer_.raw();
    if (!seg) break;
    const uint32_t slotBus = seg->slot[s.slot].bus.load(std::memory_order_relaxed);
    if (bus != 0 && static_cast<int>(slotBus) != bus) continue;
    const double x = kStripX0 + shown * (kStripW + kStripGap);
    const double y = kStripY;
    {
      Widget w;
      w.kind = Kind::Strip;
      w.slot = s.slot;
      w.value = shown;
      w.x = x;
      w.y = y;
      w.w = kStripW;
      w.h = 40.0;
      widgets_.push_back(w);
    }
    struct R { int plus; double dx, dy, r; const char* label; };
    const R rows[] = {
        {ch::kInput, 34, 150, 20, "IN"},
        {ch::kCompThreshold, 86, 150, 20, "COMP"},
        {ch::kCompMakeup, 34, 250, 20, "MAKEUP"},
        {ch::kOutput, 86, 250, 20, "OUT"},
    };
    for (const R& r : rows) {
      Widget k;
      k.kind = Kind::Remote;
      k.slot = s.slot;
      k.plus = r.plus;
      k.x = x + r.dx;
      k.y = y + r.dy;
      k.r = r.r;
      k.label = r.label;
      widgets_.push_back(k);
    }
    Widget comp;
    comp.kind = Kind::Remote;
    comp.slot = s.slot;
    comp.plus = ch::kCompIn;
    comp.x = x + 18;
    comp.y = y + 318;
    comp.w = 48;
    comp.h = 22;
    comp.label = "COMP";
    widgets_.push_back(comp);
    Widget eq;
    eq.kind = Kind::Remote;
    eq.slot = s.slot;
    eq.plus = ch::kEqIn;
    eq.x = x + 70;
    eq.y = y + 318;
    eq.w = 40;
    eq.h = 22;
    eq.label = "EQ";
    widgets_.push_back(eq);
    ++shown;
  }

  // Mix bus controls.
  auto knob = [&](uint32_t p, double x, double y, double r, const char* label) {
    Widget k;
    k.kind = Kind::Knob;
    k.param = p;
    k.x = x;
    k.y = y;
    k.r = r;
    k.label = label;
    widgets_.push_back(k);
  };
  auto pill = [&](uint32_t p, double x, double y, double w, double h,
                  const char* label) {
    Widget b;
    b.kind = Kind::Pill;
    b.param = p;
    b.x = x;
    b.y = y;
    b.w = w;
    b.h = h;
    b.label = label;
    widgets_.push_back(b);
  };

  pill(P_COMP_IN, kBusX + 14, 96, 62, 22, "COMP");
  knob(P_COMP_THRESHOLD, kBusX + 56, 168, 21, "THRESH");
  knob(P_COMP_RATIO, kBusX + 136, 168, 21, "RATIO");
  knob(P_COMP_ATTACK, kBusX + 56, 254, 21, "ATTACK");
  knob(P_COMP_RELEASE, kBusX + 136, 254, 21, "REL");
  knob(P_COMP_MIX, kBusX + 96, 336, 19, "MIX");

  pill(P_EQ_IN, kBusX + 14, 392, 40, 22, "EQ");
  pill(P_EQ_TYPE, kBusX + 60, 392, 56, 22, "TYPE");
  knob(P_EQ_LF_GAIN, kBusX + 24, 462, 17, "LF");
  knob(P_EQ_LM_GAIN, kBusX + 74, 462, 17, "LMF");
  knob(P_EQ_HM_GAIN, kBusX + 124, 462, 17, "HMF");
  knob(P_EQ_HF_GAIN, kBusX + 174, 462, 17, "HF");

  knob(P_SUM_AMOUNT, kBusX + 44, 560, 19, "SUM");
  knob(P_INPUT, kBusX + 118, 560, 17, "IN");
  knob(P_OUTPUT, kBusX + 180, 560, 19, "OUT");
  pill(P_LIMITER, kBusX + 14, 646, 62, 22, "LIMIT");

  (void)state;
}

bool Panel::pointerDown(Shared& shared, PanelState& state, double x, double y,
                        bool fine) {
  layout(shared, state);
  state.fine = fine;
  for (const Widget& w : widgets_) {
    const bool hit =
        (w.kind == Kind::Knob || w.kind == Kind::Remote)
            ? ((x - w.x) * (x - w.x) + (y - w.y) * (y - w.y) <= w.r * w.r * 1.6)
            : (x >= w.x && x <= w.x + w.w && y >= w.y && y <= w.y + w.h);
    if (!hit) continue;
    switch (w.kind) {
      case Kind::Strip:
        state.selectedStrip = static_cast<int>(w.value);
        return true;
      case Kind::Tab:
        shared.set(P_BUS, w.value);
        return true;
      case Kind::Capture: {
        const int bus = std::clamp(static_cast<int>(shared.get(P_BUS) + 0.5), 0, 8);
        if (bus == 0) return true;
        auto* seg = peer_.raw();
        if (!seg) return true;
        for (int i = 0; i < peer::kSlots; ++i) {
          if (!peer::Client::alive(seg->slot[i])) continue;
          if (seg->slot[i].bus.load(std::memory_order_relaxed) != 0) continue;
          peer::Client::writeRemote(seg->slot[i], ch::kBus, bus);
        }
        return true;
      }
      case Kind::Pill: {
        if (w.param == P_EQ_TYPE) {
          shared.set(P_EQ_TYPE, shared.get(P_EQ_TYPE) > 0.5 ? 0.0 : 1.0);
        } else if (w.param == P_COMP_RATIO) {
          int v = static_cast<int>(shared.get(P_COMP_RATIO) + 0.5);
          shared.set(P_COMP_RATIO, static_cast<double>((v + 1) % 3));
        } else {
          shared.set(w.param, shared.get(w.param) > 0.5 ? 0.0 : 1.0);
        }
        return true;
      }
      case Kind::Knob:
        state.dragKind = 0;
        state.dragParam = static_cast<int>(w.param);
        state.dragY = y;
        state.dragStart = shared.get(w.param);
        return true;
      case Kind::Remote: {
        const StripRef* s = nullptr;
        for (const StripRef& r : strips_) {
          if (r.slot == w.slot) s = &r;
        }
        if (!s) return true;
        state.dragKind = 1;
        state.dragSlot = w.slot;
        state.dragPlus = w.plus;
        state.dragY = y;
        state.dragStart = slotValue(*s, w.plus);
        return true;
      }
      default:
        return false;
    }
  }
  return false;
}

bool Panel::pointerMove(Shared& shared, PanelState& state, double x, double y,
                        bool fine) {
  (void)x;
  if (state.dragKind < 0) {
    // Change the pointer while over a knob is handled by the host's cursor.
    return false;
  }
  const double span = fine || state.fine ? 640.0 : 160.0;
  const double delta = (state.dragY - y) / span;
  const double v = clamp01(state.dragStart + delta);
  if (state.dragKind == 0) {
    shared.set(static_cast<uint32_t>(state.dragParam), v);
  } else {
    for (const StripRef& r : strips_) {
      if (r.slot == state.dragSlot) writeSlot(r, state.dragPlus, v);
    }
  }
  return true;
}

void Panel::pointerUp() {}

void Panel::knob(cairo_t* cr, const Widget& w, double value,
                 const char* readout, bool active) const {
  const double a0 = 135.0 * kPi / 180.0;
  const double a1 = 405.0 * kPi / 180.0;
  const double a = a0 + (a1 - a0) * clamp01(value);
  cairo_set_line_width(cr, 4.0);
  setRgb(cr, {0.10, 0.11, 0.12});
  cairo_arc(cr, w.x, w.y, w.r, a0, a1);
  cairo_stroke(cr);
  setRgb(cr, active ? kAccent : Rgb{0.52, 0.55, 0.60});
  cairo_arc(cr, w.x, w.y, w.r, a0, a);
  cairo_stroke(cr);
  cairo_set_line_width(cr, 2.0);
  setRgb(cr, {0.92, 0.93, 0.94});
  cairo_move_to(cr, w.x, w.y);
  cairo_line_to(cr, w.x + std::cos(a) * (w.r - 1.5),
                w.y + std::sin(a) * (w.r - 1.5));
  cairo_stroke(cr);
  text(cr, w.label, w.x, w.y + w.r + 13.0, 7.5, 1, kDim.r, kDim.g, kDim.b);
  if (readout && readout[0]) {
    text(cr, readout, w.x, w.y - w.r - 6.0, 7.0, 1, 0.74, 0.76, 0.78);
  }
}

void Panel::pill(cairo_t* cr, const Widget& w, const char* label,
                 bool on) const {
  roundRect(cr, w.x, w.y, w.w, w.h, 4);
  setRgb(cr, on ? kAccent : Rgb{0.16, 0.17, 0.19});
  cairo_fill(cr);
  setRgb(cr, kEdge, 0.8);
  cairo_set_line_width(cr, 1.0);
  roundRect(cr, w.x + 0.5, w.y + 0.5, w.w - 1, w.h - 1, 4);
  cairo_stroke(cr);
  text(cr, label, w.x + w.w * 0.5, w.y + w.h * 0.5 + 3.5, 8.0, 1,
       on ? 0.10 : 0.72, on ? 0.08 : 0.74, on ? 0.05 : 0.76);
}

void Panel::meter(cairo_t* cr, double x, double y, double w, double h, float dbL,
                  float dbR) const {
  roundRect(cr, x, y, w, h, 3);
  setRgb(cr, {0.07, 0.08, 0.09});
  cairo_fill(cr);
  auto bar = [&](double bx, float db) {
    const double t = clamp01((db + 54.0) / 60.0);
    const double bh = (h - 4.0) * t;
    setRgb(cr, t > 0.92 ? Rgb{0.92, 0.35, 0.25}
                        : (t > 0.8 ? Rgb{0.92, 0.78, 0.30}
                                   : Rgb{0.38, 0.80, 0.50}));
    cairo_rectangle(cr, bx, y + h - 2.0 - bh, 4.0, bh);
    cairo_fill(cr);
  };
  bar(x + 4.0, dbL);
  bar(x + 10.0, dbR);
}

void Panel::grMeter(cairo_t* cr, double x, double y, double w, double h,
                    float grDb) const {
  roundRect(cr, x, y, w, h, 3);
  setRgb(cr, {0.07, 0.08, 0.09});
  cairo_fill(cr);
  const double t = clamp01(grDb / 24.0);
  setRgb(cr, {0.95, 0.55, 0.20});
  cairo_rectangle(cr, x + 2.0, y + 2.0, w - 4.0, (h - 4.0) * t);
  cairo_fill(cr);
}

void Panel::draw(cairo_t* cr, const Shared& shared, const PanelState& state) {
  layout(shared, state);
  cairo_set_source_rgb(cr, 0.055, 0.060, 0.070);
  cairo_paint(cr);

  // Header.
  roundRect(cr, 0, 0, kPanelWidth, kHeaderH, 0);
  setRgb(cr, {0.10, 0.11, 0.13});
  cairo_fill(cr);
  text(cr, "SUITE CONTROL", 24, 40, 20.0, 0, kText.r, kText.g, kText.b);

  auto* seg = peer_.raw();
  if (!seg) {
    text(cr, "PEER BUS UNAVAILABLE", kPanelWidth * 0.5, 420, 16, 1, 0.9, 0.45,
         0.35);
    text(cr, "Check that Suite Channel Plus is loaded in this process.",
         kPanelWidth * 0.5, 448, 9, 1, kDim.r, kDim.g, kDim.b);
  }

  const int bus = std::clamp(static_cast<int>(shared.get(P_BUS) + 0.5), 0, 8);

  // Plates first so the controls sit on top.
  int shown = 0;
  auto* seg2 = peer_.raw();
  for (const StripRef& sref : strips_) {
    if (shown >= 8 || !seg2) break;
    const uint32_t slotBus =
        seg2->slot[sref.slot].bus.load(std::memory_order_relaxed);
    if (bus != 0 && static_cast<int>(slotBus) != bus) continue;
    const double x = kStripX0 + shown * (kStripW + kStripGap);
    roundRect(cr, x, kStripY, kStripW, kStripH, 8);
    setRgb(cr, {0.085, 0.095, 0.105});
    cairo_fill(cr);
    setRgb(cr, kEdge, 0.7);
    cairo_set_line_width(cr, 1.0);
    roundRect(cr, x + 0.5, kStripY + 0.5, kStripW - 1, kStripH - 1, 8);
    cairo_stroke(cr);
    ++shown;
  }

  roundRect(cr, kBusX, kStripY, kBusW, kStripH, 8);
  setRgb(cr, {0.10, 0.11, 0.12});
  cairo_fill(cr);
  setRgb(cr, kEdge, 0.8);
  cairo_set_line_width(cr, 1.0);
  roundRect(cr, kBusX + 0.5, kStripY + 0.5, kBusW - 1, kStripH - 1, 8);
  cairo_stroke(cr);
  text(cr, "MIX BUS", kBusX + kBusW * 0.5, kStripY + 24, 11, 1, 0.93, 0.90,
       0.84);

  // Controls.
  for (const Widget& w : widgets_) {
    switch (w.kind) {
      case Kind::Tab: {
        const bool on = std::fabs(shared.get(P_BUS) - w.value) < 0.5;
        Widget copy = w;
        pill(cr, copy, w.label, on);
        break;
      }
      case Kind::Capture: {
        Widget copy = w;
        pill(cr, copy, "CAPTURE", false);
        break;
      }
      case Kind::Strip: {
        const bool sel = state.selectedStrip == static_cast<int>(w.value);
        roundRect(cr, w.x, w.y - 4, w.w, 28, 4);
        setRgb(cr, sel ? Rgb{0.20, 0.17, 0.13} : Rgb{0.09, 0.10, 0.11});
        cairo_fill(cr);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "CH %d", static_cast<int>(w.value) + 1);
        text(cr, buf, w.x + w.w * 0.5, w.y + 14, 10, 1, sel ? 0.95 : 0.72,
             sel ? 0.80 : 0.74, sel ? 0.55 : 0.76);
        break;
      }
      case Kind::Knob: {
        char buf[24];
        const char* ro = nullptr;
        if (w.param == P_COMP_THRESHOLD) {
          ro = dbText(buf, sizeof(buf), shared.get(w.param), 10.0, -20.0);
        } else if (w.param == P_COMP_ATTACK) {
          ro = dbText(buf, sizeof(buf), shared.get(w.param), 0.1, 30.0);
        } else if (w.param == P_COMP_RELEASE) {
          ro = dbText(buf, sizeof(buf), shared.get(w.param), 100.0, 1200.0);
        } else if (w.param == P_COMP_MIX || w.param == P_SUM_AMOUNT) {
          ro = pctText(buf, sizeof(buf), shared.get(w.param));
        } else if (w.param == P_INPUT || w.param == P_OUTPUT) {
          ro = dbText(buf, sizeof(buf), shared.get(w.param), -18.0, 18.0);
        } else if (w.param == P_COMP_RATIO) {
          static const char* kR[] = {"2:1", "4:1", "10:1"};
          ro = kR[std::clamp(static_cast<int>(shared.get(w.param) + 0.5), 0, 2)];
        } else {
          ro = dbText(buf, sizeof(buf), shared.get(w.param), -15.0, 15.0);
        }
        knob(cr, w, shared.get(w.param), ro, false);
        break;
      }
      case Kind::Remote: {
        const StripRef* sref = nullptr;
        for (const StripRef& r : strips_) {
          if (r.slot == w.slot) sref = &r;
        }
        if (!sref) break;
        const double v = slotValue(*sref, w.plus);
        if (w.plus == ch::kCompIn || w.plus == ch::kEqIn) {
          Widget copy = w;
          pill(cr, copy, w.label, v > 0.5);
        } else {
          char buf[24];
          const char* ro = nullptr;
          if (w.plus == ch::kCompThreshold) {
            ro = dbText(buf, sizeof(buf), v, 10.0, -20.0);
          } else if (w.plus == ch::kCompMakeup) {
            ro = dbText(buf, sizeof(buf), v, -9.0, 9.0);
          } else {
            ro = dbText(buf, sizeof(buf), v, -18.0, 18.0);
          }
          knob(cr, w, v, ro, false);
        }
        break;
      }
      case Kind::Pill: {
        bool on = shared.get(w.param) > 0.5;
        const char* label = w.label;
        if (w.param == P_EQ_TYPE) {
          label = on ? "BLACK" : "BROWN";
          on = true;
        }
        Widget copy = w;
        pill(cr, copy, label, on);
        break;
      }
    }
  }

  // Meters on top of the plates.
  shown = 0;
  for (const StripRef& sref : strips_) {
    if (shown >= 8 || !seg2) break;
    const uint32_t slotBus =
        seg2->slot[sref.slot].bus.load(std::memory_order_relaxed);
    if (bus != 0 && static_cast<int>(slotBus) != bus) continue;
    const double x = kStripX0 + shown * (kStripW + kStripGap);
    const float inL = seg2->slot[sref.slot].inPeakL.load(std::memory_order_relaxed);
    const float inR = seg2->slot[sref.slot].inPeakR.load(std::memory_order_relaxed);
    const float outL =
        seg2->slot[sref.slot].outPeakL.load(std::memory_order_relaxed);
    const float outR =
        seg2->slot[sref.slot].outPeakR.load(std::memory_order_relaxed);
    const float gr = seg2->slot[sref.slot].grDb.load(std::memory_order_relaxed);
    text(cr, "IN", x + 10, kStripY + 78, 7, 0, kDim.r, kDim.g, kDim.b);
    meter(cr, x + 10, kStripY + 52, 100, 10, inL, inR);
    meter(cr, x + 8, kStripY + 384, 44, 190, inL, inR);
    meter(cr, x + 68, kStripY + 384, 44, 190, outL, outR);
    text(cr, "GR", x + 8, kStripY + 594, 7, 0, kDim.r, kDim.g, kDim.b);
    grMeter(cr, x + 8, kStripY + 602, 104, 8, gr);
    ++shown;
  }

  grMeter(cr, kBusX + 196, 96, 10, 226,
          shared.grDb.load(std::memory_order_relaxed));
  meter(cr, kBusX + 210, 96, 24, 226, shared.inPeakLDb.load(),
        shared.outPeakLDb.load());
  text(cr, "GR", kBusX + 201, 336, 7, 1, kDim.r, kDim.g, kDim.b);
  text(cr, "OUT", kBusX + 222, 336, 7, 1, kDim.r, kDim.g, kDim.b);
  text(cr, "BUS", kBusX + kBusW * 0.5, kStripY + kStripH - 10, 7.5, 1,
       kDim.r, kDim.g, kDim.b);
}

} // namespace suite
