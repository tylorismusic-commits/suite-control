// Suite Control — panel.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cairo.h>
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <string>
#include <vector>

#include "params.hpp"
#include "peer.hpp"

namespace suite {

constexpr int kPanelWidth = 1280;
constexpr int kPanelHeight = 760;

// Suite Channel Plus parameter indices this surface mirrors.  The contract is
// fixed by that plug-in's appended-only parameter table.
namespace ch {
constexpr int kEnabled = 0;
constexpr int kInput = 1;
constexpr int kEqIn = 8;
constexpr int kCompIn = 22;
constexpr int kCompThreshold = 23;
constexpr int kCompMakeup = 24;
constexpr int kOutput = 34;
constexpr int kBus = 38;
constexpr int kCount = 39;
} // namespace ch

struct PanelState {
  int selectedStrip = 0;
  int dragKind = -1;   // 0 = local param, 1 = remote param
  int dragParam = -1;
  int dragSlot = -1;
  int dragPlus = -1;
  double dragY = 0.0;
  double dragStart = 0.0;
  bool fine = false;
  float peakHold[8][2] = {};
};

class Panel {
public:
  Panel();
  ~Panel();
  Panel(const Panel&) = delete;
  Panel& operator=(const Panel&) = delete;

  void loadFont(const std::string& dir);
  void tick(const Shared& shared, PanelState& state);
  void draw(cairo_t* cr, const Shared& shared, const PanelState& state);

  bool pointerDown(Shared& shared, PanelState& state, double x, double y,
                   bool fine);
  bool pointerMove(Shared& shared, PanelState& state, double x, double y,
                   bool fine);
  void pointerUp();
  void wheel(Shared&, PanelState&, double, double, double) {}
  void beginTextEntry(Shared&, PanelState&, double, double) {}
  void textKey(Shared&, PanelState&, int, unsigned) {}
  void importDroppedFile(Shared&, PanelState&, const std::string&) {}

  int stripCount() const { return static_cast<int>(strips_.size()); }

  static constexpr int kTextEnter = -2;
  static constexpr int kTextEscape = -1;
  static constexpr int kTextBackspace = -3;

private:
  enum class Kind { Knob, Remote, Pill, Tab, Capture, Strip };

  struct Widget {
    Kind kind = Kind::Knob;
    uint32_t param = 0;
    int slot = -1;
    int plus = -1;
    double x = 0.0, y = 0.0, r = 0.0, w = 0.0, h = 0.0;
    const char* label = "";
    double value = 0.0;
  };

  struct StripRef {
    int slot = -1;
    uint32_t serial = 0;
  };

  void layout(const Shared& shared, const PanelState& state);
  void refreshStrips();
  void text(cairo_t* cr, const char* s, double x, double y, double size,
            int align, double r, double g, double b) const;
  void knob(cairo_t* cr, const Widget& w, double value, const char* readout,
            bool active) const;
  void pill(cairo_t* cr, const Widget& w, const char* label, bool on) const;
  void meter(cairo_t* cr, double x, double y, double w, double h, float dbL,
             float dbR) const;
  void grMeter(cairo_t* cr, double x, double y, double w, double h,
               float grDb) const;
  double slotValue(const StripRef& s, int plus) const;
  void writeSlot(const StripRef& s, int plus, double v);

  std::vector<Widget> widgets_;
  std::vector<StripRef> strips_;
  peer::Client peer_;
  unsigned long lastSerial_ = 0;

  FT_Library ft_ = nullptr;
  FT_Face ftFace_ = nullptr;
  cairo_font_face_t* font_ = nullptr;
  cairo_font_face_t* fallbackFont_ = nullptr;
};

} // namespace suite
