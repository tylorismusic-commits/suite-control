// Suite Channel — CLAP plugin
// SPDX-License-Identifier: GPL-3.0-or-later

#include <clap/all.h>
#include <clap/ext/configurable-audio-ports.h>

#ifndef SUITE_MACOS_COCOA
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <cairo-xlib.h>
#endif
#include <dlfcn.h>
#include <sys/time.h>

#include <atomic>
#include <vector>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <cstdarg>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "dsp/engine.hpp"
#include "params.hpp"
#include "peer.hpp"
#include "version.hpp"
#include "ui/geometry.hpp"
#include "ui/panel.hpp"
#ifndef SUITE_MACOS_COCOA
#include "ui/xdnd.hpp"
#else
#include <memory>

#include "ui/cocoa_view.hpp"
#endif

namespace suite {
namespace {

// Set SUITE_DEBUG=1 to trace the editor thread to /tmp/suitechannel-ui.log.
bool debugOn() {
  static const bool on = std::getenv("SUITE_DEBUG") != nullptr;
  return on;
}

void dbg(const char* fmt, ...) {
  if (!debugOn()) {
    return;
  }
  FILE* f = std::fopen("/tmp/suitechannel-ui.log", "a");
  if (!f) {
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(f, fmt, ap);
  va_end(ap);
  std::fputc('\n', f);
  std::fclose(f);
}

constexpr const char* kPluginId = "com.tylorismusic.suitechannelplus";
constexpr const char* kPluginName = "Suite Channel Plus";
constexpr const char* kVendor = "tylorismusic";

std::string bundleResourceDir() {
  Dl_info info{};
  if (dladdr(reinterpret_cast<void*>(&bundleResourceDir), &info) && info.dli_fname) {
    std::string path = info.dli_fname;
    const size_t slash = path.rfind('/');
    if (slash != std::string::npos) {
      path = path.substr(0, slash);
    }
#ifdef SUITE_MACOS_COCOA
    // Resources live in Contents/Resources, next to Contents/MacOS.
    return path + "/../Resources/SuiteChannel.resources";
#else
    return path + "/SuiteChannel.resources";
#endif
  }
  return "SuiteChannel.resources";
}

// ---------------------------------------------------------------------------
// X11 editor.  Owns the window and the Cairo surface, and runs its own
// thread so the host's audio and main threads are never blocked on drawing.
// ---------------------------------------------------------------------------

#ifdef SUITE_MACOS_COCOA
// macOS editor: the panel lives in a Cocoa NSView created by the host.
class Editor {
public:
  explicit Editor(Shared& shared) : shared_(shared) {}

  std::function<void()> requestFlush;

  double scale() const { return scale_.load(std::memory_order_relaxed); }
  UiSize size() const { return sizeForScale(scale()); }

  void setScale(double scale) {
    scale_.store(clampScale(scale), std::memory_order_relaxed);
  }

  bool open(void* parent) {
    if (!cocoa_) {
      panel_.loadFont(bundleResourceDir());
      cocoa_ = std::make_unique<CocoaView>();
      cocoa_->setContent(&panel_, &shared_, &state_);
    }
    if (parent) cocoa_->attach(parent);
    return true;
  }

  void close() { cocoa_.reset(); }

  void resize(double scale) {
    scale_.store(clampScale(scale), std::memory_order_relaxed);
    if (cocoa_) {
      const UiSize s = size();
      cocoa_->resize(static_cast<int>(s.width), static_cast<int>(s.height));
    }
  }

  ~Editor() { close(); }

private:
  Shared& shared_;
  PanelState state_;
  Panel panel_;
  std::atomic<double> scale_{1.0};
  std::unique_ptr<CocoaView> cocoa_;
};
#else
// Fit the 700x920 panel into the current screen by default, leaving room for
// the host's window decorations and a bottom transport/dock.
double fitScaleForScreen() {
  Display* dpy = XOpenDisplay(nullptr);
  if (!dpy) {
    return 0.9;
  }
  const int screen = DefaultScreen(dpy);
  const int screenH = DisplayHeight(dpy, screen);
  XCloseDisplay(dpy);
  const double fit = (screenH - 140.0) / static_cast<double>(kPanelHeight);
  return clampScale(std::min(1.0, std::max(kMinUiScale, fit)));
}

class Editor {
public:
  explicit Editor(Shared& shared) : shared_(shared) {}

  // Called when the user changes a control, so the host can ask for a
  // process call and pick up the new values.
  std::function<void()> requestFlush;

  double scale() const { return scale_.load(std::memory_order_relaxed); }
  UiSize size() const { return sizeForScale(scale()); }

  void setScale(double scale) {
    scale_.store(clampScale(scale), std::memory_order_relaxed);
  }

  // Host thread: record the request only.  All Xlib calls happen on the
  // editor thread, because Xlib is not thread-safe and calling
  // XInitThreads() after the host has used Xlib is undefined behaviour
  // (that made the editor fail to load in REAPER).
  void resize(double scale) {
    scale_.store(clampScale(scale), std::memory_order_relaxed);
    resizePending_.store(true, std::memory_order_relaxed);
  }

  bool open(void* parent) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (thread_.joinable()) {
      return opened_;
    }
    parentWindow_ =
        parent ? static_cast<Window>(reinterpret_cast<uintptr_t>(parent)) : 0;
    opened_ = false;
    openFailed_ = false;
    quitting_ = false;
    thread_ = std::thread([this] { run(); });
    cv_.wait_for(lock, std::chrono::seconds(3),
                 [this] { return opened_ || openFailed_; });
    return opened_;
  }

  void close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      quitting_ = true;
    }
    if (thread_.joinable()) {
      thread_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    opened_ = false;
  }

