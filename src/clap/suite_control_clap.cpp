// Suite Control — CLAP plug-in
// SPDX-License-Identifier: GPL-3.0-or-later
#include <clap/clap.h>

#include <dlfcn.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo/cairo-xlib.h>

#include "dsp/engine.hpp"
#include "params.hpp"
#include "ui/panel.hpp"

namespace {

constexpr const char* kPluginId = "com.tylorismusic.suitecontrol";
constexpr const char* kPluginName = "Suite Control";
constexpr const char* kVendor = "tylorismusic";
constexpr const char* kVersion = "0.1.0";

std::string bundleResourceDir() {
  Dl_info info{};
  if (dladdr(reinterpret_cast<void*>(&bundleResourceDir), &info) &&
      info.dli_fname) {
    std::string path = info.dli_fname;
    const size_t slash = path.rfind('/');
    if (slash != std::string::npos) path = path.substr(0, slash);
    return path + "/SuiteControl.resources";
  }
  return "SuiteControl.resources";
}

// ---------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------
class Editor {
public:
  explicit Editor(suite::Shared& shared) : shared_(shared) {}
  ~Editor() { close(); }

  std::function<void()> requestFlush;
  double scale() const { return 1.0; }

  bool open(void* parent) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (opened_) return true;
    parentWindow_ = reinterpret_cast<Window>(parent);
    quitting_ = false;
    openFailed_ = false;
    opened_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
  }

  void close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      quitting_ = true;
    }
    if (thread_.joinable()) thread_.join();
    opened_ = false;
  }

private:
  void run() {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
      openFailed_ = true;
      return;
    }
    const int screen = DefaultScreen(dpy);
    const Window parent = parentWindow_ ? parentWindow_ : RootWindow(dpy, screen);
    const int w = suite::kPanelWidth;
    const int h = suite::kPanelHeight;
    XSetWindowAttributes attrs{};
    attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                       PointerMotionMask | Button1MotionMask | ButtonMotionMask |
                       KeyPressMask;
    const Window win = XCreateWindow(dpy, parent, 0, 0, w, h, 0,
                                     CopyFromParent, InputOutput, CopyFromParent,
                                     CWEventMask, &attrs);
    XSizeHints hints{};
    hints.flags = PMinSize | PMaxSize;
    hints.min_width = hints.max_width = w;
    hints.min_height = hints.max_height = h;
    XSetWMNormalHints(dpy, win, &hints);
    XStoreName(dpy, win, kPluginName);
    XMapRaised(dpy, win);
    XFlush(dpy);

    panel_.loadFont(bundleResourceDir());

    XWindowAttributes wa{};
    XGetWindowAttributes(dpy, win, &wa);
    cairo_surface_t* windowSurface =
        cairo_xlib_surface_create(dpy, win, wa.visual, w, h);
    const cairo_format_t fmt =
        wa.depth == 32 ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24;
    cairo_surface_t* backing = cairo_image_surface_create(fmt, w, h);
    cairo_t* cr = cairo_create(backing);

    while (true) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (quitting_) break;
      }
      while (XPending(dpy) > 0) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        handleEvent(ev, state_);
      }
      panel_.tick(shared_, state_);
      panel_.draw(cr, shared_, state_);
      cairo_surface_flush(backing);
      cairo_t* xcr = cairo_create(windowSurface);
      cairo_set_source_surface(xcr, backing, 0, 0);
      cairo_pattern_set_filter(cairo_get_source(xcr), CAIRO_FILTER_NEAREST);
      cairo_paint(xcr);
      cairo_destroy(xcr);
      cairo_surface_flush(windowSurface);
      XFlush(dpy);
      std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    cairo_destroy(cr);
    cairo_surface_destroy(backing);
    cairo_surface_destroy(windowSurface);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
  }

  void handleEvent(const XEvent& ev, suite::PanelState& state) {
    switch (ev.type) {
      case ButtonPress:
        // X only delivers KeyPress to the focused window.
        XSetInputFocus(ev.xbutton.display, ev.xbutton.window, RevertToParent,
                       CurrentTime);
        if (ev.xbutton.button == Button1) {
          panel_.pointerDown(shared_, state, ev.xbutton.x, ev.xbutton.y,
                             (ev.xbutton.state & ShiftMask) != 0);
        } else if (ev.xbutton.button == Button4) {
          panel_.wheel(shared_, state, ev.xbutton.x, ev.xbutton.y, 1.0,
                       (ev.xbutton.state & ShiftMask) != 0);
        } else if (ev.xbutton.button == Button5) {
          panel_.wheel(shared_, state, ev.xbutton.x, ev.xbutton.y, -1.0,
                       (ev.xbutton.state & ShiftMask) != 0);
        } else if (ev.xbutton.button == Button3) {
          panel_.beginTextEntry(shared_, state, ev.xbutton.x, ev.xbutton.y);
        }
        break;
      case ButtonRelease:
        panel_.pointerUp();
        break;
      case MotionNotify:
        panel_.pointerMove(shared_, state, ev.xmotion.x, ev.xmotion.y,
                           (ev.xmotion.state & ShiftMask) != 0);
        break;
      case KeyPress: {
        char buf[8] = {};
        KeySym sym = 0;
        XKeyEvent key = ev.xkey;
        const int n = XLookupString(&key, buf, sizeof(buf), &sym, nullptr);
        if (sym == XK_Return || sym == XK_KP_Enter) {
          panel_.textKey(shared_, state, suite::Panel::kTextEnter, 0);
        } else if (sym == XK_Escape) {
          panel_.textKey(shared_, state, suite::Panel::kTextEscape, 0);
        } else if (sym == XK_BackSpace) {
          panel_.textKey(shared_, state, suite::Panel::kTextBackspace, 0);
        } else if (n > 0) {
          panel_.textKey(shared_, state, 0,
                         static_cast<unsigned>(static_cast<unsigned char>(buf[0])));
        }
        break;
      }
      default:
        break;
    }
  }

  suite::Shared& shared_;
  suite::PanelState state_;
  suite::Panel panel_;
  std::mutex mutex_;
  Display* display_ = nullptr;
  Window parentWindow_ = 0;
  bool opened_ = false;
  bool openFailed_ = false;
  bool quitting_ = false;
  std::thread thread_;
};

