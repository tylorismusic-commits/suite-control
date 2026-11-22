// Suite Control — panel.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Same visual language as Suite Channel: faceplate gradient, section plates,
// console-coloured knob caps (HF red, HMF green, LMF blue, LF amber), printed
// legends and segmented meters.
#include "ui/panel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace suite {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHeaderH = 64.0;
constexpr double kPlateY = 72.0;
constexpr double kPlateH = 680.0;
constexpr double kStripX0 = 16.0;
constexpr double kStripW = 120.0;
constexpr double kStripGap = 5.0;
constexpr double kBusX = 1021.0;
constexpr double kBusW = 251.0;

struct Rgb {
  double r, g, b;
};

void setRgb(cairo_t* cr, Rgb c, double a = 1.0) {
  cairo_set_source_rgba(cr, c.r, c.g, c.b, a);
}

Rgb mix(Rgb a, Rgb b, double t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
          a.b + (b.b - a.b) * t};
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

double knobAngle(double f) { return (135.0 + 270.0 * f) * kPi / 180.0; }

const Rgb kBodyTop{0.20, 0.22, 0.26};
const Rgb kBodyBottom{0.10, 0.11, 0.14};
const Rgb kLegend{0.84, 0.87, 0.91};
const Rgb kLegendDim{0.48, 0.52, 0.58};
const Rgb kHeaderBg{0.09, 0.10, 0.12};
const Rgb kAccent{0.30, 0.76, 0.66};   // Console teal
const Rgb kBtnOff{0.15, 0.17, 0.20};
const Rgb kBtnText{0.72, 0.76, 0.80};
const Rgb kScrew{0.38, 0.40, 0.44};

struct KnobPalette {
  Rgb cap, edge, indicator;
};

// 0 blue, 1 amber, 2 red, 3 green, 4 copper, 5 grey
KnobPalette paletteFor(int group) {
  switch (group) {
    case 1:
      return {{0.64, 0.42, 0.16}, {0.28, 0.16, 0.05}, {1.0, 0.92, 0.78}};
    case 2:
      return {{0.72, 0.20, 0.16}, {0.34, 0.06, 0.05}, {1.0, 0.90, 0.84}};
    case 3:
      return {{0.24, 0.58, 0.28}, {0.06, 0.24, 0.09}, {0.90, 1.0, 0.88}};
    case 4:
      return {{0.58, 0.36, 0.18}, {0.25, 0.14, 0.05}, {1.0, 0.93, 0.80}};
    case 5:
      return {{0.30, 0.32, 0.35}, {0.12, 0.13, 0.15}, {0.94, 0.95, 0.96}};
    default:
      return {{0.22, 0.42, 0.72}, {0.06, 0.16, 0.34}, {0.86, 0.93, 1.0}};
  }
}

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

void Panel::tick(const Shared& shared, PanelState& state) {
  (void)state;
  bus_ = std::clamp(static_cast<int>(shared.get(P_BUS) + 0.5), 0, 8);
  hub_.refresh();
}

void Panel::plate(cairo_t* cr, double x, double y, double w, double h,
                  const char* title) const {
  roundRect(cr, x, y, w, h, 5);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.16);
  cairo_fill(cr);
  cairo_set_line_width(cr, 1.0);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.07);
  roundRect(cr, x + 0.5, y + 0.5, w - 1, h - 1, 5);
  cairo_stroke(cr);
  if (title) {
    text(cr, title, x + 10, y + 18, 10.0, 0, kLegend.r, kLegend.g, kLegend.b);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.10);
    cairo_move_to(cr, x + 10, y + 26);
    cairo_line_to(cr, x + w - 10, y + 26);
    cairo_stroke(cr);
  }
}

