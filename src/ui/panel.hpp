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

// Suite Channel Plus parameter indices this surface mirrors.
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
  bool fine = false;
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
  void wheel(Shared& shared, PanelState& state, double x, double y, double d);
  bool beginTextEntry(Shared& shared, PanelState& state, double x, double y);
  void textKey(Shared& shared, PanelState& state, int code, unsigned ch);
  void importDroppedFile(Shared&, PanelState&, const std::string&) {}

  int stripCount() const {
    int n = 0;
    for (int i = 0; i < hub_.size(); ++i) {
      if (bus_ == 0 || static_cast<int>(hub_.bus(i)) == bus_) ++n;
    }
    return n;
  }
  bool editing() const { return editing_; }

  static constexpr int kTextEnter = -2;
  static constexpr int kTextEscape = -1;
  static constexpr int kTextBackspace = -3;

private:
  enum class Kind {
    Knob, Remote, RemotePill, RemoteFader, Pill, Tab, Capture, Strip, Rename,
    Page, View, Scroll
  };


  struct Widget {
    Kind kind = Kind::Knob;
    uint32_t param = 0;
    int index = -1;   // hub entry for remote widgets
    int plus = -1;
    double x = 0.0, y = 0.0, r = 0.0, w = 0.0, h = 0.0;
    const char* label = "";
    int palette = 0;  // knob colour group
    double value = 0.0;
  };

  void layout(const Shared& shared, const PanelState& state);
  void text(cairo_t* cr, const char* s, double x, double y, double size,
            int align, double r, double g, double b) const;
  void knob(cairo_t* cr, const Widget& w, double value, const char* readout,
            int palette) const;
  void pill(cairo_t* cr, const Widget& w, const char* label, bool on) const;
  void plate(cairo_t* cr, double x, double y, double w, double h,
             const char* title) const;
  void fader(cairo_t* cr, const Widget& w, double value) const;
  int visibleCount() const;
  int maxScroll() const;
  void stripLabel(int i, char* buf, size_t n) const;
  void meter(cairo_t* cr, double x, double y, double w, double h, float dbL,
             float dbR) const;
  void grMeter(cairo_t* cr, double x, double y, double w, double h,
               float grDb) const;

  bool dragging_ = false;
  int dragKind_ = -1;  // 0 local, 1 remote
  int dragParam_ = -1;
  int dragIndex_ = -1;
  int dragPlus_ = -1;
  double dragY_ = 0.0;
  double dragStart_ = 0.0;

  void drawBusPage(cairo_t* cr, const Shared& shared, const PanelState& state);
  void drawDetailPage(cairo_t* cr, const Shared& shared, const PanelState& state);
  void layoutBus(const Shared& shared, const PanelState& state);
  void layoutDetail();

  std::vector<Widget> widgets_;
  peer::Hub hub_;
  int bus_ = 1;
  int page_ = 0;       // 0 = mix bus, 1 = selected channel
  int view_ = 0;       // 0 mix, 1 input, 2 eq, 3 dynamics, 4 output
  int scroll_ = 0;     // first visible strip
  int detail_ = -1;    // hub index for the channel page
  bool editing_ = false;
  int editIndex_ = -1;
  std::string editText_;
  double captureNoticeUntil_ = 0.0;
  int captureCount_ = -1;

  FT_Library ft_ = nullptr;
  FT_Face ftFace_ = nullptr;
  cairo_font_face_t* font_ = nullptr;
  cairo_font_face_t* fallbackFont_ = nullptr;
};

} // namespace suite