// ---------------------------------------------------------------------------
// Plugin
// ---------------------------------------------------------------------------
struct Plugin {
  clap_plugin_t plugin{};
  const clap_host_t* host = nullptr;
  suite::Shared shared;
  suite::Engine engine;
  Editor editor{shared};
  suite::Meters meters;
  std::atomic<bool> active{false};
  std::atomic<uint32_t> reportedLatency{0xffffffffu};
  std::vector<float> monoRight;
};

const char* const kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
                                 CLAP_PLUGIN_FEATURE_COMPRESSOR,
                                 CLAP_PLUGIN_FEATURE_EQUALIZER,
                                 CLAP_PLUGIN_FEATURE_MASTERING, nullptr};

Plugin* self(const clap_plugin_t* p) {
  return static_cast<Plugin*>(p->plugin_data);
}

void writeName(char* dst, size_t size, const char* src) {
  std::strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
}

// --- Parameters -------------------------------------------------------------
uint32_t paramsCount(const clap_plugin_t*) { return suite::P_COUNT; }

bool paramsInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
  if (index >= suite::P_COUNT) return false;
  const suite::ParamSpec& s = suite::kParamSpecs[index];
  std::memset(info, 0, sizeof(*info));
  info->id = index;
  info->flags = CLAP_PARAM_IS_AUTOMATABLE;
  if (s.stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
  if (s.hidden) info->flags |= CLAP_PARAM_IS_HIDDEN;
  writeName(info->name, sizeof(info->name), s.name);
  writeName(info->module, sizeof(info->module), s.module);
  info->min_value = s.min;
  info->max_value = s.max;
  info->default_value = s.def;
  return true;
}

bool paramsValue(const clap_plugin_t* p, clap_id id, double* value) {
  if (id >= suite::P_COUNT) return false;
  *value = self(p)->shared.get(id);
  return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* out,
                       uint32_t size) {
  if (id >= suite::P_COUNT) return false;
  const suite::ParamSpec& s = suite::kParamSpecs[id];
  if (s.valueLabels.count > 0) {
    const int i = std::min(std::max(static_cast<int>(value + 0.5), 0),
                           s.valueLabels.count - 1);
    std::snprintf(out, size, "%s", s.valueLabels.names[i]);
    return true;
  }
  if (id == suite::P_COMP_THRESHOLD) {
    std::snprintf(out, size, "%+.1f dB", suite::compThresholdDb((float)value));
  } else if (id == suite::P_COMP_ATTACK) {
    std::snprintf(out, size, "%.1f ms", suite::compAttackMs((float)value));
  } else if (id == suite::P_COMP_RELEASE) {
    std::snprintf(out, size, "%.0f ms", suite::compReleaseMs((float)value));
  } else if (id == suite::P_INPUT || id == suite::P_OUTPUT) {
    std::snprintf(out, size, "%+.1f dB", suite::inputGainDb((float)value));
  } else if (s.unit && std::strcmp(s.unit, "dB") == 0) {
    std::snprintf(out, size, "%+.1f dB", suite::bandGainDb((float)value));
  } else if (s.unit && std::strcmp(s.unit, "%") == 0) {
    std::snprintf(out, size, "%.0f%%", 100.0 * value);
  } else {
    std::snprintf(out, size, "%.2f", value);
  }
  return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* text,
                       double* value) {
  if (id >= suite::P_COUNT) return false;
  *value = std::atof(text);
  return true;
}

