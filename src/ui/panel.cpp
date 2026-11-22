// Suite Control — panel.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/panel.hpp"

#include <algorithm>
#include <chrono>
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
constexpr double kStripTop = 110.0;
constexpr double kStripH = 604.0;
constexpr double kBusX = 1021.0;
constexpr double kBusW = 251.0;
constexpr int kVisible = 8;

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
const Rgb kAccent{0.30, 0.76, 0.66};
const Rgb kBtnOff{0.15, 0.17, 0.20};
const Rgb kBtnText{0.72, 0.76, 0.80};
const Rgb kScrew{0.38, 0.40, 0.44};

struct KnobPalette {
  Rgb cap, edge, indicator;
};

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

struct DetailEntry {
  int plus;
  const char* label;
  int palette;
  int fmt;
};

const DetailEntry kDetail[] = {
    {0, "POWER", 0, 5},       {1, "INPUT", 0, 0},      {2, "POLARITY", 0, 5},
    {3, "COLOUR", 4, 6},      {4, "AMOUNT", 4, 3},     {5, "HPF", 5, 7},
    {6, "LPF", 5, 7},         {7, "EQ TYPE", 0, 6},    {8, "EQ IN", 0, 5},
    {9, "LF", 1, 1},          {10, "LF FREQ", 1, 4},   {11, "LF BELL", 1, 5},
    {12, "LMF", 0, 1},        {13, "LMF FREQ", 0, 4},  {14, "LMF Q", 0, 8},
    {15, "HMF", 3, 1},        {16, "HMF FREQ", 3, 4},  {17, "HMF Q", 3, 8},
    {18, "HF", 2, 1},         {19, "HF FREQ", 2, 4},   {20, "HF BELL", 2, 5},
    {21, "COMP", 0, 6},       {22, "COMP IN", 0, 5},   {23, "THRESH", 0, 9},
    {24, "MAKEUP", 0, 2},     {25, "F.ATK", 0, 5},     {26, "AUTO", 0, 5},
    {27, "GATE IN", 3, 5},    {28, "GATE THR", 3, 9},  {29, "RANGE", 3, 1},
    {30, "GATE REL", 3, 10},  {31, "GATE F.ATK", 3, 5},{32, "GATE MODE", 3, 6},
    {33, "POST EQ", 0, 5},    {34, "OUTPUT", 0, 11},   {35, "OVERSAMPLE", 5, 6},
    {36, "GATE HOLD", 3, 12}, {37, "LIMITER", 0, 5},   {38, "BUS", 5, 6},
};
constexpr int kDetailCount =
    static_cast<int>(sizeof(kDetail) / sizeof(kDetail[0]));

int enumCount(int plus) {
  switch (plus) {
    case 3: return 5;
    case 7: return 2;
    case 21: return 2;
    case 32: return 3;
    case 35: return 3;
    case 38: return 9;
    default: return 0;
  }
}

const char* enumName(int plus, int i) {
  static const char* kColour[] = {"VALVE", "CONSOLE", "BUS", "TAPE", "XFMR"};
  static const char* kTwo[] = {"VCA", "76"};
  static const char* kEq[] = {"BROWN", "BLACK"};
  static const char* kGate[] = {"EXP", "GATE", "DUCK"};
  static const char* kOs[] = {"OFF", "2X", "4X"};
  switch (plus) {
    case 3: return kColour[std::clamp(i, 0, 4)];
    case 7: return kEq[std::clamp(i, 0, 1)];
    case 21: return kTwo[std::clamp(i, 0, 1)];
    case 32: return kGate[std::clamp(i, 0, 2)];
    case 35: return kOs[std::clamp(i, 0, 2)];
    case 38: return kBusLabels[std::clamp(i, 0, 8)];
    default: return "";
  }
}

const char* formatDetail(int plus, int fmt, double v, char* buf, size_t n) {
  switch (fmt) {
    case 0:
      std::snprintf(buf, n, "%+.1f dB", -18.0 + 36.0 * v);
      break;
    case 1:
      std::snprintf(buf, n, "%+.1f dB", -15.0 + 30.0 * v);
      break;
    case 2:
      std::snprintf(buf, n, "%+.1f dB", -9.0 + 18.0 * v);
      break;
    case 3:
      std::snprintf(buf, n, "%.0f%%", 100.0 * v);
      break;
    case 4: {
      double hz = 1000.0;
      switch (plus) {
        case 10: hz = 30.0 * std::pow(15.0, v); break;
        case 13: hz = 200.0 * std::pow(12.5, v); break;
        case 16: hz = 600.0 * std::pow(11.67, v); break;
        case 19: hz = 1500.0 * std::pow(10.67, v); break;
        default: hz = 1000.0; break;
      }
      if (hz >= 1000.0) {
        std::snprintf(buf, n, "%.1fk", hz / 1000.0);
      } else {
        std::snprintf(buf, n, "%.0f", hz);
      }
      break;
    }
    case 5:
      return v > 0.5 ? "ON" : "OFF";
    case 6:
      return enumName(plus, static_cast<int>(v + 0.5));
    case 7:
      if (v <= 0.001) {
        std::snprintf(buf, n, "OUT");
      } else if (plus == 5) {
        std::snprintf(buf, n, "%.0f", 16.0 + 334.0 * v);
      } else {
        std::snprintf(buf, n, "%.1fk", (3000.0 + 12200.0 * v) / 1000.0);
      }
      break;
    case 8:
      std::snprintf(buf, n, "%.1f", 0.5 + 2.5 * v);
      break;
    case 9:
      std::snprintf(buf, n, "%+.0f dB", 10.0 - 30.0 * v);
      break;
    case 10:
      std::snprintf(buf, n, "%.2fs", 0.1 + 3.9 * v);
      break;
    case 11: {
      if (v <= 0.001) {
        std::snprintf(buf, n, "OFF");
      } else if (v >= 0.75) {
        std::snprintf(buf, n, "%+.1f dB", 48.0 * (v - 0.75));
      } else {
        const double t = v / 0.75;
        std::snprintf(buf, n, "%.1f dB",
                      60.0 * std::log10(std::max(t * t * t, 1e-6)));
      }
      break;
    }
    case 12:
      std::snprintf(buf, n, "%.0f ms", 500.0 * v);
      break;
    default:
      std::snprintf(buf, n, "%.2f", v);
      break;
  }
  return buf;
}

const char* dbText(char* buf, size_t n, double v01, double lo, double hi) {
  std::snprintf(buf, n, "%+.1f", lo + (hi - lo) * clamp01(v01));
  return buf;
}
const char* pctText(char* buf, size_t n, double v01) {
  std::snprintf(buf, n, "%.0f%%", 100.0 * clamp01(v01));
  return buf;
}

const char* faderText(char* buf, size_t n, double v) {
  if (v <= 0.001) {
    std::snprintf(buf, n, "OFF");
  } else if (v >= 0.75) {
    std::snprintf(buf, n, "%+.1f", 48.0 * (v - 0.75));
  } else {
    const double t = v / 0.75;
    std::snprintf(buf, n, "%.1f",
                  60.0 * std::log10(std::max(t * t * t, 1e-6)));
  }
  return buf;
}