void Panel::layout(const Shared& shared, const PanelState& state) {
  (void)state;
  widgets_.clear();

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

  auto knob = [&](uint32_t p, double x, double y, double r, const char* label,
                  int palette) {
    Widget k;
    k.kind = Kind::Knob;
    k.param = p;
    k.x = x;
    k.y = y;
    k.r = r;
    k.label = label;
    k.palette = palette;
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

  // Remote strips.
  int shown = 0;
  for (int i = 0; i < hub_.size() && shown < 8; ++i) {
    if (bus_ != 0 && static_cast<int>(hub_.bus(i)) != bus_) continue;
    const double x = kStripX0 + shown * (kStripW + kStripGap);
    {
      Widget w;
      w.kind = Kind::Strip;
      w.index = i;
      w.value = shown;
      w.x = x;
      w.y = kPlateY + 8;
      w.w = kStripW;
      w.h = 26.0;
      widgets_.push_back(w);
    }
    struct R {
      int plus;
      double dx, dy, r;
      const char* label;
      int palette;
    };
    const R rows[] = {
        {ch::kInput, 34, 108, 21, "INPUT", 0},
        {ch::kCompThreshold, 86, 108, 21, "COMP", 0},
        {ch::kCompMakeup, 34, 216, 21, "MAKEUP", 0},
        {ch::kOutput, 86, 216, 21, "OUTPUT", 0},
    };
    for (const R& r : rows) {
      Widget k;
      k.kind = Kind::Remote;
      k.index = i;
      k.plus = r.plus;
      k.x = x + r.dx;
      k.y = kPlateY + r.dy;
      k.r = r.r;
      k.label = r.label;
      k.palette = r.palette;
      widgets_.push_back(k);
    }
    Widget comp;
    comp.kind = Kind::Remote;
    comp.index = i;
    comp.plus = ch::kCompIn;
    comp.x = x + 16;
    comp.y = kPlateY + 300;
    comp.w = 48;
    comp.h = 22;
    comp.label = "COMP";
    widgets_.push_back(comp);
    Widget eq;
    eq.kind = Kind::Remote;
    eq.index = i;
    eq.plus = ch::kEqIn;
    eq.x = x + 68;
    eq.y = kPlateY + 300;
    eq.w = 38;
    eq.h = 22;
    eq.label = "EQ";
    widgets_.push_back(eq);
    ++shown;
  }

  // Mix bus.
  const double bx = kBusX + 8;
  pill(P_COMP_IN, bx, 138, 54, 20, "COMP");
  knob(P_COMP_THRESHOLD, bx + 30, 218, 19, "THRESH", 0);
  knob(P_COMP_RATIO, bx + 96, 218, 19, "RATIO", 0);
  knob(P_COMP_MIX, bx + 162, 218, 17, "MIX", 0);
  knob(P_COMP_ATTACK, bx + 30, 316, 19, "ATTACK", 0);
  knob(P_COMP_RELEASE, bx + 96, 316, 19, "RELEASE", 0);

  pill(P_EQ_IN, bx, 386, 40, 20, "EQ");
  pill(P_EQ_TYPE, bx + 48, 386, 60, 20, "TYPE");
  knob(P_EQ_LF_GAIN, bx + 20, 466, 19, "LF", 1);
  knob(P_EQ_LM_GAIN, bx + 72, 466, 19, "LMF", 0);
  knob(P_EQ_HM_GAIN, bx + 124, 466, 19, "HMF", 3);
  knob(P_EQ_HF_GAIN, bx + 176, 466, 19, "HF", 2);

  knob(P_SUM_AMOUNT, bx + 36, 600, 20, "SUM", 4);

  pill(P_LIMITER, bx + 8, 676, 60, 20, "LIMIT");
  knob(P_INPUT, bx + 110, 700, 17, "IN", 0);
  knob(P_OUTPUT, bx + 172, 700, 19, "OUT", 0);
}

bool Panel::pointerDown(Shared& shared, PanelState& state, double x, double y,
                        bool fine) {
  layout(shared, state);
  state.fine = fine;
  for (const Widget& w : widgets_) {
    const bool hit =
        (w.kind == Kind::Knob || w.kind == Kind::Remote)
            ? ((x - w.x) * (x - w.x) + (y - w.y) * (y - w.y) <=
               w.r * w.r * 1.6)
            : (x >= w.x && x <= w.x + w.w && y >= w.y && y <= w.y + w.h);
    if (!hit) continue;
    switch (w.kind) {
      case Kind::Strip:
        state.selectedStrip = static_cast<int>(w.value);
        return true;
      case Kind::Tab:
        shared.set(P_BUS, w.value);
        bus_ = static_cast<int>(w.value + 0.5);
        return true;
      case Kind::Capture: {
        if (bus_ <= 0) return true;
        hub_.refresh();
        for (int i = 0; i < hub_.size(); ++i) {
          if (hub_.bus(i) == 0) hub_.write(i, ch::kBus, bus_);
        }
        return true;
      }
      case Kind::Pill:
        if (w.param == P_EQ_TYPE) {
          shared.set(P_EQ_TYPE, shared.get(P_EQ_TYPE) > 0.5 ? 0.0 : 1.0);
        } else {
          shared.set(w.param, shared.get(w.param) > 0.5 ? 0.0 : 1.0);
        }
        return true;
      case Kind::Knob:
        dragging_ = true;
        dragKind_ = 0;
        dragParam_ = static_cast<int>(w.param);
        dragY_ = y;
        dragStart_ = shared.get(w.param);
        return true;
      case Kind::Remote:
        dragging_ = true;
        dragKind_ = 1;
        dragIndex_ = w.index;
        dragPlus_ = w.plus;
        dragY_ = y;
        dragStart_ = hub_.value(w.index, w.plus);
        return true;
    }
  }
  return false;
}

bool Panel::pointerMove(Shared& shared, PanelState& state, double x, double y,
                        bool fine) {
  (void)x;
  (void)state;
  if (!dragging_) return false;
  const double span = (fine || state.fine) ? 640.0 : 190.0;
  const double delta = (dragY_ - y) / span;
  const double v = clamp01(dragStart_ + delta);
  if (dragKind_ == 0) {
    shared.set(static_cast<uint32_t>(dragParam_), v);
  } else if (dragKind_ == 1) {
    hub_.write(dragIndex_, dragPlus_, v);
  }
  return true;
}

void Panel::pointerUp() {
  dragging_ = false;
  dragKind_ = -1;
  dragParam_ = -1;
  dragIndex_ = -1;
  dragPlus_ = -1;
}

void Panel::knob(cairo_t* cr, const Widget& w, double value,
                 const char* readout, int palette) const {
  const KnobPalette pal = paletteFor(palette);
  const double cx = w.x;
  const double cy = w.y;
  const double r = w.r;

  setRgb(cr, kLegendDim, 0.55);
  cairo_set_line_width(cr, 1.0);
  for (int i = 0; i <= 10; ++i) {
    const double a = knobAngle(i / 10.0);
    cairo_move_to(cr, cx + std::cos(a) * r * 1.10,
                  cy + std::sin(a) * r * 1.10);
    cairo_line_to(cr, cx + std::cos(a) * r * 1.19,
                  cy + std::sin(a) * r * 1.19);
    cairo_stroke(cr);
  }

  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.18);
  cairo_arc(cr, cx + 0.5, cy + 1.5, r * 1.03, 0, 2 * kPi);
  cairo_fill(cr);

  cairo_pattern_t* body = cairo_pattern_create_linear(0, cy - r, 0, cy + r);
  const Rgb top = mix({0.10, 0.12, 0.14}, {1, 1, 1}, 0.16);
  const Rgb bot = mix({0.10, 0.12, 0.14}, {0, 0, 0}, 0.30);
  cairo_pattern_add_color_stop_rgb(body, 0.0, top.r, top.g, top.b);
  cairo_pattern_add_color_stop_rgb(body, 1.0, bot.r, bot.g, bot.b);
  cairo_arc(cr, cx, cy, r, 0, 2 * kPi);
  cairo_set_source(cr, body);
  cairo_fill(cr);
  cairo_pattern_destroy(body);
  cairo_set_line_width(cr, 1.0);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.45);
  cairo_arc(cr, cx, cy, r - 0.5, 0, 2 * kPi);
  cairo_stroke(cr);

  cairo_pattern_t* cap = cairo_pattern_create_radial(
      cx - r * 0.35, cy - r * 0.40, r * 0.05, cx, cy, r * 0.98);
  const Rgb hi = mix(pal.cap, {1, 1, 1}, 0.24);
  cairo_pattern_add_color_stop_rgb(cap, 0.0, hi.r, hi.g, hi.b);
  cairo_pattern_add_color_stop_rgb(cap, 0.75, pal.cap.r, pal.cap.g, pal.cap.b);
  cairo_pattern_add_color_stop_rgb(cap, 1.0, pal.edge.r, pal.edge.g,
                                   pal.edge.b);
  cairo_arc(cr, cx, cy, r * 0.82, 0, 2 * kPi);
  cairo_set_source(cr, cap);
  cairo_fill(cr);
  cairo_pattern_destroy(cap);

  const double a = knobAngle(clamp01(value));
  cairo_set_line_width(cr, 2.0);
  setRgb(cr, pal.indicator);
  cairo_move_to(cr, cx + std::cos(a) * r * 0.26,
                cy + std::sin(a) * r * 0.26);
  cairo_line_to(cr, cx + std::cos(a) * r * 0.78,
                cy + std::sin(a) * r * 0.78);
  cairo_stroke(cr);

  if (readout && readout[0]) {
    text(cr, readout, cx, cy - r * 1.32 - 5.0, 8.5, 1, kLegend.r, kLegend.g,
         kLegend.b);
  }
  text(cr, w.label, cx, cy + r * 1.32 + 12.0, 9.0, 1, kLegend.r, kLegend.g,
       kLegend.b);
}