  ~Editor() { close(); }

private:
  void run() {
    dbg("thread start");
    Display* dpy = XOpenDisplay(nullptr);
    dbg("display=%p", static_cast<void*>(dpy));
    if (!dpy) {
      std::lock_guard<std::mutex> lock(mutex_);
      openFailed_ = true;
      cv_.notify_all();
      return;
    }
    const int screen = DefaultScreen(dpy);
    const Window parent = parentWindow_ ? parentWindow_ : RootWindow(dpy, screen);
    UiSize current = size();

    XSetWindowAttributes attrs{};
    // Button1MotionMask is essential: PointerMotionMask only reports motion
    // with no button held, so dragging a knob would never deliver events.
    attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                       PointerMotionMask | Button1MotionMask | ButtonMotionMask |
                       KeyPressMask;
    const Window win =
        XCreateWindow(dpy, parent, 0, 0, current.width, current.height, 0,
                      CopyFromParent, InputOutput, CopyFromParent, CWEventMask,
                      &attrs);
    xdnd_.init(dpy, win, [this](const std::string& path) {
      panel_.importDroppedFile(shared_, state_, path);
    });

    const UiSize minSize = sizeForScale(kMinUiScale);
    const UiSize maxSize = sizeForScale(kMaxUiScale);
    XSizeHints hints{};
    hints.flags = PAspect | PMinSize | PMaxSize;
    hints.min_aspect.x = hints.max_aspect.x = kPanelWidth;
    hints.min_aspect.y = hints.max_aspect.y = kPanelHeight;
    hints.min_width = static_cast<int>(minSize.width);
    hints.min_height = static_cast<int>(minSize.height);
    hints.max_width = static_cast<int>(maxSize.width);
    hints.max_height = static_cast<int>(maxSize.height);
    XSetWMNormalHints(dpy, win, &hints);
    XStoreName(dpy, win, kPluginName);
    XMapRaised(dpy, win);
    XFlush(dpy);

    panel_.loadFont(bundleResourceDir());
    {
      std::lock_guard<std::mutex> lock(mutex_);
      display_ = dpy;
      window_ = win;
      opened_ = true;
      cv_.notify_all();
    }

    // Use the window's own visual and depth.  Hosts often give the plugin a
    // parent with a non-default visual (ARGB), and cairo refuses to draw on a
    // surface whose visual does not match the drawable.
    XWindowAttributes wa{};
    XGetWindowAttributes(dpy, win, &wa);
    dbg("window=%lu depth=%d visual=%p size=%dx%d", win, wa.depth,
        static_cast<void*>(wa.visual), current.width, current.height);
    cairo_surface_t* windowSurface =
        cairo_xlib_surface_create(dpy, win, wa.visual, current.width, current.height);
    dbg("xlib surface status=%s",
        cairo_status_to_string(cairo_surface_status(windowSurface)));
    const cairo_format_t imageFormat =
        wa.depth == 32 ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24;
    cairo_surface_t* backing = nullptr;
    cairo_t* cr = nullptr;

    auto rebuildBacking = [&]() {
      if (cr) {
        cairo_destroy(cr);
        cr = nullptr;
      }
      if (backing) {
        cairo_surface_destroy(backing);
        backing = nullptr;
      }
      backing = cairo_image_surface_create(imageFormat, current.width,
                                           current.height);
      cr = cairo_create(backing);
    };
    rebuildBacking();

    while (true) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (quitting_) {
          break;
        }
      }
      const UiSize want = size();
      if (want.width != current.width || want.height != current.height) {
        current = want;
        XResizeWindow(dpy, win, current.width, current.height);
        cairo_xlib_surface_set_size(windowSurface, current.width, current.height);
        rebuildBacking();
      }
      while (XPending(dpy) > 0) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        handleEvent(ev);
      }
      panel_.tick(shared_, state_);
      cairo_save(cr);
      cairo_scale(cr, scale(), scale());
      panel_.draw(cr, shared_, state_);
      cairo_restore(cr);
      cairo_surface_flush(backing);
      cairo_t* xcr = cairo_create(windowSurface);
      cairo_set_source_surface(xcr, backing, 0, 0);
      cairo_pattern_set_filter(cairo_get_source(xcr), CAIRO_FILTER_NEAREST);
      cairo_paint(xcr);
      cairo_destroy(xcr);
      // Required: cairo's Xlib backend buffers the composite, and without
      // this flush nothing reaches the server (the window stays black).
      cairo_surface_flush(windowSurface);
      XFlush(dpy);
      static int frames = 0;
      if (debugOn() && (frames++ % 120) == 0) {
        XWindowAttributes ga;
        XGetWindowAttributes(dpy, win, &ga);
        dbg("frame %d geom=%dx%d mapped=%d", frames, ga.width, ga.height,
            ga.map_state == IsViewable);
      }
      if (requestFlush && shared_.anyDirty()) {
        requestFlush();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    if (cr) {
      cairo_destroy(cr);
    }
    if (backing) {
      cairo_surface_destroy(backing);
    }
    if (windowSurface) {
      cairo_surface_destroy(windowSurface);
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      display_ = nullptr;
      window_ = 0;
    }
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
  }

