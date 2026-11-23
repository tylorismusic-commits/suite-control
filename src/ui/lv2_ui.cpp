// Suite Control — LV2 UI (X11 + Cairo)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The host owns the widget size; we letterbox the 1280x760 panel into whatever
// allocation arrives.  Redraws are throttled from the idle interface so a busy
// GTK idle handler cannot starve the host.

#include <lv2/core/lv2.h>
#include <lv2/ui/ui.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <cairo-xlib.h>
#include <poll.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "console_state.hpp"
#include "params.hpp"
#include "ui/geometry.hpp"
#include "ui/osc.hpp"
#include "ui/osc_panel.hpp"
#include "ui/panel.hpp"

namespace {

constexpr const char* kUiUri =
    "https://github.com/tylorismusic-commits/suite-control#ui";
constexpr uint32_t kFirstParam = 4;
constexpr auto kFrameInterval = std::chrono::milliseconds(33);

struct Ui {
  LV2UI_Write_Function write = nullptr;
  LV2UI_Controller controller = nullptr;
  const LV2UI_Resize* hostResize = nullptr;

  suite::Shared shared;
  suite::Panel panel;
  suite::PanelState state;

  // Console names/mute state is edited here but saved by the plug-in, so both
  // sides meet in a tiny per-process segment.
  suite::ConsoleBridge console;
  suite::OscLink osc{suite::OscProtocol::Auto};
  uint32_t consoleSeq = 0;
  bool consoleDirty = false;

  Display* dpy = nullptr;
  Window win = 0;
  cairo_surface_t* windowSurface = nullptr;
  cairo_surface_t* backing = nullptr;
  cairo_t* cr = nullptr;
  cairo_format_t format = CAIRO_FORMAT_RGB24;

  uint32_t width = suite::kPanelWidth;
  uint32_t height = suite::kPanelHeight;

  // Hosts (REAPER among them) echo values back through port_event while we are
  // still applying our own write, so a click would be reverted by a stale echo.
  double lastWriteValue[suite::P_COUNT] = {};
  double lastWriteMs[suite::P_COUNT] = {};