double chDefault(int plus) {
  switch (plus) {
    case 0: return 1.0;    // power
    case 1: return 0.5;    // input
    case 3: return 1.0;    // colour model (Console)
    case 4: return 0.35;   // colour amount
    case 8: return 1.0;    // eq in
    case 10: return 0.70;  // lf freq
    case 13: return 0.637; // lmf freq
    case 14: return 0.4;   // lmf q
    case 16: return 0.655; // hmf freq
    case 17: return 0.4;   // hmf q
    case 19: return 0.707; // hf freq
    case 23: return 0.73;  // comp threshold
    case 24: return 0.5;   // makeup
    case 26: return 1.0;   // auto
    case 28: return 0.25;  // gate threshold
    case 29: return 0.6;   // gate range
    case 30: return 0.5;   // gate release
    case 32: return 1.0;   // gate mode
    case 34: return 0.75;  // output
    case 35: return 2.0;   // oversample
    case 37: return 1.0;   // limiter
    case 9:
    case 12:
    case 15:
    case 18: return 0.5;   // eq gains
    default: return 0.0;
  }
}

// Parses "‑6.5 dB", "35%", "250", "1.5k".
bool parseNumberText(const char* text, double* out, bool* percent) {
  if (!text) return false;
  while (*text == ' ') ++text;
  char* end = nullptr;
  const double v = std::strtod(text, &end);
  if (end == text) return false;
  bool scale = false;
  for (const char* p = end; *p; ++p) {
    if (*p == '%') scale = true;
    if (*p == 'k' || *p == 'K') {
      *out = v * 1000.0;
      *percent = scale;
      return true;
    }
  }
  *out = v;
  *percent = scale;
  return true;
}

double parseRemoteValue(int plus, const char* text) {
  double n = 0.0;
  bool pct = false;
  if (!parseNumberText(text, &n, &pct)) return -1.0;
  switch (plus) {
    case 1: return (n + 18.0) / 36.0;
    case 4: return n / 100.0;
    case 5: return n <= 0.0 ? 0.0 : (n - 16.0) / 334.0;
    case 6: return n <= 0.0 ? 0.0 : (n - 3000.0) / 12200.0;
    case 9:
    case 12:
    case 15:
    case 18: return (n + 15.0) / 30.0;
    case 10: return n <= 0.0 ? 0.0 : std::log(n / 30.0) / std::log(15.0);
    case 13: return n <= 0.0 ? 0.0 : std::log(n / 200.0) / std::log(12.5);
    case 16: return n <= 0.0 ? 0.0 : std::log(n / 600.0) / std::log(11.67);
    case 19: return n <= 0.0 ? 0.0 : std::log(n / 1500.0) / std::log(10.67);
    case 14:
    case 17: return (n - 0.5) / 2.5;
    case 23: return (10.0 - n) / 30.0;
    case 24: return (n + 9.0) / 18.0;
    case 28: return (10.0 - n) / 40.0;
    case 29: return n / 40.0;
    case 30: return (n - 0.1) / 3.9;
    case 34:
      if (n >= 0.0) return 0.75 + n / 48.0;
      return 0.75 * std::pow(10.0, n / 180.0);
    case 36: return n / 500.0;
    default:
      if (pct) return n / 100.0;
      return n;
  }
}

double parseLocalValue(uint32_t param, const char* text) {
  double n = 0.0;
  bool pct = false;
  if (!parseNumberText(text, &n, &pct)) return -1.0;
  switch (param) {
    case P_COMP_THRESHOLD: return (10.0 - n) / 30.0;
    case P_COMP_ATTACK: return n <= 0.0 ? 0.0 : std::log(n / 0.1) / std::log(300.0);
    case P_COMP_RELEASE: return n <= 0.0 ? 0.0 : std::log(n / 100.0) / std::log(12.0);
    case P_COMP_MIX:
    case P_SUM_AMOUNT: return n / 100.0;
    case P_INPUT:
    case P_OUTPUT: return (n + 18.0) / 36.0;
    case P_EQ_LF_GAIN:
    case P_EQ_LM_GAIN:
    case P_EQ_HM_GAIN:
    case P_EQ_HF_GAIN: return (n + 15.0) / 30.0;
    case P_COMP_RATIO: return std::floor(n + 0.5);
    default:
      if (pct) return n / 100.0;
      return n;
  }
}

// Shared display formatting for the bus knobs.
const char* busKnobText(uint32_t param, double v, char* buf, size_t n) {
  if (param == P_COMP_THRESHOLD) return dbText(buf, n, v, 10.0, -20.0);
  if (param == P_COMP_ATTACK) return dbText(buf, n, v, 0.1, 30.0);
  if (param == P_COMP_RELEASE) return dbText(buf, n, v, 100.0, 1200.0);
  if (param == P_COMP_MIX || param == P_SUM_AMOUNT) return pctText(buf, n, v);
  if (param == P_INPUT || param == P_OUTPUT) return dbText(buf, n, v, -18.0, 18.0);
  if (param == P_COMP_RATIO) {
    static const char* kR[] = {"2:1", "4:1", "10:1"};
    return kR[std::clamp(static_cast<int>(v + 0.5), 0, 2)];
  }
  return dbText(buf, n, v, -15.0, 15.0);
}

// Shared display formatting for the mirrored channel knobs.
const char* remoteText(int plus, double v, char* buf, size_t n) {
  switch (plus) {
    case 1: return dbText(buf, n, v, -18.0, 18.0);
    case 4: return pctText(buf, n, v);
    case 5:
    case 6:
      if (v <= 0.001) return "OUT";
      if (plus == 5) {
        std::snprintf(buf, n, "%.0f", 16.0 + 334.0 * v);
      } else {
        std::snprintf(buf, n, "%.1fk", (3000.0 + 12200.0 * v) / 1000.0);
      }
      return buf;
    case 9:
    case 12:
    case 15:
    case 18: return dbText(buf, n, v, -15.0, 15.0);
    case 10: {
      const double hz = 30.0 * std::pow(15.0, v);
      if (hz >= 1000.0) std::snprintf(buf, n, "%.1fk", hz / 1000.0);
      else std::snprintf(buf, n, "%.0f", hz);
      return buf;
    }
    case 13: {
      const double hz = 200.0 * std::pow(12.5, v);
      if (hz >= 1000.0) std::snprintf(buf, n, "%.1fk", hz / 1000.0);
      else std::snprintf(buf, n, "%.0f", hz);
      return buf;
    }
    case 16: {
      const double hz = 600.0 * std::pow(11.67, v);
      if (hz >= 1000.0) std::snprintf(buf, n, "%.1fk", hz / 1000.0);
      else std::snprintf(buf, n, "%.0f", hz);
      return buf;
    }
    case 19: {
      const double hz = 1500.0 * std::pow(10.67, v);
      std::snprintf(buf, n, "%.1fk", hz / 1000.0);
      return buf;
    }
    case 14:
    case 17: std::snprintf(buf, n, "%.1f", 0.5 + 2.5 * v); return buf;
    case 23: return dbText(buf, n, v, 10.0, -20.0);
    case 24: return dbText(buf, n, v, -9.0, 9.0);
    case 28: return dbText(buf, n, v, -30.0, 10.0);
    case 29: std::snprintf(buf, n, "%.0f dB", 40.0 * v); return buf;
    case 30: std::snprintf(buf, n, "%.2fs", 0.1 + 3.9 * v); return buf;
    case 34: return faderText(buf, n, v);
    case 36: std::snprintf(buf, n, "%.0f ms", 500.0 * v); return buf;
    default: return dbText(buf, n, v, -15.0, 15.0);
  }
}

bool isToggleKind(int fmt) { return fmt == 5; }
bool isEnumKind(int fmt) { return fmt == 6; }