  // Event coordinates are physical pixels; the panel works in logical units.
  void handleEvent(const XEvent& ev) {
    if (xdnd_.handle(ev)) {
      return;
    }
    const double s = scale();
    switch (ev.type) {
      case ButtonPress: {
        // X only delivers KeyPress to the focused window. Hosts do not always
        // focus plugin windows on a right-click, so take focus here.
        if (display_ && window_) {
          XSetInputFocus(display_, window_, RevertToParent, CurrentTime);
          XFlush(display_);
        }
        const bool fine = (ev.xbutton.state & ShiftMask) != 0;
        const double x = ev.xbutton.x / s;
        const double y = ev.xbutton.y / s;
        if (ev.xbutton.button == Button4) {
          panel_.wheel(shared_, state_, x, y, 1.0);
        } else if (ev.xbutton.button == Button5) {
          panel_.wheel(shared_, state_, x, y, -1.0);
        } else if (ev.xbutton.button == Button1) {
          panel_.pointerDown(shared_, state_, x, y, fine);
        } else if (ev.xbutton.button == Button3) {
          panel_.beginTextEntry(shared_, state_, x, y);
        }
        break;
      }
      case ButtonRelease:
        panel_.pointerUp();
        break;
      case KeyPress: {
        char buf[8] = {};
        KeySym sym = 0;
        XKeyEvent key = ev.xkey;
        const int n = XLookupString(&key, buf, sizeof(buf), &sym, nullptr);
        if (sym == XK_Return || sym == XK_KP_Enter) {
          panel_.textKey(shared_, state_, Panel::kTextEnter, 0);
        } else if (sym == XK_Escape) {
          panel_.textKey(shared_, state_, Panel::kTextEscape, 0);
        } else if (sym == XK_BackSpace) {
          panel_.textKey(shared_, state_, Panel::kTextBackspace, 0);
        } else if (n > 0) {
          panel_.textKey(shared_, state_, 0, buf[0]);
        }
        break;
      }
      case MotionNotify: {
        const bool fine = (ev.xmotion.state & ShiftMask) != 0;
        panel_.pointerMove(shared_, state_, ev.xmotion.x / s, ev.xmotion.y / s,
                           fine);
        break;
      }
      default:
        break;
    }
  }

  Shared& shared_;
  PanelState state_;
  Panel panel_;
  XdndDropTarget xdnd_;
  std::atomic<double> scale_{1.0};
  std::atomic<bool> resizePending_{false};
  std::mutex mutex_;
  std::condition_variable cv_;
  Display* display_ = nullptr;
  Window window_ = 0;
  Window parentWindow_ = 0;
  bool opened_ = false;
  bool openFailed_ = false;
  bool quitting_ = false;
  std::thread thread_;
};
#endif

// ---------------------------------------------------------------------------
// Plugin
// ---------------------------------------------------------------------------

// Audio ports the configurable-ports bookkeeping covers.  The plug-in's main
// input and output come first; the array leaves room for extra busses such as
// a sidechain.
constexpr uint32_t kMaxPorts = 4;