void applyEvents(Plugin* s, const clap_input_events_t* in) {
  if (!in) return;
  const uint32_t n = in->size(in);
  for (uint32_t i = 0; i < n; ++i) {
    const clap_event_header_t* h = in->get(in, i);
    if (!h) continue;
    if (h->type == CLAP_EVENT_PARAM_VALUE) {
      const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
      if (e->param_id < suite::P_COUNT) s->shared.setFromHost(e->param_id, e->value);
    }
  }
}

void emitDirty(Plugin* s, const clap_output_events_t* out) {
  if (!out) return;
  for (uint32_t i = 0; i < suite::P_COUNT; ++i) {
    if (!s->shared.takeDirty(i)) continue;
    clap_event_param_value_t ev{};
    ev.header.size = sizeof(ev);
    ev.header.type = CLAP_EVENT_PARAM_VALUE;
    ev.param_id = i;
    ev.note_id = -1;
    ev.port_index = -1;
    ev.channel = -1;
    ev.key = -1;
    ev.value = s->shared.get(i);
    out->try_push(out, &ev.header);
  }
}

void paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in,
                 const clap_output_events_t* out) {
  Plugin* s = self(p);
  applyEvents(s, in);
  emitDirty(s, out);
}

const clap_plugin_params_t kParamsExt = {
    paramsCount, paramsInfo, paramsValue, paramsValueToText, paramsTextToValue,
    paramsFlush,
};

// --- State ------------------------------------------------------------------
bool stateSave(const clap_plugin_t* p, const clap_ostream_t* stream) {
  Plugin* s = self(p);
  double values[suite::P_COUNT];
  for (uint32_t i = 0; i < suite::P_COUNT; ++i) values[i] = s->shared.get(i);
  if (stream->write(stream, values, sizeof(values)) !=
      static_cast<int64_t>(sizeof(values))) {
    return false;
  }
  // Console name table + mute states, appended after the parameters.
  constexpr uint32_t kMagic = 0x53434e4d;  // SCNM
  constexpr uint32_t kTableVersion = 2;
  const uint32_t count = suite::Shared::kNameSlots;
  if (stream->write(stream, &kMagic, sizeof(kMagic)) != sizeof(kMagic) ||
      stream->write(stream, &kTableVersion, sizeof(kTableVersion)) !=
          sizeof(kTableVersion) ||
      stream->write(stream, &count, sizeof(count)) != sizeof(count)) {
    return false;
  }
  for (uint32_t i = 0; i < count; ++i) {
    if (stream->write(stream, s->shared.names[i],
                      suite::Shared::kNameBytes) != suite::Shared::kNameBytes) {
      return false;
    }
    const uint8_t m = s->shared.muted[i] ? 1 : 0;
    if (stream->write(stream, &m, 1) != 1) return false;
    const uint8_t forced = s->shared.gainForced[i] ? 1 : 0;
    if (stream->write(stream, &forced, 1) != 1) return false;
    if (stream->write(stream, &s->shared.savedGain[i],
                      sizeof(float)) != sizeof(float)) {
      return false;
    }
  }
  return true;
}