void Panel::pill(cairo_t* cr, const Widget& w, const char* label,
                 bool on) const {
  roundRect(cr, w.x, w.y, w.w, w.h, 5);
  setRgb(cr, on ? kAccent : kBtnOff);
  cairo_fill(cr);
  cairo_set_line_width(cr, 1.0);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.35);
  roundRect(cr, w.x + 0.5, w.y + 0.5, w.w - 1.0, w.h - 1.0, 5);
  cairo_stroke(cr);
  const Rgb tc = on ? Rgb{1, 1, 1} : kBtnText;
  text(cr, label, w.x + w.w * 0.5, w.y + w.h * 0.5 + 3.5, 8.5, 1, tc.r, tc.g,
       tc.b);
}

void Panel::meter(cairo_t* cr, double x, double y, double w, double h,
                  float dbL, float dbR) const {
  roundRect(cr, x, y, w, h, 3);
  setRgb(cr, {0.06, 0.07, 0.08});
  cairo_fill(cr);
  cairo_set_line_width(cr, 1.0);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.06);
  roundRect(cr, x + 0.5, y + 0.5, w - 1, h - 1, 3);
  cairo_stroke(cr);
  auto bar = [&](double bx, float db) {
    const double t = clamp01((db + 54.0) / 60.0);
    const double bh = (h - 4.0) * t;
    const double by = y + h - 2.0 - bh;
    const Rgb c = t > 0.9 ? Rgb{0.86, 0.28, 0.20}
                          : (t > 0.74 ? Rgb{0.88, 0.72, 0.26}
                                      : Rgb{0.42, 0.78, 0.46});
    setRgb(cr, c);
    cairo_rectangle(cr, bx, by, 3.5, bh);
    cairo_fill(cr);
  };
  bar(x + 4.0, dbL);
  bar(x + 9.0, dbR);
}