struct Plugin {
  clap_plugin_t plugin{};
  const clap_host_t* host = nullptr;
  Shared shared;
  Engine engine;
  Editor editor{shared};
  double sampleRate = 48000.0;
  Meters meters;
  std::atomic<float> grForHost{0.0f};
  // The host caches parameter flags, so a model change is announced with a
  // rescan.  rescan() is main-thread only, so audio-thread changes queue a
  // main-thread callback.
  std::atomic<int> infoModel{-1};
  std::atomic<int> reportedOversample{-1};
  std::atomic<uint32_t> reportedLatency{0xffffffffu};
  std::atomic<bool> rescanQueued{false};
  std::atomic<bool> active{false};
  // Channel width per audio port.  Logic hides a stereo-only effect on mono
  // tracks, and clap-wrapper only learns about alternate layouts through
  // clap.configurable-audio-ports, so the busses can be narrowed here.
  std::atomic<uint32_t> inChannels[kMaxPorts] = {2, 2, 2, 2};
  std::atomic<uint32_t> outChannels[kMaxPorts] = {2, 2, 2, 2};
  // Second channel for a mono host: the engine keeps per-channel state, so
  // aliasing both sides onto one buffer would process every sample twice.
  std::vector<float> monoRight;
  peer::Client peer;
  bool peerRegistered = false;
};

const char* const kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
                                 CLAP_PLUGIN_FEATURE_COMPRESSOR,
                                 CLAP_PLUGIN_FEATURE_EQUALIZER, nullptr};

void writeName(char* dst, size_t size, const char* src) {
  std::strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
}

Plugin* self(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

// --- Parameters -------------------------------------------------------------

uint32_t paramsCount(const clap_plugin_t*) { return P_COUNT; }

bool paramsInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
  if (index >= P_COUNT) {
    return false;
  }
  const ParamSpec& s = kParamSpecs[index];
  std::memset(info, 0, sizeof(*info));
  info->id = index;
  info->flags = CLAP_PARAM_IS_AUTOMATABLE;
  if (s.stepped) {
    info->flags |= CLAP_PARAM_IS_STEPPED;
  }
  if (s.hidden) {
    info->flags |= CLAP_PARAM_IS_HIDDEN;
  }
  writeName(info->name, sizeof(info->name), s.name);
  writeName(info->module, sizeof(info->module), s.module);
  info->min_value = s.min;
  info->max_value = s.max;
  info->default_value = s.def;
  return true;
}

bool paramsValue(const clap_plugin_t* p, clap_id id, double* value) {
  if (id >= P_COUNT) {
    return false;
  }
  *value = self(p)->shared.get(id);
  return true;
}

// Hosts show these strings in their generic parameter lists. The editor uses
// the same formatter, so both always report the same option names.
bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
                       char* out, uint32_t size) {
  if (id >= P_COUNT) {
    return false;
  }
  const std::string text = formatParamValue(id, value);
  std::snprintf(out, size, "%s", text.c_str());
  return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* text,
                       double* value) {
  if (!text || id >= P_COUNT) {
    return false;
  }
  return parseParamValue(id, text, value);
}

void applyEvents(Plugin* s, const clap_input_events_t* in) {
  const uint32_t count = in->size(in);
  for (uint32_t i = 0; i < count; ++i) {
    const clap_event_header_t* hdr = in->get(in, i);
    if (hdr->space_id != CLAP_CORE_EVENT_SPACE_ID) {
      continue;
    }
    if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
      const auto* pv = reinterpret_cast<const clap_event_param_value_t*>(hdr);
      // A pending UI change wins over the host's cached value: hosts that
      // resend their whole parameter set each block would otherwise undo a
      // preset (or a knob move) before we could send it back.
      if (pv->param_id < P_COUNT && !s->shared.isDirty(pv->param_id)) {
        s->shared.setFromHost(pv->param_id, pv->value);
      }
    }
  }
}

// Sends the values the editor changed back to the host, so they can be
// automated and recorded.
void emitDirty(Plugin* s, const clap_output_events_t* out) {
  if (!out) {
    return;
  }
  for (uint32_t p = 0; p < P_COUNT; ++p) {
    if (!s->shared.takeDirty(p)) {
      continue;
    }
    clap_event_param_value_t ev{};
    ev.header.size = sizeof(ev);
    ev.header.time = 0;
    ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.header.type = CLAP_EVENT_PARAM_VALUE;
    ev.header.flags = 0;
    ev.param_id = p;
    ev.note_id = -1;
    ev.port_index = -1;
    ev.channel = -1;
    ev.key = -1;
    ev.value = s->shared.get(p);
    out->try_push(out, &ev.header);
  }
}

// Queue a parameter-info rescan if the model changed since the last one.
// Safe on the audio thread: it only sets a flag and asks the host for a
// main-thread callback.
void noteHostChanges(Plugin* s) {
  const int os = static_cast<int>(s->shared.get(P_OVERSAMPLE) + 0.5);
  const bool latencyChanged =
      os != s->reportedOversample.load(std::memory_order_relaxed);
  if (!latencyChanged) {
    return;
  }
  if (!s->rescanQueued.exchange(true, std::memory_order_relaxed)) {
    if (s->host && s->host->request_callback) {
      s->host->request_callback(s->host);
    }
  }
}

void paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in,
                 const clap_output_events_t* out) {
  Plugin* s = self(p);
  if (in) {
    applyEvents(s, in);
  }
  emitDirty(s, out);
  noteHostChanges(s);
}

const clap_plugin_params_t kParamsExt = {
    paramsCount, paramsInfo, paramsValue, paramsValueToText, paramsTextToValue,
    paramsFlush,
};

const clap_plugin_descriptor_t kDescriptor = {
    CLAP_VERSION,
    kPluginId,
    kPluginName,
    kVendor,
    "https://github.com/tylorismusic-commits/suite-channel-plus",
    "",
    "",
    kVersion,
    "Vertical SSL-style channel strip: switchable VCA/76Blue compressor, expander/gate, four-band console EQ and five colour voicings.",
    kFeatures,
};

// --- State ------------------------------------------------------------------

bool stateSave(const clap_plugin_t* p, const clap_ostream_t* stream) {
  Plugin* s = self(p);
  double values[P_COUNT];
  for (uint32_t i = 0; i < P_COUNT; ++i) {
    values[i] = s->shared.get(i);
  }
  const double scale = s->editor.scale();
  const int64_t written = stream->write(stream, values, sizeof(values));
  const int64_t writtenScale = stream->write(stream, &scale, sizeof(scale));
  return written == static_cast<int64_t>(sizeof(values)) &&
         writtenScale == static_cast<int64_t>(sizeof(scale));
}

bool stateLoad(const clap_plugin_t* p, const clap_istream_t* stream) {
  Plugin* s = self(p);
  double values[P_COUNT] = {};
  const int64_t got = stream->read(stream, values, sizeof(values));
  // Appended parameters mean older states are shorter, so load whatever is
  // present and take defaults for the rest.  This covers 0.1.x (36 values),
  // 0.3.0 (38) and Plus (39) without a version field.
  if (got <= 0 || got % static_cast<int64_t>(sizeof(double)) != 0) {
    return false;
  }
  const uint32_t count = static_cast<uint32_t>(
      std::min<int64_t>(got / static_cast<int64_t>(sizeof(double)), P_COUNT));
  for (uint32_t i = 0; i < count; ++i) {
    s->shared.setFromHost(i, values[i]);
  }
  for (uint32_t i = count; i < P_COUNT; ++i) {
    s->shared.setFromHost(i, kParamSpecs[i].def);
  }
  noteHostChanges(s);
  // The UI scale is optional, so states saved before it still load.
  double scale = 0.0;
  if (stream->read(stream, &scale, sizeof(scale)) == static_cast<int64_t>(sizeof(scale)) &&
      scale > 0.0) {
    s->editor.resize(scale);
  }
  return true;
}

const clap_plugin_state_t kStateExt = {stateSave, stateLoad};

// --- Audio ports ------------------------------------------------------------

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) {
  return isInput ? 2 : 1;
}

bool audioPortsInfo(const clap_plugin_t* p, uint32_t index, bool isInput,
                    clap_audio_port_info_t* info) {
  if (isInput) {
    if (index > 1) {
      return false;
    }
  } else if (index != 0) {
    return false;
  }
  const Plugin* s = self(p);
  const uint32_t channels =
      s ? (isInput ? s->inChannels[index] : s->outChannels[index])
              .load(std::memory_order_relaxed)
        : 2u;
  std::memset(info, 0, sizeof(*info));
  info->channel_count = channels;
  info->port_type = channels == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;

  if (isInput) {
    if (index == 0) {
      info->id = 0;
      writeName(info->name, sizeof(info->name), "Input");
      info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    } else {
      info->id = 2;
      writeName(info->name, sizeof(info->name), "Sidechain");
      info->flags = 0;
    }
    return true;
  }
  info->id = 1;
  writeName(info->name, sizeof(info->name), "Output");
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  return true;
}

const clap_plugin_audio_ports_t kAudioPortsExt = {audioPortsCount, audioPortsInfo};

// Mono and stereo main busses.  A host that only offers effects matching the
// track width (Logic) hides a stereo-only plug-in on mono tracks, and
// clap-wrapper discovers alternate layouts only through this extension: it
// probes every width pair once per instance and advertises what the plug-in
// accepts.  Extra busses may follow the main pair; the DSP reads channel_count
// from the buffer, so it copes with either width.
bool configurableCanApply(
    const clap_plugin_t* p,
    const clap_audio_port_configuration_request_t* requests, uint32_t count) {
  const Plugin* s = self(p);
  if (!s || !requests || count == 0) {
    return false;
  }
  bool sawIn = false, sawOut = false;
  uint32_t mainIn = 0, mainOut = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const auto& r = requests[i];
    if (r.port_index >= kMaxPorts || r.channel_count == 0 ||
        r.channel_count > 8) {
      return false;
    }
    if (r.port_index != 0) {
      continue;  // sidechain, reference
    }
    (r.is_input ? mainIn : mainOut) = r.channel_count;
    (r.is_input ? sawIn : sawOut) = true;
  }
  return !(sawIn && sawOut) || mainIn == mainOut;
}