bool stateLoad(const clap_plugin_t* p, const clap_istream_t* stream) {
  Plugin* s = self(p);
  double values[suite::P_COUNT] = {};
  const int64_t got = stream->read(stream, values, sizeof(values));
  if (got <= 0 || got % static_cast<int64_t>(sizeof(double)) != 0) return false;
  const uint32_t count = static_cast<uint32_t>(
      std::min<int64_t>(got / static_cast<int64_t>(sizeof(double)),
                        suite::P_COUNT));
  for (uint32_t i = 0; i < count; ++i) s->shared.setFromHost(i, values[i]);
  for (uint32_t i = count; i < suite::P_COUNT; ++i) {
    s->shared.setFromHost(i, suite::kParamSpecs[i].def);
  }
  // Optional name table.
  uint32_t magic = 0, version = 0, slots = 0;
  if (stream->read(stream, &magic, sizeof(magic)) == sizeof(magic) &&
      magic == 0x53434e4d &&
      stream->read(stream, &version, sizeof(version)) == sizeof(version) &&
      version == 1 &&
      stream->read(stream, &slots, sizeof(slots)) == sizeof(slots) &&
      (version == 1 || version == 2) && slots <= suite::Shared::kNameSlots) {
    for (uint32_t i = 0; i < slots; ++i) {
      if (stream->read(stream, s->shared.names[i],
                       suite::Shared::kNameBytes) !=
          suite::Shared::kNameBytes) {
        break;
      }
      uint8_t m = 0;
      if (stream->read(stream, &m, 1) != 1) break;
      s->shared.muted[i] = m != 0;
      if (version >= 2) {
        uint8_t forced = 0;
        if (stream->read(stream, &forced, 1) != 1) break;
        s->shared.gainForced[i] = forced != 0;
        if (stream->read(stream, &s->shared.savedGain[i], sizeof(float)) !=
            sizeof(float)) {
          break;
        }
      }
    }
  }
  return true;
}

const clap_plugin_state_t kStateExt = {stateSave, stateLoad};

// --- Ports ------------------------------------------------------------------
uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) {
  return isInput ? 1 : 1;
}
bool audioPortsInfo(const clap_plugin_t*, uint32_t index, bool isInput,
                    clap_audio_port_info_t* info) {
  if (index != 0) return false;
  std::memset(info, 0, sizeof(*info));
  info->id = 0;
  info->channel_count = 2;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
  writeName(info->name, sizeof(info->name), isInput ? "Input" : "Output");
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  return true;
}
const clap_plugin_audio_ports_t kPortsExt = {audioPortsCount, audioPortsInfo};

// --- Latency ----------------------------------------------------------------
uint32_t latencyGet(const clap_plugin_t* p) {
  return static_cast<uint32_t>(self(p)->engine.latencySamples());
}
const clap_plugin_latency_t kLatencyExt = {latencyGet};

// --- GUI --------------------------------------------------------------------
bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) {
  return !isFloating && api && std::strcmp(api, CLAP_WINDOW_API_X11) == 0;
}
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* floating) {
  *api = CLAP_WINDOW_API_X11;
  *floating = false;
  return true;
}
bool guiCreate(const clap_plugin_t*, const char* api, bool isFloating) {
  return !isFloating && api && std::strcmp(api, CLAP_WINDOW_API_X11) == 0;
}
void guiDestroy(const clap_plugin_t* p) { self(p)->editor.close(); }
bool guiSetScale(const clap_plugin_t*, double) { return false; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) {
  *w = suite::kPanelWidth;
  *h = suite::kPanelHeight;
  return true;
}
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) {
  return false;
}
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t*, uint32_t, uint32_t) { return false; }
bool guiSetParent(const clap_plugin_t* p, const clap_window_t* window) {
  return self(p)->editor.open(reinterpret_cast<void*>(window->x11));
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* p) {
  Plugin* s = self(p);
  s->editor.requestFlush = [s] {
    if (s->host && s->host->request_callback) s->host->request_callback(s->host);
  };
  return true;
}
bool guiHide(const clap_plugin_t*) { return true; }
const clap_plugin_gui_t kGuiExt = [] {
  clap_plugin_gui_t g{};
  g.is_api_supported = guiIsApiSupported;
  g.get_preferred_api = guiGetPreferredApi;
  g.create = guiCreate;
  g.destroy = guiDestroy;
  g.set_scale = guiSetScale;
  g.get_size = guiGetSize;
  g.can_resize = guiCanResize;
  g.get_resize_hints = guiGetResizeHints;
  g.adjust_size = guiAdjustSize;
  g.set_size = guiSetSize;
  g.set_parent = guiSetParent;
  g.set_transient = guiSetTransient;
  g.suggest_title = guiSuggestTitle;
  g.show = guiShow;
  g.hide = guiHide;
  return g;
}();

// --- Lifecycle --------------------------------------------------------------
const clap_plugin_descriptor_t kDescriptor = {
    CLAP_VERSION,
    kPluginId,
    kPluginName,
    kVendor,
    "https://github.com/tylorismusic-commits/suite-control",
    "",
    "",
    kVersion,
    "Folder-track mix bus with SSL-style compression, 4K EQ, console summing colour, and a remote surface for Suite Channel Plus instances.",
    kFeatures,
};

