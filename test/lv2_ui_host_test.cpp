// Suite Channel — LV2 UI host test (X11 + Xvfb)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Loads suite_channel_plus_ui.so directly, embeds it in a parent window and drives it
// through the idle interface the way a real host does.  This is the only
// automated coverage for the resize logic: without it, a broken ui_resize or
// a stale idle interface would only be caught by hand in Ardour.

#include <dlfcn.h>
#include <lv2/ui/ui.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <chrono>

#include "params.hpp"

namespace {

int gFailures = 0;

void check(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++gFailures;
  }
}

struct HostResize {
  LV2UI_Resize api{};
  int lastWidth = 0;
  int lastHeight = 0;
  int calls = 0;
};

int onResize(void* handle, int width, int height) {
  auto* host = static_cast<HostResize*>(handle);
  host->lastWidth = width;
  host->lastHeight = height;
  ++host->calls;
  return 0;
}

void onWrite(LV2UI_Controller, uint32_t, uint32_t, uint32_t, const void*) {}

using DescriptorFn = const LV2UI_Descriptor* (*)(uint32_t);

} // namespace

int main(int argc, char** argv) {
  const std::string bundle = argc > 1 ? argv[1] : "build/lv2/suite-channel.lv2";
  const std::string library = bundle + "/suite_channel_plus_ui.so";

  void* module = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!module) {
    std::printf("FAIL  cannot load %s: %s\n", library.c_str(), dlerror());
    return 1;
  }
  check(true, "UI library loads");

  auto descriptor =
      reinterpret_cast<DescriptorFn>(dlsym(module, "lv2ui_descriptor"));
  check(descriptor != nullptr, "lv2ui_descriptor is exported");
  if (!descriptor) {
    return 1;
  }

  const LV2UI_Descriptor* ui = descriptor(0);
  check(ui != nullptr, "UI descriptor index 0 exists");
  if (!ui) {
    return 1;
  }

  Display* dpy = XOpenDisplay(nullptr);
  if (!dpy) {
    std::printf("FAIL  cannot open display (run under xvfb-run)\n");
    return 1;
  }

  const int screen = DefaultScreen(dpy);
  Window parent = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0,
                                      1200, 800, 0, 0, 0);
  XMapWindow(dpy, parent);
  XFlush(dpy);

  HostResize host;
  host.api.handle = &host;
  host.api.ui_resize = &onResize;
  float scale = 1.0f;

  LV2_Feature parentFeature{LV2_UI__parent,
                            reinterpret_cast<void*>(
                                static_cast<uintptr_t>(parent))};
  LV2_Feature resizeFeature{LV2_UI__resize, &host.api};
  LV2_Feature scaleFeature{LV2_UI__scaleFactor, &scale};
  const LV2_Feature* features[] = {&parentFeature, &resizeFeature,
                                   &scaleFeature, nullptr};

  LV2UI_Widget widget = nullptr;
  LV2UI_Handle handle = ui->instantiate(
      ui, "https://github.com/tylorismusic-commits/suite-channel",
      (bundle + "/").c_str(), &onWrite, nullptr, &widget, features);
  check(handle != nullptr, "UI instantiates with a parent window");
  if (!handle) {
    return 1;
  }
  check(widget != nullptr, "UI returns an X11 widget");
  check(host.calls > 0, "UI asks the host for its preferred size");
  check(host.lastWidth == 700 && host.lastHeight == 920,
        "preferred size is the 700x920 logical layout");

  const auto* idle = static_cast<const LV2UI_Idle_Interface*>(
      ui->extension_data(LV2_UI__idleInterface));
  check(idle != nullptr, "UI provides the idle interface");
  check(ui->extension_data("http://lv2plug.in/plugins/not-a-thing") == nullptr,
        "unknown extension data is refused");

  const Window window =
      static_cast<Window>(reinterpret_cast<uintptr_t>(widget));

  // Ardour/suil only allows shrinking the plugin window when the X11 window
  // advertises a PMinSize hint; otherwise the minimum equals the current size
  // and resizing is grow-only.
  {
    XSizeHints hints{};
    long supplied = 0;
    const bool ok = XGetWMNormalHints(dpy, window, &hints, &supplied) != 0;
    check(ok && (hints.flags & PMinSize) != 0 && hints.min_width == 350 &&
              hints.min_height == 460,
          "PMinSize hint advertises the 0.5x minimum");
    check(ok && (hints.flags & PMaxSize) != 0 && hints.max_width == 1750 &&
              hints.max_height == 2300,
          "PMaxSize hint advertises the 2.5x maximum");
  }

  bool idled = true;
  if (idle) {
    for (int i = 0; i < 5; ++i) {
      if (idle->idle(handle) != 0) {
        idled = false;
      }
    }
  }
  check(idled, "idle() returns 0 and draws without crashing");

  // Feed a gain-reduction value the way the host would, then idle again.
  if (idle) {
    const float gr = 6.5f;
    ui->port_event(handle, 4 + suite::P_COUNT, sizeof(float), 0, &gr);
    idle->idle(handle);
  }

  // ---------------------------------------------------------------------
  // The host owns the size.  Adopting it must not bounce a resize back
  // through the host, or suil re-allocates and the window flickers/loops.
  // ---------------------------------------------------------------------
  if (idle) {
    host.calls = 0;
    XResizeWindow(dpy, window, 1600, 800);
    XSync(dpy, False);
    for (int i = 0; i < 5; ++i) {
      idle->idle(handle);
    }
    XWindowAttributes attrs{};
    XGetWindowAttributes(dpy, window, &attrs);
    char label[128];
    std::snprintf(label, sizeof(label),
                  "growing follows the host allocation (got %dx%d)",
                  attrs.width, attrs.height);
    check(attrs.width == 1600 && attrs.height == 800, label);
    check(host.calls == 0, "growing does not call back into the host resize");

    XResizeWindow(dpy, window, 700, 420);
    XSync(dpy, False);
    for (int i = 0; i < 5; ++i) {
      idle->idle(handle);
    }
    XGetWindowAttributes(dpy, window, &attrs);
    std::snprintf(label, sizeof(label),
                  "shrinking follows the host allocation (got %dx%d)",
                  attrs.width, attrs.height);
    check(attrs.width == 700 && attrs.height == 420, label);
    check(host.calls == 0, "shrinking does not call back into the host resize");
  }

  // A degenerate allocation must not crash or wedge the idle loop.
  if (idle) {
    XResizeWindow(dpy, window, 40, 20);
    XSync(dpy, False);
    for (int i = 0; i < 5; ++i) {
      idle->idle(handle);
    }
    check(true, "degenerate allocation is survivable");
  }

  // The idle caller runs from the host's main loop, so it must be cheap when
  // nothing changed.  500 calls should be dominated by the 33 ms throttle.
  if (idle) {
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 500; ++i) {
      idle->idle(handle);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    char label[128];
    std::snprintf(label, sizeof(label),
                  "500 idle calls stay throttled (%lld ms)",
                  static_cast<long long>(elapsed.count()));
    check(elapsed.count() < 500, label);
  }

  ui->cleanup(handle);
  XDestroyWindow(dpy, parent);
  XCloseDisplay(dpy);
  dlclose(module);
  std::printf("%d failure(s)\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