bool configurableApply(const clap_plugin_t* p,
                       const clap_audio_port_configuration_request_t* requests,
                       uint32_t count) {
  if (!configurableCanApply(p, requests, count)) {
    return false;
  }
  Plugin* s = self(p);
  for (uint32_t i = 0; i < count; ++i) {
    const auto& r = requests[i];
    auto& slot = r.is_input ? s->inChannels[r.port_index]
                            : s->outChannels[r.port_index];
    slot.store(r.channel_count, std::memory_order_relaxed);
  }
  return true;
}

const clap_plugin_configurable_audio_ports_t kConfigurablePortsExt = {
    configurableCanApply, configurableApply};


// --- GUI --------------------------------------------------------------------

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) {
#ifdef SUITE_MACOS_COCOA
  (void)isFloating;
  return api && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
#else
  return !isFloating && api && std::strcmp(api, CLAP_WINDOW_API_X11) == 0;
#endif
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) {
#ifdef SUITE_MACOS_COCOA
  *api = CLAP_WINDOW_API_COCOA;
#else
  *api = CLAP_WINDOW_API_X11;
#endif
  *isFloating = false;
  return true;
}

bool guiCreate(const clap_plugin_t*, const char*, bool) { return true; }

void guiDestroy(const clap_plugin_t* p) { self(p)->editor.close(); }

// X11 embedding uses physical pixels, so the host does not set a scale.
bool guiSetScale(const clap_plugin_t*, double) { return false; }

bool guiGetSize(const clap_plugin_t* p, uint32_t* width, uint32_t* height) {
  const UiSize s = self(p)->editor.size();
  *width = s.width;
  *height = s.height;
  return true;
}

bool guiCanResize(const clap_plugin_t*) { return true; }

bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) {
  hints->can_resize_horizontally = true;
  hints->can_resize_vertically = true;
  hints->preserve_aspect_ratio = true;
  hints->aspect_ratio_width = kPanelWidth;
  hints->aspect_ratio_height = kPanelHeight;
  return true;
}

// Returns the nearest valid size; the editor always keeps 560:1040.
bool guiAdjustSize(const clap_plugin_t*, uint32_t* width, uint32_t* height) {
  const UiSize s = snapSize(*width, *height);
  *width = s.width;
  *height = s.height;
  return true;
}

bool guiSetSize(const clap_plugin_t* p, uint32_t width, uint32_t height) {
  self(p)->editor.resize(scaleForRequest(width, height));
  return true;
}

bool guiSetParent(const clap_plugin_t* p, const clap_window_t* window) {
#ifdef SUITE_MACOS_COCOA
  return self(p)->editor.open(window->cocoa);
#else
  return self(p)->editor.open(window->x11 ? reinterpret_cast<void*>(
                                                static_cast<uintptr_t>(window->x11))
                                          : nullptr);
#endif
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }

void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t*) { return true; }

bool guiHide(const clap_plugin_t*) { return true; }

const clap_plugin_gui_t kGuiExt = {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale,
    guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize,
    guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide,
};

// --- Gain adjustment metering -----------------------------------------------

double gainMeterGet(const clap_plugin_t* p) {
  return -static_cast<double>(self(p)->grForHost.load(std::memory_order_relaxed));
}

const clap_plugin_gain_adjustment_metering_t kGainMeterExt = {gainMeterGet};

uint32_t latencyGet(const clap_plugin_t* p) {
  return static_cast<uint32_t>(self(p)->engine.latencySamples());
}

const clap_plugin_latency_t kLatencyExt = {latencyGet};

// --- Plugin lifecycle -------------------------------------------------------

bool pluginInit(const clap_plugin_t*) { return true; }

void pluginDestroy(const clap_plugin_t* p) {
  delete self(p);
}