bool pluginInit(const clap_plugin_t*) { return true; }
void pluginDestroy(const clap_plugin_t* p) { delete self(p); }

bool pluginActivate(const clap_plugin_t* p, double sampleRate, uint32_t,
                    uint32_t) {
  Plugin* s = self(p);
  s->engine.prepare(static_cast<float>(sampleRate));
  s->active.store(true, std::memory_order_relaxed);
  const uint32_t latencyNow = s->engine.latencySamples();
  if (latencyNow != s->reportedLatency.exchange(latencyNow)) {
    if (s->host && s->host->get_extension) {
      const auto* latency = static_cast<const clap_host_latency_t*>(
          s->host->get_extension(s->host, CLAP_EXT_LATENCY));
      if (latency && latency->changed) latency->changed(s->host);
    }
  }
  return true;
}
void pluginDeactivate(const clap_plugin_t* p) {
  self(p)->active.store(false, std::memory_order_relaxed);
}
bool pluginStartProcessing(const clap_plugin_t*) { return true; }
void pluginStopProcessing(const clap_plugin_t*) {}
void pluginReset(const clap_plugin_t* p) { self(p)->engine.reset(); }

clap_process_status pluginProcess(const clap_plugin_t* p,
                                  const clap_process_t* proc) {
  Plugin* s = self(p);
  const uint32_t frames = proc->frames_count;
  if (proc->in_events) applyEvents(s, proc->in_events);
  emitDirty(s, proc->out_events);

  if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) {
    return CLAP_PROCESS_CONTINUE;
  }
  const clap_audio_buffer_t& inBuf = proc->audio_inputs[0];
  const clap_audio_buffer_t& outBuf = proc->audio_outputs[0];
  if (!outBuf.data32 || !inBuf.data32 || !outBuf.data32[0] || !inBuf.data32[0]) {
    return CLAP_PROCESS_CONTINUE;
  }
  float* left = outBuf.data32[0];
  float* right = outBuf.channel_count > 1 && outBuf.data32[1]
                     ? outBuf.data32[1]
                     : nullptr;
  if (!right) {
    if (s->monoRight.size() < frames) s->monoRight.resize(frames);
    right = s->monoRight.data();
  }
  const float* inL = inBuf.data32[0];
  const float* inR =
      inBuf.channel_count > 1 && inBuf.data32[1] ? inBuf.data32[1] : inL;
  if (left != inL) std::memcpy(left, inL, frames * sizeof(float));
  if (right != inR) std::memcpy(right, inR, frames * sizeof(float));

  s->engine.setControls(suite::controlsFrom(s->shared));
  s->engine.process(left, right, static_cast<int>(frames), s->meters);

  s->shared.inPeakDb.store(s->meters.inPeakDb, std::memory_order_relaxed);
  s->shared.outPeakDb.store(s->meters.outPeakDb, std::memory_order_relaxed);
  s->shared.inPeakLDb.store(s->meters.inPeakLDb, std::memory_order_relaxed);
  s->shared.inPeakRDb.store(s->meters.inPeakRDb, std::memory_order_relaxed);
  s->shared.outPeakLDb.store(s->meters.outPeakLDb, std::memory_order_relaxed);
  s->shared.outPeakRDb.store(s->meters.outPeakRDb, std::memory_order_relaxed);
  s->shared.grDb.store(s->meters.grDb, std::memory_order_relaxed);
  return CLAP_PROCESS_CONTINUE;
}

const void* pluginGetExtension(const clap_plugin_t*, const char* id) {
  if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &kParamsExt;
  if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &kStateExt;
  if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kPortsExt;
  if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &kLatencyExt;
  if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &kGuiExt;
  return nullptr;
}

void pluginOnMainThread(const clap_plugin_t*) {}

const clap_plugin_t* pluginCreate(const clap_host_t* host) {
  auto* s = new Plugin();
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
  return &s->plugin;
}

const clap_plugin_descriptor_t* factoryDescriptor(const clap_plugin_factory_t*,
                                                  uint32_t index) {
  return index == 0 ? &kDescriptor : nullptr;
}
const clap_plugin_t* factoryCreate(const clap_plugin_factory_t*,
                                   const clap_host_t* host, const char* id) {
  if (!id || std::strcmp(id, kPluginId) != 0) return nullptr;
  return pluginCreate(host);
}
uint32_t factoryCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_factory_t kFactory = {factoryCount, factoryDescriptor,
                                        factoryCreate};

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* id) {
  if (std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0) return &kFactory;
  return nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION, entryInit, entryDeinit, entryGetFactory};