void Panel::grMeter(cairo_t* cr, double x, double y, double w, double h,
                    float grDb) const {
  roundRect(cr, x, y, w, h, 3);
  setRgb(cr, {0.06, 0.07, 0.08});
  cairo_fill(cr);
  const double t = clamp01(grDb / 24.0);
  setRgb(cr, {0.95, 0.55, 0.20});
  cairo_rectangle(cr, x + 2.0, y + 2.0, w - 4.0, (h - 4.0) * t);
  cairo_fill(cr);
}

void Panel::draw(cairo_t* cr, const Shared& shared, const PanelState& state) {
  layout(shared, state);
  cairo_set_source_rgb(cr, 0.045, 0.050, 0.060);
  cairo_paint(cr);

  // Header.
  cairo_rectangle(cr, 0, 0, kPanelWidth, kHeaderH);
  setRgb(cr, kHeaderBg);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, 0, 0.5);
  cairo_line_to(cr, kPanelWidth, 0.5);
  cairo_stroke(cr);
  text(cr, "SUITE CONTROL", 24, 40, 22.0, 0, kLegend.r, kLegend.g, kLegend.b);

  // Faceplate.
  cairo_pattern_t* face = cairo_pattern_create_linear(0, kPlateY, 0,
                                                      kPlateY + kPlateH);
  cairo_pattern_add_color_stop_rgb(face, 0.0, kBodyTop.r, kBodyTop.g,
                                   kBodyTop.b);
  cairo_pattern_add_color_stop_rgb(face, 1.0, kBodyBottom.r, kBodyBottom.g,
                                   kBodyBottom.b);
  roundRect(cr, 8, kPlateY, kPanelWidth - 16, kPlateH, 6);
  cairo_set_source(cr, face);
  cairo_fill(cr);
  cairo_pattern_destroy(face);
  setRgb(cr, {1, 1, 1}, 0.12);
  cairo_set_line_width(cr, 1.0);
  roundRect(cr, 8.5, kPlateY + 0.5, kPanelWidth - 17, kPlateH - 1, 6);
  cairo_stroke(cr);
  for (double sx : {24.0, kPanelWidth - 24.0}) {
    for (double sy : {kPlateY + 16.0, kPlateY + kPlateH - 16.0}) {
      cairo_arc(cr, sx, sy, 4.0, 0, 2 * kPi);
      setRgb(cr, kScrew, 0.9);
      cairo_fill(cr);
    }
  }

  const int shownCount = [&] {
    int n = 0;
    for (int i = 0; i < hub_.size() && n < 8; ++i) {
      if (bus_ != 0 && static_cast<int>(hub_.bus(i)) != bus_) continue;
      ++n;
    }
    return n;
  }();

  char status[96];
  if (hub_.size() == 0) {
    std::snprintf(status, sizeof(status), "NO SUITE CHANNEL PLUS VISIBLE");
  } else {
    std::snprintf(status, sizeof(status), "%d OF %d CHANNELS ON BUS %s",
                  shownCount, hub_.size(),
                  kBusLabels[std::clamp(bus_, 0, 8)]);
  }
  text(cr, status, kPanelWidth - 24, 40, 8.5, 2, kLegendDim.r, kLegendDim.g,
       kLegendDim.b);

  if (shownCount == 0) {
    char line1[96];
    std::snprintf(line1, sizeof(line1), "NO SUITE CHANNEL PLUS ON BUS %s",
                  kBusLabels[std::clamp(bus_, 0, 8)]);
    text(cr, line1, kStripX0 + 500, 360, 14, 1, kLegend.r, kLegend.g,
         kLegend.b);
    text(cr, "Set its BUS knob to this bus, or press CAPTURE to assign every",
         kStripX0 + 500, 386, 9, 1, kLegendDim.r, kLegendDim.g, kLegendDim.b);
    text(cr, "unassigned Suite Channel Plus instance to it.", kStripX0 + 500,
         402, 9, 1, kLegendDim.r, kLegendDim.g, kLegendDim.b);
  }

  // Strip plates.
  int shown = 0;
  for (int i = 0; i < hub_.size() && shown < 8; ++i) {
    if (bus_ != 0 && static_cast<int>(hub_.bus(i)) != bus_) continue;
    const double x = kStripX0 + shown * (kStripW + kStripGap);
    roundRect(cr, x, kPlateY + 8, kStripW, kPlateH - 16, 6);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.14);
    cairo_fill(cr);
    setRgb(cr, {1, 1, 1}, 0.06);
    cairo_set_line_width(cr, 1.0);
    roundRect(cr, x + 0.5, kPlateY + 8.5, kStripW - 1, kPlateH - 17, 6);
    cairo_stroke(cr);
    ++shown;
  }

  // Bus plate.
  plate(cr, kBusX, kPlateY + 8, kBusW, kPlateH - 16, nullptr);
  text(cr, "MIX BUS", kBusX + kBusW * 0.5, kPlateY + 34, 12, 1, kLegend.r,
       kLegend.g, kLegend.b);
  text(cr, "COMPRESSOR", kBusX + 18, kPlateY + 58, 8.5, 0, kLegendDim.r,
       kLegendDim.g, kLegendDim.b);
  text(cr, "EQUALISER", kBusX + 18, kPlateY + 304, 8.5, 0, kLegendDim.r,
       kLegendDim.g, kLegendDim.b);
  text(cr, "SUMMING", kBusX + 18, kPlateY + 474, 8.5, 0, kLegendDim.r,
       kLegendDim.g, kLegendDim.b);
  text(cr, "OUTPUT", kBusX + 18, kPlateY + 594, 8.5, 0, kLegendDim.r,
       kLegendDim.g, kLegendDim.b);

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
        roundRect(cr, w.x, w.y, w.w, w.h, 4);
        setRgb(cr, sel ? Rgb{0.24, 0.21, 0.16} : Rgb{0.13, 0.15, 0.18});
        cairo_fill(cr);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "CH %d", static_cast<int>(w.value) + 1);
        text(cr, buf, w.x + w.w * 0.5, w.y + 17, 10.5, 1,
             sel ? 0.96 : kLegend.r, sel ? 0.86 : kLegend.g,
             sel ? 0.62 : kLegend.b);
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
        knob(cr, w, shared.get(w.param), ro, w.palette);
        break;
      }
      case Kind::Remote: {
        const double v = hub_.value(w.index, w.plus);
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
          knob(cr, w, v, ro, w.palette);
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

  // Meters.
  shown = 0;
  for (int i = 0; i < hub_.size() && shown < 8; ++i) {
    if (bus_ != 0 && static_cast<int>(hub_.bus(i)) != bus_) continue;
    const double x = kStripX0 + shown * (kStripW + kStripGap);
    float inL = 0, inR = 0, outL = 0, outR = 0, gr = 0;
    hub_.meter(i, &inL, &inR, &outL, &outR, &gr);
    meter(cr, x + 8, kPlateY + 356, 44, 190, inL, inR);
    meter(cr, x + 68, kPlateY + 356, 44, 190, outL, outR);
    text(cr, "IN", x + 30, kPlateY + 558, 7.5, 1, kLegendDim.r, kLegendDim.g,
         kLegendDim.b);
    text(cr, "OUT", x + 90, kPlateY + 558, 7.5, 1, kLegendDim.r, kLegendDim.g,
         kLegendDim.b);
    grMeter(cr, x + 8, kPlateY + 572, 104, 8, gr);
    text(cr, "GR", x + 8, kPlateY + 596, 7.0, 0, kLegendDim.r, kLegendDim.g,
         kLegendDim.b);
    ++shown;
  }

  // Bus GR and output meters.
  grMeter(cr, kBusX + 198, kPlateY + 60, 9, 168,
          shared.grDb.load(std::memory_order_relaxed));
  meter(cr, kBusX + 212, kPlateY + 60, 22, 168,
        shared.inPeakLDb.load(std::memory_order_relaxed),
        shared.outPeakLDb.load(std::memory_order_relaxed));
  text(cr, "GR", kBusX + 202, kPlateY + 240, 7, 1, kLegendDim.r,
       kLegendDim.g, kLegendDim.b);
  text(cr, "OUT", kBusX + 223, kPlateY + 240, 7, 1, kLegendDim.r,
       kLegendDim.g, kLegendDim.b);
}

} // namespace suite