bool pluginActivate(const clap_plugin_t* p, double sampleRate, uint32_t,
                    uint32_t) {
  Plugin* s = self(p);
  s->sampleRate = sampleRate;
  s->engine.prepare(static_cast<float>(sampleRate),
                    static_cast<float>(s->shared.get(P_OVERSAMPLE)));
  s->active.store(true, std::memory_order_relaxed);
  if (s->peer.registerSlot(static_cast<uint32_t>(s->shared.get(P_BUS) + 0.5),
                           kPluginName, P_COUNT) >= 0) {
    s->peerRegistered = true;
  }
  s->reportedOversample.store(
      static_cast<int>(s->shared.get(P_OVERSAMPLE) + 0.5),
      std::memory_order_relaxed);
  // REAPER only picks up a new latency when the plugin calls changed(), so do
  // that here (being-activated is the one legal moment) - but only when the
  // value actually changed, so a host that reacts by restarting cannot loop.
  const uint32_t latencyNow = s->engine.latencySamples();
  if (latencyNow != s->reportedLatency.exchange(latencyNow)) {
    if (s->host && s->host->get_extension) {
      const auto* latency = static_cast<const clap_host_latency_t*>(
          s->host->get_extension(s->host, CLAP_EXT_LATENCY));
      if (latency && latency->changed) {
        latency->changed(s->host);
      }
    }
  }
  return true;
}

void pluginDeactivate(const clap_plugin_t* p) {
  Plugin* s = self(p);
  s->active.store(false, std::memory_order_relaxed);
  if (s->peerRegistered) {
    s->peer.unregisterSlot();
    s->peerRegistered = false;
  }
}

bool pluginStartProcessing(const clap_plugin_t*) { return true; }

void pluginStopProcessing(const clap_plugin_t*) {}

void pluginReset(const clap_plugin_t* p) {
  self(p)->engine.reset();
}

// Mirror the host parameters into the shared registry and pull back edits from
// Suite Control.  Atomics only, so this is safe on the audio thread.
void peerSync(Plugin* s) {
  if (!s->peerRegistered) return;
  for (uint32_t i = 0; i < P_COUNT; ++i) {
    double v = 0.0;
    if (s->peer.takeRemote(static_cast<int>(i), &v)) {
      // Mark dirty so the host receives a parameter event and its automation
      // lane follows the remote move.
      s->shared.set(i, v);
    }
  }
  for (uint32_t i = 0; i < P_COUNT; ++i) {
    s->peer.publish(static_cast<int>(i), s->shared.get(i));
  }
  s->peer.setBus(
      static_cast<uint32_t>(s->shared.get(P_BUS) + 0.5));
  s->peer.publishMeters(s->meters.inPeakLDb, s->meters.inPeakRDb,
                        s->meters.outPeakLDb, s->meters.outPeakRDb,
                        s->meters.grDb);
  s->peer.beat();
}

clap_process_status pluginProcess(const clap_plugin_t* p, const clap_process_t* proc) {
  Plugin* s = self(p);
  const uint32_t frames = proc->frames_count;
  if (proc->in_events) {
    applyEvents(s, proc->in_events);
  }
  peerSync(s);
  emitDirty(s, proc->out_events);

  if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) {
    return CLAP_PROCESS_CONTINUE;
  }
  // Be tolerant of mono or partially connected buffers.
  const clap_audio_buffer_t& inBuf = proc->audio_inputs[0];
  const clap_audio_buffer_t& outBuf = proc->audio_outputs[0];
  if (!outBuf.data32 || !inBuf.data32 || !outBuf.data32[0] || !inBuf.data32[0]) {
    return CLAP_PROCESS_CONTINUE;
  }
  float* left = outBuf.data32[0];
  float* right = nullptr;
  if (outBuf.channel_count > 1 && outBuf.data32[1]) {
    right = outBuf.data32[1];
  } else {
    if (s->monoRight.size() < frames) {
      s->monoRight.resize(frames);
    }
    right = s->monoRight.data();
  }
  const float* inL = inBuf.data32[0];
  const float* inR = inBuf.channel_count > 1 && inBuf.data32[1] ? inBuf.data32[1] : inL;
  if (left != inL) {
    std::memcpy(left, inL, frames * sizeof(float));
  }
  if (right != inR) {
    std::memcpy(right, inR, frames * sizeof(float));
  }

  s->engine.setControls(controlsFrom(s->shared));

  // Optional external side-chain: the second input port, if the host routed
  // anything to it.
  const float* scL = nullptr;
  const float* scR = nullptr;
  if (proc->audio_inputs_count > 1) {
    const clap_audio_buffer_t& scBuf = proc->audio_inputs[1];
    if (scBuf.data32 && scBuf.data32[0]) {
      scL = scBuf.data32[0];
      scR = scBuf.channel_count > 1 && scBuf.data32[1] ? scBuf.data32[1] : scL;
    }
  }
  s->engine.process(left, right, static_cast<int>(frames), s->meters, scL,
                    scR);

  s->grForHost.store(s->meters.grDb, std::memory_order_relaxed);
  s->shared.grDb.store(s->meters.grDb, std::memory_order_relaxed);
  s->shared.gateGrDb.store(s->meters.gateGrDb, std::memory_order_relaxed);
  s->shared.inPeakDb.store(s->meters.inPeakDb, std::memory_order_relaxed);
  s->shared.outPeakDb.store(s->meters.outPeakDb, std::memory_order_relaxed);
  s->shared.outVuDb.store(s->meters.outVuDb, std::memory_order_relaxed);
  s->shared.inPeakLDb.store(s->meters.inPeakLDb, std::memory_order_relaxed);
  s->shared.inPeakRDb.store(s->meters.inPeakRDb, std::memory_order_relaxed);
  s->shared.outPeakLDb.store(s->meters.outPeakLDb, std::memory_order_relaxed);
  s->shared.outPeakRDb.store(s->meters.outPeakRDb, std::memory_order_relaxed);
  noteHostChanges(s);
  return CLAP_PROCESS_CONTINUE;
}

