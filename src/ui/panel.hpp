// Suite Control — panel.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cairo.h>
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <functional>
#include <string>
#include <vector>

#include "params.hpp"
#include "peer.hpp"
#include "ui/transport.hpp"

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

// Controls in the OSC strip view.
constexpr int kHostVolume = 0;
constexpr int kHostPan = 1;
constexpr int kHostMute = 2;
constexpr int kHostSolo = 3;
constexpr int kHostRec = 4;
constexpr int kHostSelect = 5;

constexpr int kHostTracks = 8;

// One host track, filled by the OSC glue (REAPER or Ardour).
struct HostTrack {
  char name[40] = {};
  char gain[16] = {};
  double volume = 0.0;
  double pan = 0.5;
  bool mute = false;
  bool solo = false;
  bool rec = false;
  bool valid = false;
  float vuL = -120.0f;
  float vuR = -120.0f;
};

struct PanelState {
  int selectedStrip = 0;
  bool fine = false;
  std::function<void(int action)> transport;
  bool playing = false;
  bool recording = false;
  bool loop = false;
  bool osc = false;
  char time[16] = {};

  // The OSC view mirrors the host's tracks; the glue keeps this current.
  HostTrack host[kHostTracks];
  int hostBank = 0;
  int oscSendPort = 0;
  int oscRecvPort = 0;
  int oscProtocol = 0;  // 0 REAPER, 1 Ardour
  std::function<void(int send, int recv)> oscConfigure;
  std::function<void()> oscSync;
  std::function<void(int index, int control, double value)> hostControl;
  std::function<void(int index)> hostSelect;
  std::function<void(int delta)> hostBankChange;
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
  void wheel(Shared& shared, PanelState& state, double x, double y, double d,
             bool fine);
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
  static constexpr int kTextTab = -4;

private:
  enum class Kind {
    Knob, Remote, RemotePill, RemoteFader, Pill, Tab, Capture, Strip, Rename,
    Page, View, Scroll, Mute, Solo, RemoteButton,
    InsertCell, InsertTab, InsertKnob, InsertPill, PickerItem, PickerCancel,
    Transport, OscStrip, OscKnob, OscFader, OscPill, OscHide, OscSetup
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
  void stripLabel(int i, const Shared& shared, char* buf, size_t n) const;
  int nameKey(int i) const;
  // 1-based position of a strip within its bus (the channel number).
  int channelOf(int i) const;
  // Hub entry of the insert assigned to (strip i, slot 1..4), or -1.
  int linkedInsert(int i, int slot) const;
  void openPicker(int strip, int slot);
  void layoutPicker();
  void layoutInsertPage();
  void drawInsertPage(cairo_t* cr, const Shared& shared);
  void drawInsertCurve(cairo_t* cr, int entry);
  void drawPicker(cairo_t* cr);
  void writeInsert(int entry, uint32_t param, double normalised);
  double insertNormalised(int entry, uint32_t param) const;
  void applyMuteSolo(Shared& shared);
  void layoutHostStrips(const PanelState& state);
  void drawHostStrips(cairo_t* cr, const PanelState& state);
  const Widget* hitTest(double x, double y) const;
  void resetWidget(Shared& shared, const Widget& w);
  bool sameEditTarget(const Widget& w) const;
  void closeEdit();
  void meter(cairo_t* cr, double x, double y, double w, double h, float dbL,
             float dbR) const;
  void grMeter(cairo_t* cr, double x, double y, double w, double h,
               float grDb, bool vertical = false) const;

  bool dragging_ = false;
  int dragKind_ = -1;  // 0 local, 1 remote
  int dragParam_ = -1;
  int dragIndex_ = -1;
  int dragPlus_ = -1;
  double dragY_ = 0.0;
  double dragStart_ = 0.0;
  double dragValue_ = 0.0;  // shown while dragging, before the peer publishes

  void drawBusPage(cairo_t* cr, const Shared& shared, const PanelState& state);
  void drawDetailPage(cairo_t* cr, const Shared& shared, const PanelState& state);
  void layoutBus(const Shared& shared, const PanelState& state);
  void layoutDetail();

  std::vector<Widget> widgets_;
  peer::Hub hub_;
  int bus_ = 1;
  int page_ = 0;       // 0 = mix bus, 1 = selected channel
  int view_ = 0;       // 0 mix, 1 input, 2 eq, 3 dynamics, 4 output, 5 OSC
  int scroll_ = 0;     // first visible strip
  int detail_ = -1;    // hub index for the channel page
  int insertSlot_ = 0; // 0 = CHANNEL tab, 1..4 = insert tab
  int insertScroll_ = 0;
  int insertMaxScroll_ = 0;
  bool insertCurve_ = false;
  bool insertGr_ = false;
  bool pickerOpen_ = false;
  int pickerBus_ = 0;
  int pickerChannel_ = 0;
  int pickerSlot_ = 0;
  std::vector<int> pickerEntries_;
  struct InsertHeader {
    std::string text;
    double x = 0.0;
    double y = 0.0;
  };
  std::vector<InsertHeader> insertHeaders_;
  double pickerX_ = 360.0, pickerY_ = 190.0;
  double pickerW_ = 560.0, pickerH_ = 300.0;
  bool editing_ = false;
  int editKind_ = 0;   // 0 name, 1 local param, 2 remote param
  int editKey_ = -1;
  int editParam_ = -1;
  int editIndex_ = -1;
  int editPlus_ = -1;
  uint64_t lastClickId_ = 0;
  double lastClickMs_ = 0.0;
  std::string editText_;

  double captureNoticeUntil_ = 0.0;
  int captureCount_ = -1;

  // The OSC row can be collapsed to a small chip.
  bool oscHidden_ = false;
  int flashAction_ = -1;
  double flashUntil_ = 0.0;
  double transportX0_ = 0.0;
  double transportRowW_ = 0.0;
  bool transportChip_ = false;
  double timeX_ = 0.0;
  double timeW_ = 88.0;

  struct Rect {
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
    bool hit(double px, double py) const {
      return px >= x && px <= x + w && py >= y && py <= y + h;
    }
  };
  bool oscSettings_ = false;
  Rect oscSendRect_;
  Rect oscRecvRect_;
  Rect oscSyncRect_;
  Rect oscCloseRect_;
  int editPortKind_ = -1;  // 0 send, 1 receive
  // Keep a just-set host control until its feedback arrives; OSC state lags
  // behind the drag, especially while the transport is running.
  double oscHoldVolume_[kHostTracks] = {};
  double oscHoldPan_[kHostTracks] = {};
  double oscHoldVolumeUntil_[kHostTracks] = {};
  double oscHoldPanUntil_[kHostTracks] = {};
  // Non-motorized faders: the host value is adopted once, then the strip
  // only moves when the hand moves it.  SYNC re-reads the host.
  double oscLocalVolume_[kHostTracks] = {};
  bool oscLocalValid_[kHostTracks] = {};
  bool oscGrab_[kHostTracks] = {};
  int oscLocalBank_ = -1;
  double oscSyncUntil_ = 0.0;
  int editSend_ = 0;
  int editRecv_ = 0;
  void oscSettingsGeometry();
  void drawOscSettings(cairo_t* cr, const PanelState& state);
  bool pointerDownOscSettings(Shared& shared, PanelState& state, double x,
                              double y);

  FT_Library ft_ = nullptr;
  FT_Face ftFace_ = nullptr;
  cairo_font_face_t* font_ = nullptr;
  cairo_font_face_t* fallbackFont_ = nullptr;
};

} // namespace suite