  static double nowMs() {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  // The UI owns its own frame loop.  X11 hosts are not required to provide
  // the idle interface, and REAPER does not: it painted once while the FX
  // container was still unmapped, discarded that frame when the window became
  // viewable, and never called back, leaving a black box.
  std::atomic<bool> dirty{true};
  std::atomic<bool> running{false};
  std::thread thread;
  std::chrono::steady_clock::time_point lastFrame{};

  // Open diagnostics.  A host that never calls idle() (or that reparents the
  // window after we paint) leaves a black box; these counters say which.
  FILE* logf = nullptr;
  long idles = 0;
  long draws = 0;
  long exposes = 0;
  long maps = 0;
  long reparents = 0;
  long eventsLogged = 0;
  long writesLogged = 0;

  long logLines = 0;

  void logf_(const char* fmt, ...) {
    if (!logf || logLines >= 300) return;
    ++logLines;
    std::fprintf(logf, "%8.1f ", nowMs() / 1000.0);
    std::va_list args;
    va_start(args, fmt);
    std::vfprintf(logf, fmt, args);
    va_end(args);
    std::fputc('\n', logf);
    std::fflush(logf);
  }

  void describeWindow(const char* what) {
    if (!logf) return;
    XWindowAttributes wa{};
    Window root = 0, parent = 0, *kids = nullptr;
    unsigned n = 0;
    const bool ok = XGetWindowAttributes(dpy, win, &wa) != 0;
    XQueryTree(dpy, win, &root, &parent, &kids, &n);
    if (kids) XFree(kids);
    logf_("%s win=0x%lx parent=0x%lx kids=%u geom=%dx%d+%d+%d depth=%d mapped=%d "
          "backing=%d status=%s",
          what, static_cast<unsigned long>(win),
          static_cast<unsigned long>(parent), n, ok ? wa.width : -1,
          ok ? wa.height : -1, ok ? wa.x : -1, ok ? wa.y : -1,
          ok ? wa.depth : -1, ok ? wa.map_state : -1,
          ok ? wa.backing_store : -1,
          !windowSurface ? "null"
                         : (cairo_surface_status(windowSurface) ==
                                    CAIRO_STATUS_SUCCESS
                                ? "ok"
                                : cairo_status_to_string(
                                      cairo_surface_status(windowSurface))));
  }

  double layoutScale() const {
    const double sx = static_cast<double>(width) / suite::kPanelWidth;
    const double sy = static_cast<double>(height) / suite::kPanelHeight;
    return std::min(sx, sy);
  }
  double offsetX() const {
    return (static_cast<double>(width) - suite::kPanelWidth * layoutScale()) * 0.5;
  }
  double offsetY() const {
    return (static_cast<double>(height) - suite::kPanelHeight * layoutScale()) * 0.5;
  }

  void rebuildSurfaces() {
    if (cr) {
      cairo_destroy(cr);
      cr = nullptr;
    }
    if (backing) {
      cairo_surface_destroy(backing);
      backing = nullptr;
    }
    backing = cairo_image_surface_create(format, static_cast<int>(width),
                                         static_cast<int>(height));
    cr = cairo_create(backing);
    if (windowSurface) {
      cairo_xlib_surface_set_size(windowSurface, width, height);
    }
  }

  void configure(uint32_t w, uint32_t h) {
    if (w == 0 || h == 0 || (w == width && h == height)) return;
    width = w;
    height = h;
    rebuildSurfaces();
    dirty = true;
  }

  void adoptConsole() {
    suite::ConsoleTable table{};
    if (!console.read(table)) return;
    if (console.sequence() == consoleSeq) return;
    consoleSeq = console.sequence();
    std::memcpy(shared.names, table.names, sizeof(shared.names));
    for (int i = 0; i < suite::ConsoleTable::kSlots; ++i) {
      shared.muted[i] = table.muted[i] != 0;
      shared.gainForced[i] = table.gainForced[i] != 0;
      shared.savedGain[i] = table.savedGain[i];
    }
    dirty = true;
  }

  void publishConsole() {
    suite::ConsoleTable table{};
    std::memcpy(table.names, shared.names, sizeof(table.names));
    for (int i = 0; i < suite::ConsoleTable::kSlots; ++i) {
      table.muted[i] = shared.muted[i] ? 1 : 0;
      table.gainForced[i] = shared.gainForced[i] ? 1 : 0;
      table.savedGain[i] = shared.savedGain[i];
    }
    console.write(table);
    consoleSeq = console.sequence();
  }

  void handleEvent(const XEvent& ev) {
    const double s = layoutScale();
    const double ox = offsetX();
    const double oy = offsetY();
    auto lx = [&](int x) { return (x - ox) / s; };
    auto ly = [&](int y) { return (y - oy) / s; };

    switch (ev.type) {
      case ConfigureNotify:
        configure(static_cast<uint32_t>(ev.xconfigure.width),
                  static_cast<uint32_t>(ev.xconfigure.height));
        break;
      case Expose:
        ++exposes;
        if (ev.xexpose.count == 0) draw();
        break;
      case MapNotify:
        ++maps;
        dirty = true;
        break;
      case ReparentNotify:
        ++reparents;
        dirty = true;
        break;
      case ButtonPress: {
        if (dpy && win) {
          XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
          XFlush(dpy);
        }
        const bool fine = (ev.xbutton.state & ShiftMask) != 0;
        if (ev.xbutton.button == Button4) {
          panel.wheel(shared, state, lx(ev.xbutton.x), ly(ev.xbutton.y), 1.0,
                      fine);
        } else if (ev.xbutton.button == Button5) {
          panel.wheel(shared, state, lx(ev.xbutton.x), ly(ev.xbutton.y), -1.0,
                      fine);
        } else if (ev.xbutton.button == Button1) {
          panel.pointerDown(shared, state, lx(ev.xbutton.x), ly(ev.xbutton.y),
                            fine);
        } else if (ev.xbutton.button == Button3) {
          panel.beginTextEntry(shared, state, lx(ev.xbutton.x),
                               ly(ev.xbutton.y));
        }
        consoleDirty = true;
        dirty = true;
        break;
      }
      case ButtonRelease:
        panel.pointerUp();
        dirty = true;
        break;
      case KeyPress: {
        char buf[8] = {};
        KeySym sym = 0;
        XKeyEvent key = ev.xkey;
        const int n = XLookupString(&key, buf, sizeof(buf), &sym, nullptr);
        if (sym == XK_Return || sym == XK_KP_Enter) {
          panel.textKey(shared, state, suite::Panel::kTextEnter, 0);
        } else if (sym == XK_Escape) {
          panel.textKey(shared, state, suite::Panel::kTextEscape, 0);
        } else if (sym == XK_BackSpace) {
          panel.textKey(shared, state, suite::Panel::kTextBackspace, 0);
        } else if (sym == XK_Tab) {
          panel.textKey(shared, state, suite::Panel::kTextTab, 0);
        } else if (n > 0) {
          panel.textKey(shared, state, 0, buf[0]);
        } else {
          int digit = -1;
          switch (sym) {
            case XK_KP_Insert: digit = '0'; break;
            case XK_KP_End: digit = '1'; break;
            case XK_KP_Down: digit = '2'; break;
            case XK_KP_Next: digit = '3'; break;
            case XK_KP_Left: digit = '4'; break;
            case XK_KP_Begin: digit = '5'; break;
            case XK_KP_Right: digit = '6'; break;
            case XK_KP_Home: digit = '7'; break;
            case XK_KP_Up: digit = '8'; break;
            case XK_KP_Prior: digit = '9'; break;
            case XK_KP_Decimal:
            case XK_KP_Delete: digit = '.'; break;
            case XK_KP_Subtract: digit = '-'; break;
            default: break;
          }
          if (digit >= 0) {
            panel.textKey(shared, state, 0, static_cast<unsigned>(digit));
          }
        }
        consoleDirty = true;
        dirty = true;
        break;
      }
      case MotionNotify:
        panel.pointerMove(shared, state, lx(ev.xmotion.x), ly(ev.xmotion.y),
                          (ev.xmotion.state & ShiftMask) != 0);
        dirty = true;
        break;
      default:
        break;
    }
  }

  void draw() {
    if (!cr || !windowSurface || width < 8 || height < 8) return;
    osc.poll();
    suite::applyOscState(osc, state);
    if (consoleDirty) {
      publishConsole();
      consoleDirty = false;
    } else {
      adoptConsole();
    }
    ++draws;
    if (draws == 1) logf_("first frame");

    for (uint32_t i = 0; i < suite::P_COUNT; ++i) {
      if (!shared.takeDirty(i)) continue;
      const float v = static_cast<float>(shared.get(i));
      lastWriteValue[i] = static_cast<double>(v);
      lastWriteMs[i] = nowMs();
      if (write) write(controller, kFirstParam + i, sizeof(float), 0, &v);
    }

    cairo_save(cr);
    cairo_set_source_rgb(cr, 0.043, 0.047, 0.051);
    cairo_paint(cr);
    cairo_translate(cr, offsetX(), offsetY());
    cairo_scale(cr, layoutScale(), layoutScale());
    cairo_rectangle(cr, 0, 0, suite::kPanelWidth, suite::kPanelHeight);
    cairo_clip(cr);
    panel.tick(shared, state);
    panel.draw(cr, shared, state);
    cairo_restore(cr);
    cairo_surface_flush(backing);

    cairo_t* xcr = cairo_create(windowSurface);
    cairo_set_source_surface(xcr, backing, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(xcr), CAIRO_FILTER_NEAREST);
    cairo_paint(xcr);
    cairo_destroy(xcr);
    cairo_surface_flush(windowSurface);
    XFlush(dpy);

  }

  void processEvents() {
    while (dpy && XPending(dpy) > 0) {
      XEvent ev;
      XNextEvent(dpy, &ev);
      handleEvent(ev);
    }
  }

  void runLoop() {
    while (running.load(std::memory_order_relaxed)) {
      processEvents();
      const auto now = std::chrono::steady_clock::now();
      if (dirty.load(std::memory_order_relaxed) ||
          now - lastFrame >= kFrameInterval) {
        draw();
        dirty.store(false, std::memory_order_relaxed);
        lastFrame = now;
      }
      // Wake on host input, otherwise run at the frame rate (meters).
      pollfd pfd{ConnectionNumber(dpy), POLLIN, 0};
      poll(&pfd, 1, 8);
    }
  }

  // The idle interface is optional for the host: if it does call us we only
  // nudge the frame loop, so no Xlib call ever crosses threads.
  int idle() {
    ++idles;
    dirty.store(true, std::memory_order_relaxed);
    return 0;
  }
};

LV2UI_Handle uiInstantiate(const LV2UI_Descriptor*, const char*,
                           const char* bundlePath, LV2UI_Write_Function write,
                           LV2UI_Controller controller, LV2UI_Widget* widget,
                           const LV2_Feature* const* features) {
  void* parent = nullptr;
  const LV2UI_Resize* hostResize = nullptr;
  const float* scaleHint = nullptr;
  for (const LV2_Feature* const* f = features; f && *f; ++f) {
    if (!std::strcmp((*f)->URI, LV2_UI__parent)) {
      parent = (*f)->data;
    } else if (!std::strcmp((*f)->URI, LV2_UI__resize)) {
      hostResize = static_cast<const LV2UI_Resize*>(f[0]->data);
    } else if (!std::strcmp((*f)->URI, LV2_UI__scaleFactor)) {
      scaleHint = static_cast<const float*>(f[0]->data);
    }
  }

  auto* ui = new Ui();
  ui->write = write;
  ui->controller = controller;
  ui->hostResize = hostResize;

  ui->dpy = XOpenDisplay(nullptr);
  if (!ui->dpy) {
    delete ui;
    return nullptr;
  }

  const int screen = DefaultScreen(ui->dpy);
  const int screenH = DisplayHeight(ui->dpy, screen);
  const double fitScale = suite::clampScale(std::min(
      1.0, std::max(suite::kMinUiScale,
                    (screenH - 140.0) / suite::kPanelHeight)));
  const double requested =
      scaleHint && *scaleHint > 0.0f ? suite::clampScale(*scaleHint) : 1.0;
  const double initialScale = std::min(requested, fitScale);
  const suite::UiSize initial = suite::sizeForScale(initialScale);
  ui->width = initial.width;
  ui->height = initial.height;

  const Window parentWindow =
      parent ? static_cast<Window>(reinterpret_cast<uintptr_t>(parent))
             : RootWindow(ui->dpy, screen);

  XSetWindowAttributes attrs{};
  attrs.background_pixel = BlackPixel(ui->dpy, screen);
  attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | Button1MotionMask | ButtonMotionMask |
                     KeyPressMask | StructureNotifyMask;
  ui->win = XCreateWindow(ui->dpy, parentWindow, 0, 0, ui->width, ui->height, 0,
                          CopyFromParent, InputOutput, CopyFromParent,
                          CWBackPixel | CWEventMask, &attrs);
  XStoreName(ui->dpy, ui->win, "Suite Control");

  ui->logf = std::fopen("/tmp/suite-control-ui.log", "w");
  ui->logf_("instantiate requested parent=0x%lx screen=%d screenDepth=%d "
            "size=%ux%u scaleHint=%.2f fit=%.2f",
            static_cast<unsigned long>(parentWindow), screen,
            DefaultDepth(ui->dpy, screen), ui->width, ui->height,
            scaleHint ? static_cast<double>(*scaleHint) : -1.0, fitScale);
  ui->describeWindow("pre-map");

  XMapRaised(ui->dpy, ui->win);
  ui->describeWindow("post-map");

  {
    const suite::UiSize minSize = suite::sizeForScale(suite::kMinUiScale);
    const suite::UiSize maxSize = suite::sizeForScale(suite::kMaxUiScale);
    XSizeHints hints{};
    hints.flags = PMinSize | PMaxSize | PBaseSize | PWinGravity;
    hints.min_width = static_cast<int>(minSize.width);
    hints.min_height = static_cast<int>(minSize.height);
    hints.max_width = static_cast<int>(maxSize.width);
    hints.max_height = static_cast<int>(maxSize.height);
    hints.base_width = suite::kPanelWidth;
    hints.base_height = suite::kPanelHeight;
    hints.win_gravity = NorthWestGravity;
    XSetWMNormalHints(ui->dpy, ui->win, &hints);
  }

  // Match the window's actual visual/depth: hosts often embed into an ARGB
  // parent, and a mismatched cairo surface silently paints nothing (black).
  XWindowAttributes wa{};
  XGetWindowAttributes(ui->dpy, ui->win, &wa);
  if (wa.depth == 0) {
    wa.depth = DefaultDepth(ui->dpy, screen);
    wa.visual = DefaultVisual(ui->dpy, screen);
  }
  ui->format = wa.depth == 32 ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24;
  ui->windowSurface = cairo_xlib_surface_create(
      ui->dpy, ui->win, wa.visual, ui->width, ui->height);
  if (cairo_surface_status(ui->windowSurface) != CAIRO_STATUS_SUCCESS) {
    // Last resort: the host's visual was unusable, fall back to the default.
    cairo_surface_destroy(ui->windowSurface);
    ui->format = CAIRO_FORMAT_RGB24;
    ui->windowSurface = cairo_xlib_surface_create(
        ui->dpy, ui->win, DefaultVisual(ui->dpy, screen), ui->width, ui->height);
  }
  ui->rebuildSurfaces();
  XFlush(ui->dpy);

  if (bundlePath) ui->panel.loadFont(bundlePath);

  if (ui->hostResize && ui->hostResize->ui_resize) {
    ui->hostResize->ui_resize(ui->hostResize->handle,
                              static_cast<int>(ui->width),
                              static_cast<int>(ui->height));
  }

  ui->console.open();
  ui->osc.open();
  suite::bindOsc(ui->osc, ui->state);
  ui->adoptConsole();
  ui->panel.tick(ui->shared, ui->state);
  ui->draw();
  ui->dirty = false;
  ui->describeWindow("after first draw");

  ui->running.store(true, std::memory_order_relaxed);
  ui->thread = std::thread([ui] { ui->runLoop(); });

  *widget = reinterpret_cast<LV2UI_Widget>(static_cast<uintptr_t>(ui->win));
  return ui;
}

void uiCleanup(LV2UI_Handle handle) {
  auto* ui = static_cast<Ui*>(handle);
  ui->osc.close();
  ui->running.store(false, std::memory_order_relaxed);
  if (ui->thread.joinable()) ui->thread.join();
  ui->logf_("cleanup idles=%ld draws=%ld exposes=%ld maps=%ld reparents=%ld",
            ui->idles, ui->draws, ui->exposes, ui->maps, ui->reparents);
  if (ui->logf) std::fclose(ui->logf);
  if (ui->cr) cairo_destroy(ui->cr);
  if (ui->backing) cairo_surface_destroy(ui->backing);
  if (ui->windowSurface) cairo_surface_destroy(ui->windowSurface);
  if (ui->dpy) {
    XDestroyWindow(ui->dpy, ui->win);
    XCloseDisplay(ui->dpy);
  }
  delete ui;
}

void uiPortEvent(LV2UI_Handle handle, uint32_t port, uint32_t size,
                 uint32_t format, const void* buffer) {
  auto* ui = static_cast<Ui*>(handle);
  if (format != 0 || size != sizeof(float) || !buffer) return;
  const float v = *static_cast<const float*>(buffer);
  if (port >= kFirstParam && port < kFirstParam + suite::P_COUNT) {
    const uint32_t id = port - kFirstParam;
    // REAPER echoes parameter values back to the UI, and the echo can still
    // carry the host's previous value right after we write a new one.  Ignore
    // those for a moment, otherwise every click snaps back.
    const double age = Ui::nowMs() - ui->lastWriteMs[id];
    if (ui->lastWriteMs[id] > 0.0 && age < 600.0 &&
        std::fabs(static_cast<double>(v) - ui->lastWriteValue[id]) > 1e-9) {
      return;
    }
    ui->shared.setFromHost(id, static_cast<double>(v));
    ui->dirty = true;
    return;
  }
  constexpr uint32_t kMeterBase = kFirstParam + suite::P_COUNT;
  switch (port) {
    case kMeterBase: ui->shared.grDb.store(v); break;
    case kMeterBase + 1: ui->shared.inPeakLDb.store(v); break;
    case kMeterBase + 2: ui->shared.inPeakRDb.store(v); break;
    case kMeterBase + 3: ui->shared.outPeakLDb.store(v); break;
    case kMeterBase + 4: ui->shared.outPeakRDb.store(v); break;
    default: break;
  }
}

int uiIdle(LV2UI_Handle handle) { return static_cast<Ui*>(handle)->idle(); }

const LV2UI_Idle_Interface kIdleInterface = {uiIdle};

const void* uiExtensionData(const char* uri) {
  if (uri && !std::strcmp(uri, LV2_UI__idleInterface)) return &kIdleInterface;
  return nullptr;
}

const LV2UI_Descriptor kUiDescriptor = {kUiUri, uiInstantiate, uiCleanup,
                                        uiPortEvent, uiExtensionData};

} // namespace

LV2_SYMBOL_EXPORT const LV2UI_Descriptor* lv2ui_descriptor(uint32_t index) {
  return index == 0 ? &kUiDescriptor : nullptr;
}