bool isDefaultName(const char* name) {
  return !name || name[0] == '\0' || std::strcmp(name, "Suite Channel Plus") == 0;
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

int Panel::visibleCount() const {
  int n = 0;
  for (int i = 0; i < hub_.size(); ++i) {
    if (bus_ != 0 && static_cast<int>(hub_.bus(i)) != bus_) continue;
    ++n;
  }
  return n;
}

int Panel::maxScroll() const { return std::max(0, visibleCount() - kVisible); }

int Panel::nameKey(int i) const {
  if (i < 0 || i >= hub_.size()) return 0;
  const uint32_t bus = hub_.bus(i);
  int ordinal = 0;
  for (int k = 0; k <= i; ++k) {
    if (hub_.bus(k) == bus) ++ordinal;
  }
  const int key = static_cast<int>(bus) * 32 + (ordinal - 1);
  return std::clamp(key, 0, Shared::kNameSlots - 1);
}

void Panel::stripLabel(int i, const Shared& shared, char* buf, size_t n) const {
  const int key = nameKey(i);
  const char* custom = shared.names[key];
  if (custom[0]) {
    std::snprintf(buf, n, "%s", custom);
    return;
  }
  const uint32_t bus = hub_.bus(i);
  int ordinal = 0;
  for (int k = 0; k <= i; ++k) {
    if (hub_.bus(k) == bus) ++ordinal;
  }
  if (bus == 0) {
    std::snprintf(buf, n, "OFF CH%d", ordinal);
  } else {
    std::snprintf(buf, n, "BUS %s CH%d", kBusLabels[std::clamp<int>(bus, 0, 8)],
                  ordinal);
  }
}

void Panel::applyMuteSolo(Shared& shared) {
  // Solo is scoped to what is on screen: the whole console on ALL, or the
  // current bus otherwise.  This keeps solo from silencing other buckets.
  auto visible = [&](int i) {
    return bus_ == 0 || static_cast<int>(hub_.bus(i)) == bus_;
  };
  bool anySolo = false;
  for (int k = 0; k < hub_.size(); ++k) {
    if (visible(k) && shared.soloed[nameKey(k)]) {
      anySolo = true;
      break;
    }
  }
  for (int i = 0; i < hub_.size(); ++i) {
    if (!visible(i)) continue;
    const int key = nameKey(i);
    const bool audible = !shared.muted[key] &&
                         (!anySolo || shared.soloed[key]);
    if (!audible) {
      if (!shared.gainForced[key]) {
        shared.savedGain[key] = static_cast<float>(hub_.value(i, ch::kOutput));
        shared.gainForced[key] = true;
      }
      hub_.write(i, ch::kOutput, 0.0);
    } else if (shared.gainForced[key]) {
      hub_.write(i, ch::kOutput, shared.savedGain[key]);
      shared.gainForced[key] = false;
    }
  }
}

void Panel::tick(const Shared& shared, PanelState& state) {
  (void)state;
  bus_ = std::clamp(static_cast<int>(shared.get(P_BUS) + 0.5), 0, 8);
  hub_.refresh();
  if (detail_ >= hub_.size()) detail_ = hub_.size() - 1;
  scroll_ = std::clamp(scroll_, 0, maxScroll());
}

void Panel::wheel(Shared& shared, PanelState& state, double x, double y,
                  double d, bool fine) {
  layout(shared, state);
  const Widget* w = hitTest(x, y);
  if (w) {
    const double step = (fine ? 0.002 : 0.02) * (d > 0 ? 1.0 : -1.0);
    if (w->kind == Kind::Knob) {
      shared.set(w->param, clamp01(shared.get(w->param) + step));
      return;
    }
    if (w->kind == Kind::Remote || w->kind == Kind::RemoteFader) {
      hub_.write(w->index, w->plus,
                 clamp01(hub_.value(w->index, w->plus) + step));
      return;
    }
  }
  if (page_ != 0) return;
  if (x > kBusX - 10 || y < kStripTop - 20) return;
  scroll_ = std::clamp(scroll_ - static_cast<int>(d), 0, maxScroll());
}

const Panel::Widget* Panel::hitTest(double x, double y) const {
  for (const Widget& w : widgets_) {
    const bool hit =
        (w.kind == Kind::Knob || w.kind == Kind::Remote)
            ? ((x - w.x) * (x - w.x) + (y - w.y) * (y - w.y) <=
               w.r * w.r * 1.6)
            : (x >= w.x && x <= w.x + w.w && y >= w.y && y <= w.y + w.h);
    if (hit) return &w;
  }
  return nullptr;
}

void Panel::resetWidget(Shared& shared, const Widget& w) {
  switch (w.kind) {
    case Kind::Knob:
      shared.set(w.param, kParamSpecs[w.param].def);
      break;
    case Kind::Remote:
    case Kind::RemoteFader:
    case Kind::RemotePill:
    case Kind::RemoteButton:
      hub_.write(w.index, w.plus, chDefault(w.plus));
      break;
    default:
      break;
  }
}

static double steadyMs() {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
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

void Panel::layoutBus(const Shared& shared, const PanelState& state) {
  (void)state;
  // Bus tabs.
  for (int i = 0; i <= 8; ++i) {
    Widget t;
    t.kind = Kind::Tab;
    t.param = P_BUS;
    t.value = static_cast<double>(i);
    t.x = 300.0 + i * 36.0;
    t.y = 18.0;
    t.w = 34.0;
    t.h = 28.0;
    t.label = kBusLabels[i];
    widgets_.push_back(t);
  }
  {
    Widget c;
    c.kind = Kind::Capture;
    c.x = 636.0;
    c.y = 18.0;
    c.w = 120.0;
    c.h = 28.0;
    c.label = "CAPTURE";
    widgets_.push_back(c);
  }
  {
    Widget s;
    s.kind = Kind::Scroll;
    s.value = -1.0;
    s.x = 768.0;
    s.y = 18.0;
    s.w = 24.0;
    s.h = 28.0;
    s.label = "<";
    widgets_.push_back(s);
    s.value = 1.0;
    s.x = 796.0;
    s.label = ">";
    widgets_.push_back(s);
  }

  // View selector row across the console area.
  static const char* kViews[] = {"MIX", "INPUT", "EQ", "DYN", "OUT"};
  for (int v = 0; v < 5; ++v) {
    Widget w;
    w.kind = Kind::View;
    w.value = static_cast<double>(v);
    w.x = 48.0 + v * 188.0;
    w.y = 78.0;
    w.w = 180.0;
    w.h = 24.0;
    w.label = kViews[v];
    widgets_.push_back(w);
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
  auto remote = [&](int i, int plus, double x, double y, double r,
                    const char* label, int palette) {
    Widget k;
    k.kind = Kind::Remote;
    k.index = i;
    k.plus = plus;
    k.x = x;
    k.y = y;
    k.r = r;
    k.label = label;
    k.palette = palette;
    widgets_.push_back(k);
  };
  auto rpill = [&](int i, int plus, double x, double y, double w, double h,
                   const char* label) {
    Widget b;
    b.kind = Kind::RemotePill;
    b.index = i;
    b.plus = plus;
    b.x = x;
    b.y = y;
    b.w = w;
    b.h = h;
    b.label = label;
    widgets_.push_back(b);
  };

  int shown = 0;
  int idx = 0;
  for (int i = 0; i < hub_.size() && shown < kVisible; ++i) {
    if (bus_ != 0 && static_cast<int>(hub_.bus(i)) != bus_) continue;
    if (idx++ < scroll_) continue;
    const double x = kStripX0 + shown * (kStripW + kStripGap);
    {
      Widget r;
      r.kind = Kind::Rename;
      r.index = i;
      r.x = x + kStripW - 28;
      r.y = kStripTop + 5;
      r.w = 22;
      r.h = 18;
      r.label = "REN";
      widgets_.push_back(r);
    }
    {
      Widget w;
      w.kind = Kind::Strip;
      w.index = i;
      w.value = shown;
      w.x = x;
      w.y = kStripTop + 2;
      w.w = kStripW - 32;
      w.h = 26.0;
      widgets_.push_back(w);
    }
    switch (view_) {
      case 0:  // mix
        remote(i, ch::kInput, x + 34, kStripTop + 84, 21, "INPUT", 0);
        remote(i, ch::kCompThreshold, x + 86, kStripTop + 84, 21, "COMP", 0);
        remote(i, ch::kCompMakeup, x + 34, kStripTop + 192, 21, "MAKEUP", 0);
        remote(i, ch::kOutput, x + 86, kStripTop + 192, 21, "OUTPUT", 0);
        rpill(i, ch::kCompIn, x + 16, kStripTop + 262, 48, 22, "COMP");
        rpill(i, ch::kEqIn, x + 68, kStripTop + 262, 38, 22, "EQ");
        break;
      case 1:  // input / colour / filters
        remote(i, ch::kInput, x + 34, kStripTop + 84, 21, "INPUT", 0);
        remote(i, 4, x + 86, kStripTop + 84, 21, "COLOUR", 4);
        remote(i, 5, x + 34, kStripTop + 192, 21, "HPF", 5);
        remote(i, 6, x + 86, kStripTop + 192, 21, "LPF", 5);
        rpill(i, ch::kEnabled, x + 12, kStripTop + 262, 50, 22, "POWER");
        rpill(i, 2, x + 66, kStripTop + 262, 40, 22, "POL");
        rpill(i, 3, x + 20, kStripTop + 294, 80, 22, "COLOUR");
        break;
      case 2:  // eq
        remote(i, 9, x + 34, kStripTop + 84, 21, "LF", 1);
        remote(i, 12, x + 86, kStripTop + 84, 21, "LMF", 0);
        remote(i, 15, x + 34, kStripTop + 192, 21, "HMF", 3);
        remote(i, 18, x + 86, kStripTop + 192, 21, "HF", 2);
        rpill(i, ch::kEqIn, x + 10, kStripTop + 262, 46, 22, "EQ IN");
        rpill(i, 7, x + 60, kStripTop + 262, 50, 22, "TYPE");
        rpill(i, 11, x + 10, kStripTop + 294, 46, 22, "LF BELL");
        rpill(i, 20, x + 60, kStripTop + 294, 50, 22, "HF BELL");
        break;
      case 3:  // dynamics
        remote(i, ch::kCompThreshold, x + 34, kStripTop + 84, 21, "THRESH", 0);
        remote(i, ch::kCompMakeup, x + 86, kStripTop + 84, 21, "MAKEUP", 0);
        remote(i, 28, x + 34, kStripTop + 192, 21, "GATE THR", 3);
        remote(i, 30, x + 86, kStripTop + 192, 21, "GATE REL", 3);
        rpill(i, ch::kCompIn, x + 10, kStripTop + 262, 46, 22, "COMP");
        rpill(i, 26, x + 60, kStripTop + 262, 50, 22, "AUTO");
        rpill(i, 27, x + 10, kStripTop + 294, 46, 22, "GATE");
        rpill(i, 25, x + 60, kStripTop + 294, 50, 22, "F.ATK");
        break;
      default: {  // output fader
        Widget f;
        f.kind = Kind::RemoteFader;
        f.index = i;
        f.plus = ch::kOutput;
        f.x = x + 40;
        f.y = kStripTop + 70;
        f.w = 60.0;
        f.h = 400.0;
        f.label = "OUTPUT";
        widgets_.push_back(f);
        rpill(i, 37, x + 14, kStripTop + 500, 60, 22, "LIMIT");
        rpill(i, 35, x + 78, kStripTop + 500, 32, 22, "OS");
        break;
      }
    }
    {
      Widget m;
      m.kind = Kind::Mute;
      m.index = i;
      m.x = x + 10;
      m.y = kStripTop + 556;
      m.w = 46;
      m.h = 22;
      m.label = "MUTE";
      widgets_.push_back(m);
    }
    {
      Widget so;
      so.kind = Kind::Solo;
      so.index = i;
      so.x = x + 62;
      so.y = kStripTop + 556;
      so.w = 46;
      so.h = 22;
      so.label = "SOLO";
      widgets_.push_back(so);
    }
    ++shown;
  }

  const double bx = kBusX + 8;
  pill(P_COMP_IN, bx, 138, 54, 20, "COMP");
  knob(P_COMP_THRESHOLD, bx + 30, 218, 19, "THRESH", 0);
  knob(P_COMP_RATIO, bx + 96, 218, 19, "RATIO", 0);
  knob(P_COMP_MIX, bx + 162, 218, 17, "MIX", 0);
  knob(P_COMP_ATTACK, bx + 30, 316, 19, "ATTACK", 0);
  knob(P_COMP_RELEASE, bx + 96, 316, 19, "RELEASE", 0);

  pill(P_EQ_IN, bx, 386, 40, 20, "EQ");
  pill(P_EQ_TYPE, bx + 48, 386, 60, 20, "TYPE");
  // Left column shares one axis: THRESH, ATTACK, LF and SUM.
  knob(P_EQ_LF_GAIN, bx + 30, 466, 19, "LF", 1);
  knob(P_EQ_LM_GAIN, bx + 82, 466, 19, "LMF", 0);
  knob(P_EQ_HM_GAIN, bx + 134, 466, 19, "HMF", 3);
  knob(P_EQ_HF_GAIN, bx + 186, 466, 19, "HF", 2);

  knob(P_SUM_AMOUNT, bx + 30, 600, 20, "SUM", 4);

  pill(P_LIMITER, bx + 8, 676, 60, 20, "LIMIT");
  knob(P_INPUT, bx + 110, 700, 17, "IN", 0);
  knob(P_OUTPUT, bx + 172, 700, 19, "OUT", 0);
}

void Panel::layoutDetail() {
  auto pillWidget = [&](int plus, double x, double y, double w, double h,
                        const char* label) {
    Widget b;
    b.kind = Kind::RemotePill;
    b.index = detail_;
    b.plus = plus;
    b.x = x;
    b.y = y;
    b.w = w;
    b.h = h;
    b.label = label;
    widgets_.push_back(b);
  };
  auto buttonWidget = [&](int plus, double x, double y, double w, double h,
                          double value, const char* label) {
    Widget b;
    b.kind = Kind::RemoteButton;
    b.index = detail_;
    b.plus = plus;
    b.x = x;
    b.y = y;
    b.w = w;
    b.h = h;
    b.value = value;
    b.label = label;
    widgets_.push_back(b);
  };
  auto knobWidget = [&](int plus, double x, double y, double r,
                        const char* label, int palette) {
    Widget k;
    k.kind = Kind::Remote;
    k.index = detail_;
    k.plus = plus;
    k.x = x;
    k.y = y;
    k.r = r;
    k.label = label;
    k.palette = palette;
    widgets_.push_back(k);
  };

  // Header row, inset past the corner screw.
  {
    Widget back;
    back.kind = Kind::Page;
    back.value = 0.0;
    back.x = 44;
    back.y = 80;
    back.w = 100;
    back.h = 26;
    back.label = "< MIX BUS";
    widgets_.push_back(back);
  }
  int shown = 0;
  for (int i = 0; i < hub_.size() && shown < 8; ++i) {
    if (bus_ != 0 && static_cast<int>(hub_.bus(i)) != bus_) continue;
    Widget tab;
    tab.kind = Kind::Page;
    tab.value = 1.0;
    tab.index = i;
    tab.x = 156.0 + shown * 46.0;
    tab.y = 80;
    tab.w = 40;
    tab.h = 26;
    widgets_.push_back(tab);
    ++shown;
  }

  // INPUT / COLOUR
  pillWidget(0, 30, 141, 58, 20, "POWER");
  pillWidget(2, 94, 141, 38, 20, "POL");
  knobWidget(1, 66, 197, 20, "INPUT", 0);
  knobWidget(4, 142, 197, 20, "COLOUR", 4);
  pillWidget(3, 200, 185, 92, 22, "COLOUR");

  // DYNAMICS
  pillWidget(22, 30, 286, 58, 20, "COMP IN");
  buttonWidget(21, 94, 286, 40, 20, 0.0, "VCA");
  buttonWidget(21, 138, 286, 40, 20, 1.0, "76");
  knobWidget(23, 62, 356, 22, "THRESH", 0);
  knobWidget(24, 136, 356, 22, "MAKEUP", 0);
  pillWidget(25, 204, 338, 46, 20, "F.ATK");
  pillWidget(26, 204, 364, 46, 20, "AUTO");
  pillWidget(33, 204, 390, 46, 20, "POST EQ");

  // GATE
  pillWidget(27, 30, 478, 54, 20, "GATE IN");
  pillWidget(32, 90, 478, 62, 20, "MODE");
  knobWidget(28, 56, 546, 20, "THRESH", 3);
  knobWidget(29, 112, 546, 20, "RANGE", 3);
  knobWidget(30, 168, 546, 20, "RELEASE", 3);
  pillWidget(31, 228, 536, 44, 20, "F.ATK");
  knobWidget(36, 246, 596, 14, "HOLD", 3);

  // FILTERS
  knobWidget(5, 72, 704, 17, "HPF", 5);
  knobWidget(6, 180, 704, 17, "LPF", 5);

  // EQUALISER
  pillWidget(8, 322, 140, 46, 20, "EQ IN");
  buttonWidget(7, 374, 140, 64, 20, 0.0, "BROWN");
  buttonWidget(7, 444, 140, 64, 20, 1.0, "BLACK");
  knobWidget(9, 392, 222, 24, "LF GAIN", 1);
  knobWidget(10, 546, 222, 22, "LF FREQ", 1);
  pillWidget(11, 664, 210, 62, 20, "LF BELL");
  knobWidget(12, 392, 332, 24, "LMF GAIN", 0);
  knobWidget(13, 546, 332, 22, "LMF FREQ", 0);
  knobWidget(14, 700, 332, 22, "LMF Q", 0);
  knobWidget(15, 392, 442, 24, "HMF GAIN", 3);
  knobWidget(16, 546, 442, 22, "HMF FREQ", 3);
  knobWidget(17, 700, 442, 22, "HMF Q", 3);
  knobWidget(18, 392, 552, 24, "HF GAIN", 2);
  knobWidget(19, 546, 552, 22, "HF FREQ", 2);
  pillWidget(20, 664, 540, 62, 20, "HF BELL");

  // OUTPUT
  {
    Widget f;
    f.kind = Kind::RemoteFader;
    f.index = detail_;
    f.plus = 34;
    f.x = 1058;
    f.y = 190;
    f.w = 60.0;
    f.h = 430.0;
    f.label = "OUTPUT";
    widgets_.push_back(f);
  }
  pillWidget(37, 1004, 640, 62, 22, "LIMIT");
  pillWidget(35, 1074, 640, 66, 22, "OVERSAMPLE");
}

void Panel::layout(const Shared& shared, const PanelState& state) {
  widgets_.clear();
  if (page_ == 0) {
    layoutBus(shared, state);
  } else {
    layoutDetail();
  }
}

bool Panel::pointerDown(Shared& shared, PanelState& state, double x, double y,
                        bool fine) {
  if (editing_) closeEdit();
  layout(shared, state);
  state.fine = fine;
  for (const Widget& w : widgets_) {
    const bool hit =
        (w.kind == Kind::Knob || w.kind == Kind::Remote)
            ? ((x - w.x) * (x - w.x) + (y - w.y) * (y - w.y) <=
               w.r * w.r * 1.6)
            : (x >= w.x && x <= w.x + w.w && y >= w.y && y <= w.y + w.h);
    if (!hit) continue;
    // Double-click resets the control to its default.
    uint64_t id = 0;
    if (w.kind == Kind::Knob) {
      id = 0x10000000ull + w.param;
    } else if (w.kind == Kind::Remote || w.kind == Kind::RemoteFader ||
               w.kind == Kind::RemotePill || w.kind == Kind::RemoteButton) {
      id = 0x20000000ull + static_cast<uint64_t>(w.index) * 64 +
           static_cast<uint64_t>(w.plus + 1);
    }
    const double now = steadyMs();
    if (id != 0 && id == lastClickId_ && now - lastClickMs_ < 350.0) {
      resetWidget(shared, w);
      lastClickId_ = 0;
      lastClickMs_ = now;
      return true;
    }
    lastClickId_ = id;
    lastClickMs_ = now;
    switch (w.kind) {
      case Kind::View:
        view_ = static_cast<int>(w.value);
        return true;
      case Kind::Scroll:
        scroll_ = std::clamp(scroll_ + static_cast<int>(w.value), 0,
                             maxScroll());
        return true;
      case Kind::Page:
        if (w.value == 0.0) {
          page_ = 0;
        } else {
          page_ = 1;
          detail_ = w.index;
        }
        return true;
      case Kind::Rename:
        editKind_ = 0;
        editIndex_ = w.index;
        editKey_ = nameKey(w.index);
        editText_ = shared.names[editKey_];
        editing_ = true;
        return true;
      case Kind::Strip:
        state.selectedStrip = static_cast<int>(w.value);
        page_ = 1;
        detail_ = w.index;
        return true;
      case Kind::Tab:
        shared.set(P_BUS, w.value);
        bus_ = static_cast<int>(w.value + 0.5);
        scroll_ = 0;
        return true;
      case Kind::Capture: {
        hub_.refresh();
        int count = 0;
        if (bus_ > 0) {
          for (int i = 0; i < hub_.size(); ++i) {
            hub_.write(i, ch::kBus, bus_);
            ++count;
          }
        }
        captureCount_ = count;
        captureNoticeUntil_ =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count() +
            2200.0;
        return true;
      }
      case Kind::Pill:
        if (w.param == P_EQ_TYPE) {
          shared.set(P_EQ_TYPE, shared.get(P_EQ_TYPE) > 0.5 ? 0.0 : 1.0);
        } else {
          shared.set(w.param, shared.get(w.param) > 0.5 ? 0.0 : 1.0);
        }
        return true;
      case Kind::Mute: {
        const int key = nameKey(w.index);
        shared.muted[key] = !shared.muted[key];
        applyMuteSolo(shared);
        return true;
      }
      case Kind::Solo: {
        const int key = nameKey(w.index);
        shared.soloed[key] = !shared.soloed[key];
        applyMuteSolo(shared);
        return true;
      }
      case Kind::RemoteButton:
        hub_.write(w.index, w.plus, w.value);
        return true;
      case Kind::RemotePill: {
        const double v = hub_.value(w.index, w.plus);
        const int count = enumCount(w.plus);
        if (count > 0) {
          const int next = (static_cast<int>(v + 0.5) + 1) % count;
          hub_.write(w.index, w.plus, static_cast<double>(next));
        } else {
          hub_.write(w.index, w.plus, v > 0.5 ? 0.0 : 1.0);
        }
        return true;
      }
      case Kind::RemoteFader:
        dragging_ = true;
        dragKind_ = 2;
        dragIndex_ = w.index;
        dragPlus_ = w.plus;
        dragY_ = y;
        dragStart_ = hub_.value(w.index, w.plus);
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
  if (dragKind_ == 2) {
    // Fader: full travel over ~380 px.
    const double delta = (dragY_ - y) / 380.0;
    hub_.write(dragIndex_, dragPlus_, clamp01(dragStart_ + delta));
    return true;
  }
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

bool Panel::sameEditTarget(const Widget& w) const {
  if (editKind_ == 0) {
    return w.kind == Kind::Rename && w.index == editIndex_;
  }
  if (editKind_ == 1) {
    return w.kind == Kind::Knob && static_cast<int>(w.param) == editParam_;
  }
  if (editKind_ == 2) {
    return (w.kind == Kind::Remote || w.kind == Kind::RemoteFader) &&
           w.index == editIndex_ && w.plus == editPlus_;
  }
  return false;
}

void Panel::closeEdit() {
  editing_ = false;
  editText_.clear();
  editKey_ = -1;
  editIndex_ = -1;
  editParam_ = -1;
  editPlus_ = -1;
}

bool Panel::beginTextEntry(Shared& shared, PanelState& state, double x,
                           double y) {
  layout(shared, state);
  const Widget* w = hitTest(x, y);
  // A second right-click on the same control, or a right-click on empty
  // space, closes the editor without applying.  A different control cancels
  // the current entry and opens the new one.
  if (editing_) {
    if (!w || sameEditTarget(*w)) {
      closeEdit();
      return true;
    }
    closeEdit();
  }
  if (!w) return false;
  if (w->kind == Kind::Rename) {
    editKind_ = 0;
    editIndex_ = w->index;
    editKey_ = nameKey(w->index);
    editText_ = shared.names[editKey_];
    editing_ = true;
    return true;
  }
  if (w->kind == Kind::Knob) {
    // Typed entry starts empty so the whole value can be replaced.
    editKind_ = 1;
    editParam_ = static_cast<int>(w->param);
    editText_.clear();
    editing_ = true;
    return true;
  }
  if (w->kind == Kind::Remote || w->kind == Kind::RemoteFader) {
    editKind_ = 2;
    editIndex_ = w->index;
    editPlus_ = w->plus;
    editText_.clear();
    editing_ = true;
    return true;
  }
  return false;
}

void Panel::textKey(Shared& shared, PanelState& state, int code, unsigned ch) {
  (void)state;
  if (!editing_) return;
  if (code == kTextEnter) {
    if (editKind_ == 0) {
      if (editKey_ >= 0 && editKey_ < Shared::kNameSlots) {
        std::snprintf(shared.names[editKey_], Shared::kNameBytes, "%s",
                      editText_.c_str());
      }
    } else if (editKind_ == 1) {
      const double v = parseLocalValue(static_cast<uint32_t>(editParam_),
                                       editText_.c_str());
      if (v >= 0.0) shared.set(static_cast<uint32_t>(editParam_), clamp01(v));
    } else if (editKind_ == 2) {
      const double v = parseRemoteValue(editPlus_, editText_.c_str());
      if (v >= 0.0) hub_.write(editIndex_, editPlus_, clamp01(v));
    }
    editing_ = false;
    editText_.clear();
    editKey_ = -1;
  } else if (code == kTextTab && editKind_ == 0) {
    // Commit this name and open the next strip's field.
    if (editKey_ >= 0 && editKey_ < Shared::kNameSlots) {
      std::snprintf(shared.names[editKey_], Shared::kNameBytes, "%s",
                    editText_.c_str());
    }
    int next = -1;
    bool seen = false;
    for (int i = 0; i < hub_.size() && next < 0; ++i) {
      if (bus_ != 0 && static_cast<int>(hub_.bus(i)) != bus_) continue;
      if (seen) {
        next = i;
        break;
      }
      if (i == editIndex_) seen = true;
    }
    if (next >= 0) {
      editIndex_ = next;
      editKey_ = nameKey(next);
      editText_ = shared.names[editKey_];
    } else {
      editing_ = false;
      editText_.clear();
      editKey_ = -1;
      editIndex_ = -1;
    }
  } else if (code == kTextEscape) {
    editing_ = false;
    editText_.clear();
    editKey_ = -1;
    editIndex_ = -1;
  } else if (code == kTextBackspace) {
    if (!editText_.empty()) editText_.pop_back();
  } else if (ch >= 32 && ch < 127 && editText_.size() < 20) {
    editText_.push_back(static_cast<char>(ch));
  }
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
    text(cr, readout, cx, cy - r - 7.0, 8.5, 1, kLegend.r, kLegend.g,
         kLegend.b);
  }
  text(cr, w.label, cx, cy + r + 13.0, 9.0, 1, kLegend.r, kLegend.g,
       kLegend.b);
}

void Panel::fader(cairo_t* cr, const Widget& w, double value) const {
  const double h = w.h;
  const double top = w.y;
  const double cx = w.x;
  roundRect(cr, cx - 7, top, 14, h, 5);
  setRgb(cr, {0.06, 0.07, 0.08});
  cairo_fill(cr);
  cairo_set_line_width(cr, 1.0);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.07);
  roundRect(cr, cx - 6.5, top + 0.5, 13, h - 1, 5);
  cairo_stroke(cr);

  // Scale ticks.
  static const struct {
    double v;
    const char* label;
  } kTicks[] = {{1.0, "+12"}, {0.75, "0"},   {0.5, "-12"},
                {0.25, "-24"}, {0.0, "OFF"}};
  for (const auto& t : kTicks) {
    const double y = top + (1.0 - t.v) * (h - 4.0) + 2.0;
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
    cairo_move_to(cr, cx - 9, y);
    cairo_line_to(cr, cx - 15, y);
    cairo_stroke(cr);
    cairo_move_to(cr, cx + 9, y);
    cairo_line_to(cr, cx + 15, y);
    cairo_stroke(cr);
  }

  const double capY = top + (1.0 - clamp01(value)) * (h - 20.0);
  cairo_pattern_t* pat = cairo_pattern_create_linear(0, capY, 0, capY + 20);
  cairo_pattern_add_color_stop_rgb(pat, 0.0, 0.46, 0.48, 0.51);
  cairo_pattern_add_color_stop_rgb(pat, 0.5, 0.30, 0.32, 0.35);
  cairo_pattern_add_color_stop_rgb(pat, 1.0, 0.18, 0.19, 0.21);
  roundRect(cr, cx - 26, capY, 52, 20, 4);
  cairo_set_source(cr, pat);
  cairo_fill(cr);
  cairo_pattern_destroy(pat);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.35);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, cx - 22, capY + 10);
  cairo_line_to(cr, cx + 22, capY + 10);
  cairo_stroke(cr);

  char buf[24];
  text(cr, faderText(buf, sizeof(buf), value), cx, top - 8, 9.0, 1,
       kLegend.r, kLegend.g, kLegend.b);
  text(cr, w.label, cx, top + h + 14, 9.0, 1, kLegend.r, kLegend.g,
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
                    float grDb, bool vertical) const {
  roundRect(cr, x, y, w, h, 3);
  setRgb(cr, {0.06, 0.07, 0.08});
  cairo_fill(cr);
  const double t = clamp01(grDb / 24.0);
  setRgb(cr, {0.95, 0.55, 0.20});
  if (vertical) {
    // Reduction hangs from the top, like a console GR ladder.
    cairo_rectangle(cr, x + 2.0, y + 2.0, w - 4.0, (h - 4.0) * t);
  } else {
    cairo_rectangle(cr, x + 2.0, y + 2.0, (w - 4.0) * t, h - 4.0);
  }
  cairo_fill(cr);
}

void Panel::draw(cairo_t* cr, const Shared& shared, const PanelState& state) {
  layout(shared, state);
  cairo_set_source_rgb(cr, 0.045, 0.050, 0.060);
  cairo_paint(cr);

  cairo_rectangle(cr, 0, 0, kPanelWidth, kHeaderH);
  setRgb(cr, kHeaderBg);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, 0, 0.5);
  cairo_line_to(cr, kPanelWidth, 0.5);
  cairo_stroke(cr);
  text(cr, "SUITE CONTROL", 24, 40, 22.0, 0, kLegend.r, kLegend.g, kLegend.b);

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

  if (page_ == 0) {
    drawBusPage(cr, shared, state);
  } else {
    drawDetailPage(cr, shared, state);
  }

  if (editing_ && editKind_ != 0) {
    const double w = 220.0, h = 30.0;
    const double x = (kPanelWidth - w) * 0.5;
    const double y = kPlateY + 10.0;
    roundRect(cr, x, y, w, h, 4);
    setRgb(cr, {0.06, 0.07, 0.08});
    cairo_fill(cr);
    setRgb(cr, kAccent, 0.9);
    cairo_set_line_width(cr, 1.0);
    roundRect(cr, x + 0.5, y + 0.5, w - 1, h - 1, 4);
    cairo_stroke(cr);
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%s_", editText_.c_str());
    text(cr, buf, x + 12, y + 20, 12, 0, kLegend.r, kLegend.g, kLegend.b);
    text(cr, "ENTER", x + w - 12, y + 20, 8, 2, kLegendDim.r, kLegendDim.g,
         kLegendDim.b);
  }
}

void Panel::drawBusPage(cairo_t* cr, const Shared& shared,
                        const PanelState& state) {
  const int total = visibleCount();
  const double nowMs = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
  char status[128];
  if (captureCount_ >= 0 && nowMs < captureNoticeUntil_) {
    std::snprintf(status, sizeof(status), "CAPTURED %d CHANNEL%s TO BUS %s",
                  captureCount_, captureCount_ == 1 ? "" : "S",
                  kBusLabels[std::clamp(bus_, 0, 8)]);
    text(cr, status, kPanelWidth - 24, 40, 9.0, 2, kAccent.r * 0.7 + 0.2,
         kAccent.g, kAccent.b);
  } else if (total == 0) {
    std::snprintf(status, sizeof(status), "NO CHANNELS ON BUS %s",
                  kBusLabels[std::clamp(bus_, 0, 8)]);
    text(cr, status, kPanelWidth - 24, 40, 8.5, 2, 0.80, 0.45, 0.35);
  } else {
    const int first = std::min(scroll_ + 1, total);
    const int last = std::min(scroll_ + kVisible, total);
    std::snprintf(status, sizeof(status), "SHOWING %d-%d OF %d", first, last,
                  total);
    text(cr, status, kPanelWidth - 24, 40, 8.5, 2, kLegendDim.r, kLegendDim.g,
         kLegendDim.b);
  }

  if (total == 0) {
    const bool anyChannels = hub_.size() > 0;
    char line1[128];
    if (anyChannels) {
      std::snprintf(line1, sizeof(line1), "NO CHANNELS ON BUS %s",
                    kBusLabels[std::clamp(bus_, 0, 8)]);
    } else if (hub_.segments() == 0) {
      std::snprintf(line1, sizeof(line1), "NO PEER SEGMENTS");
    } else {
      std::snprintf(line1, sizeof(line1), "PEER SEGMENTS FOUND, NO CHANNELS");
    }
    text(cr, line1, 500, 350, 14, 1, kLegend.r, kLegend.g, kLegend.b);
    if (anyChannels) {
      text(cr, "Set the BUS knob to this bus, or press CAPTURE to assign every",
           500, 378, 9, 1, kLegendDim.r, kLegendDim.g, kLegendDim.b);
      text(cr, "Suite Channel Plus instance to it.", 500, 394, 9, 1,
           kLegendDim.r, kLegendDim.g, kLegendDim.b);
    } else if (hub_.segments() == 0) {
      text(cr, "Suite Channel Plus has not registered in this session.", 500,
           378, 9, 1, kLegendDim.r, kLegendDim.g, kLegendDim.b);
      text(cr, "Fully restart the host after installing and check that", 500,
           394, 9, 1, kLegendDim.r, kLegendDim.g, kLegendDim.b);
      text(cr, "Suite Channel Plus is loaded on at least one track.", 500, 410,
           9, 1, kLegendDim.r, kLegendDim.g, kLegendDim.b);
    } else {
      text(cr, "Suite Channel Plus is loaded but has not registered a slot.",
           500, 378, 9, 1, kLegendDim.r, kLegendDim.g, kLegendDim.b);
      text(cr, "Reinstall Suite Channel Plus and restart the host.", 500, 394,
           9, 1, kLegendDim.r, kLegendDim.g, kLegendDim.b);
    }
  }

  // Strip plates.
  int shown = 0;
  int idx = 0;
  for (int i = 0; i < hub_.size() && shown < kVisible; ++i) {
    if (bus_ != 0 && static_cast<int>(hub_.bus(i)) != bus_) continue;
    if (idx++ < scroll_) continue;
    const double x = kStripX0 + shown * (kStripW + kStripGap);
    roundRect(cr, x, kStripTop, kStripW, kStripH, 6);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.14);
    cairo_fill(cr);
    setRgb(cr, {1, 1, 1}, 0.06);
    cairo_set_line_width(cr, 1.0);
    roundRect(cr, x + 0.5, kStripTop + 0.5, kStripW - 1, kStripH - 1, 6);
    cairo_stroke(cr);
    ++shown;
  }

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

  for (const Widget& w : widgets_) {
    switch (w.kind) {
      case Kind::View: {
        const bool on = view_ == static_cast<int>(w.value);
        pill(cr, w, w.label, on);
        break;
      }
      case Kind::Scroll:
        pill(cr, w, w.label, false);
        break;
      case Kind::Tab: {
        const bool on = std::fabs(shared.get(P_BUS) - w.value) < 0.5;
        pill(cr, w, w.label, on);
        break;
      }
      case Kind::Capture:
        pill(cr, w, "CAPTURE", false);
        break;
      case Kind::Rename:
        pill(cr, w, "REN", false);
        break;
      case Kind::Mute: {
        const int key = nameKey(w.index);
        pill(cr, w, "MUTE", shared.muted[key]);
        break;
      }
      case Kind::Solo: {
        const int key = nameKey(w.index);
        pill(cr, w, "SOLO", shared.soloed[key]);
        break;
      }
      case Kind::Strip: {
        const bool sel = state.selectedStrip == static_cast<int>(w.value);
        roundRect(cr, w.x, w.y, w.w, w.h, 4);
        setRgb(cr, sel ? Rgb{0.24, 0.21, 0.16} : Rgb{0.13, 0.15, 0.18});
        cairo_fill(cr);
        char buf[40];
        if (editing_ && editKey_ == nameKey(w.index)) {
          std::snprintf(buf, sizeof(buf), "%s_", editText_.c_str());
        } else {
          stripLabel(w.index, shared, buf, sizeof(buf));
        }
        text(cr, buf, w.x + w.w * 0.5, w.y + 17, 8.0, 1,
             sel ? 0.96 : kLegend.r, sel ? 0.86 : kLegend.g,
             sel ? 0.62 : kLegend.b);
        break;
      }
      case Kind::Knob: {
        char buf[40];
        const char* ro = busKnobText(w.param, shared.get(w.param), buf,
                                     sizeof(buf));
        knob(cr, w, shared.get(w.param), ro, w.palette);
        break;
      }
      case Kind::Remote: {
        const double v = hub_.value(w.index, w.plus);
        char buf[40];
        const char* ro = remoteText(w.plus, v, buf, sizeof(buf));
        knob(cr, w, v, ro, w.palette);
        break;
      }
      case Kind::RemotePill: {
        const double v = hub_.value(w.index, w.plus);
        const int count = enumCount(w.plus);
        if (count > 0) {
          pill(cr, w, enumName(w.plus, static_cast<int>(v + 0.5)), true);
        } else {
          pill(cr, w, w.label, v > 0.5);
        }
        break;
      }
      case Kind::RemoteButton: {
        const bool on = std::fabs(hub_.value(w.index, w.plus) - w.value) < 0.5;
        pill(cr, w, w.label, on);
        break;
      }
      case Kind::RemoteFader:
        fader(cr, w, hub_.value(w.index, w.plus));
        break;
      case Kind::Pill: {
        bool on = shared.get(w.param) > 0.5;
        const char* label = w.label;
        if (w.param == P_EQ_TYPE) {
          label = on ? "BLACK" : "BROWN";
          on = true;
        }
        pill(cr, w, label, on);
        break;
      }
      case Kind::Page:
        break;
    }
  }

  // Meters under the strip controls.
  shown = 0;
  idx = 0;
  for (int i = 0; i < hub_.size() && shown < kVisible; ++i) {
    if (bus_ != 0 && static_cast<int>(hub_.bus(i)) != bus_) continue;
    if (idx++ < scroll_) continue;
    const double x = kStripX0 + shown * (kStripW + kStripGap);
    float inL = 0, inR = 0, outL = 0, outR = 0, gr = 0;
    hub_.meter(i, &inL, &inR, &outL, &outR, &gr);
    if (view_ == 4) {
      meter(cr, x + 92, kStripTop + 70, 16, 400, outL, outR);
      text(cr, "OUT", x + 100, kStripTop + 484, 7.0, 1, kLegendDim.r,
           kLegendDim.g, kLegendDim.b);
    } else {
      meter(cr, x + 8, kStripTop + 336, 44, 160, inL, inR);
      meter(cr, x + 68, kStripTop + 336, 44, 160, outL, outR);
      text(cr, "IN", x + 30, kStripTop + 508, 7.5, 1, kLegendDim.r,
           kLegendDim.g, kLegendDim.b);
      text(cr, "OUT", x + 90, kStripTop + 508, 7.5, 1, kLegendDim.r,
           kLegendDim.g, kLegendDim.b);
      grMeter(cr, x + 8, kStripTop + 516, 104, 8, gr);
      text(cr, "GR", x + 8, kStripTop + 540, 7.0, 0, kLegendDim.r,
           kLegendDim.g, kLegendDim.b);
    }
    ++shown;
  }

  grMeter(cr, kBusX + 198, kPlateY + 60, 9, 168,
          shared.grDb.load(std::memory_order_relaxed), true);
  meter(cr, kBusX + 212, kPlateY + 60, 22, 168,
        shared.inPeakLDb.load(std::memory_order_relaxed),
        shared.outPeakLDb.load(std::memory_order_relaxed));
  text(cr, "GR", kBusX + 202, kPlateY + 240, 7, 1, kLegendDim.r,
       kLegendDim.g, kLegendDim.b);
  text(cr, "OUT", kBusX + 223, kPlateY + 240, 7, 1, kLegendDim.r,
       kLegendDim.g, kLegendDim.b);
}

void Panel::drawDetailPage(cairo_t* cr, const Shared& shared,
                           const PanelState& state) {
  if (detail_ < 0 || detail_ >= hub_.size()) {
    page_ = 0;
    return;
  }
  char title[96];
  stripLabel(detail_, shared, title, sizeof(title));
  text(cr, title, 700, 99, 15, 1, kLegend.r, kLegend.g, kLegend.b);

  // Same section grouping as Suite Channel, widened for the console.
  plate(cr, 20, 110, 280, 142, "INPUT / COLOUR");
  plate(cr, 20, 258, 280, 186, "DYNAMICS");
  plate(cr, 20, 452, 280, 180, "GATE / EXPANDER");
  plate(cr, 20, 640, 280, 104, "FILTERS");
  plate(cr, 310, 110, 670, 626, "EQUALISER");
  plate(cr, 990, 110, 274, 626, "OUTPUT");

  // Output meters.
  float inL = 0, inR = 0, outL = 0, outR = 0, gr = 0;
  hub_.meter(detail_, &inL, &inR, &outL, &outR, &gr);
  meter(cr, 1140, 190, 16, 430, outL, outR);
  grMeter(cr, 1168, 190, 10, 430, gr, true);
  text(cr, "OUT", 1148, 636, 7.5, 1, kLegendDim.r, kLegendDim.g, kLegendDim.b);
  text(cr, "GR", 1173, 636, 7.5, 1, kLegendDim.r, kLegendDim.g, kLegendDim.b);

  for (const Widget& w : widgets_) {
    switch (w.kind) {
      case Kind::Page: {
        if (w.value == 0.0) {
          pill(cr, w, "< MIX BUS", false);
        } else {
          char label[16];
          std::snprintf(label, sizeof(label), "%d", w.index + 1);
          pill(cr, w, label, w.index == detail_);
        }
        break;
      }
      case Kind::RemoteButton: {
        const bool on = std::fabs(hub_.value(w.index, w.plus) - w.value) < 0.5;
        pill(cr, w, w.label, on);
        break;
      }
      case Kind::RemotePill: {
        const double v = hub_.value(w.index, w.plus);
        const int count = enumCount(w.plus);
        if (count > 0) {
          pill(cr, w, enumName(w.plus, static_cast<int>(v + 0.5)), true);
        } else {
          pill(cr, w, w.label, v > 0.5);
        }
        break;
      }
      case Kind::RemoteFader:
        fader(cr, w, hub_.value(w.index, w.plus));
        break;
      case Kind::Remote: {
        const double v = hub_.value(w.index, w.plus);
        char buf[40];
        const char* ro = remoteText(w.plus, v, buf, sizeof(buf));
        knob(cr, w, v, ro, w.palette);
        break;
      }
      default:
        break;
    }
  }
}

} // namespace suite