const void* pluginGetExtension(const clap_plugin_t*, const char* id) {
  if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) {
    return &kParamsExt;
  }
  if (std::strcmp(id, CLAP_EXT_STATE) == 0) {
    return &kStateExt;
  }
  if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
    return &kAudioPortsExt;
  }
  if (std::strcmp(id, CLAP_EXT_CONFIGURABLE_AUDIO_PORTS) == 0) {
    return &kConfigurablePortsExt;
  }
  if (std::strcmp(id, CLAP_EXT_GUI) == 0) {
    return &kGuiExt;
  }
  if (std::strcmp(id, CLAP_EXT_GAIN_ADJUSTMENT_METERING) == 0) {
    return &kGainMeterExt;
  }
  if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) {
    return &kLatencyExt;
  }
  return nullptr;
}

void pluginOnMainThread(const clap_plugin_t* p) {
  Plugin* s = self(p);
  if (!s->rescanQueued.exchange(false, std::memory_order_relaxed)) {
    return;
  }
  const int os = static_cast<int>(s->shared.get(P_OVERSAMPLE) + 0.5);
  const bool latencyChanged =
      s->reportedOversample.exchange(os, std::memory_order_relaxed) != os;
  if (!s->host || !s->host->get_extension) {
    return;
  }
  if (latencyChanged && s->active.load(std::memory_order_relaxed)) {
    // CLAP only allows the latency to change while the plugin is being
    // activated; ask the host to restart.  The new latency is reported from
    // pluginActivate(), after the engine has actually switched rates.
    if (s->host->request_restart) {
      s->host->request_restart(s->host);
    }
  }
}

Plugin* createPlugin(const clap_host_t* host) {
  Plugin* s = new Plugin();
  s->host = host;
  s->plugin.desc = &kDescriptor;
  s->plugin.plugin_data = s;
  s->plugin.init = pluginInit;
  s->plugin.destroy = pluginDestroy;
  s->plugin.activate = pluginActivate;
  s->plugin.deactivate = pluginDeactivate;
  s->plugin.start_processing = pluginStartProcessing;
  s->plugin.stop_processing = pluginStopProcessing;
  s->plugin.reset = pluginReset;
  s->plugin.process = pluginProcess;
  s->plugin.get_extension = pluginGetExtension;
  s->plugin.on_main_thread = pluginOnMainThread;
  s->engine.prepare(48000.0f);
  // Open at a size that fits the current screen unless a saved state says
  // otherwise.
#ifdef SUITE_MACOS_COCOA
  s->editor.setScale(1.0);
#else
  s->editor.setScale(fitScaleForScreen());
#endif
  // Latency may only change while the plugin is being activated, so the
  // oversampling switch is applied on the host's restart, not mid-block.
  s->engine.setDeferOversample(true);
  s->editor.requestFlush = [s] {
    if (s->host && s->host->request_process) {
      s->host->request_process(s->host);
    }
  };
  return s;
}

// --- Factory and entry ------------------------------------------------------

uint32_t factoryCount(const clap_plugin_factory_t*) { return 1; }

const clap_plugin_descriptor_t* factoryDescriptor(const clap_plugin_factory_t*, uint32_t index) {
  return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t* factoryCreate(const clap_plugin_factory_t*, const clap_host_t* host,
                                   const char* id) {
  if (std::strcmp(id, kPluginId) != 0) {
    return nullptr;
  }
  return &createPlugin(host)->plugin;
}

const clap_plugin_factory_t kFactory = {factoryCount, factoryDescriptor, factoryCreate};

bool entryInit(const char*) {
  // Do not call XInitThreads() here: the host has already used Xlib by the
  // time plugins are loaded, and initialising threading afterwards is
  // undefined behaviour (it stopped the editor loading in REAPER).  The
  // editor instead confines every Xlib call to its own thread.
  return true;
}

void entryDeinit() {}

const void* entryGetFactory(const char* factoryId) {
  return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr;
}

} // namespace
} // namespace suite

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION,
    suite::entryInit,
    suite::entryDeinit,
    suite::entryGetFactory,
};
