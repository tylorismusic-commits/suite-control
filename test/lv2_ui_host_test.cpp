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
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

#include "params.hpp"
#include "peer.hpp"

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

std::mutex gWriteMutex;
std::vector<std::pair<uint32_t, float>> gWrites;

void onWrite(LV2UI_Controller, uint32_t port, uint32_t size, uint32_t,
             const void* data) {
  if (size != sizeof(float) || !data) return;
  std::lock_guard<std::mutex> lock(gWriteMutex);
  gWrites.emplace_back(port, *static_cast<const float*>(data));
}

bool sawWrite(uint32_t port, float expected, int millis) {
  const auto end = std::chrono::steady_clock::now() +
                   std::chrono::milliseconds(millis);
  while (std::chrono::steady_clock::now() < end) {
    {
      std::lock_guard<std::mutex> lock(gWriteMutex);
      for (const auto& w : gWrites) {
        if (w.first == port && std::fabs(w.second - expected) < 0.01f) {
          return true;
        }
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

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
  // Like REAPER: the FX container is not mapped yet when the UI is created, so
  // the first frame is discarded.  The UI has to repaint on its own because
  // REAPER never calls the idle interface.
  Window parent = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0,
                                      1200, 800, 0, 0, 0);
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

  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  XMapWindow(dpy, parent);
  XSync(dpy, False);

  auto painted = [&]() {
    XImage* img = XGetImage(dpy, window, 0, 0,
                            static_cast<unsigned>(host.lastWidth),
                            static_cast<unsigned>(host.lastHeight), AllPlanes,
                            ZPixmap);
    bool any = false;
    if (img) {
      for (int y = 0; y < img->height && !any; y += 8) {
        for (int x = 0; x < img->width; x += 8) {
          if (XGetPixel(img, x, y) != 0) {
            any = true;
            break;
          }
        }
      }
      XDestroyImage(img);
    }
    return any;
  };
  auto paintedWithin = [&](int millis) {
    const auto end = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds(millis);
    while (std::chrono::steady_clock::now() < end) {
      XSync(dpy, False);
      if (painted()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
  };

  // No idle() call: this is the host path that left Suite Control black.
  check(paintedWithin(1500),
        "paints without the host calling idle (REAPER's path)");

  bool idled = true;
  if (idle) {
    for (int i = 0; i < 5; ++i) {
      if (idle->idle(handle) != 0) {
        idled = false;
      }
    }
  }
  check(idled, "idle() is safe when the host does call it");

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
  {
    host.calls = 0;
    XResizeWindow(dpy, window, 1600, 800);
    XSync(dpy, False);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
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
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    XGetWindowAttributes(dpy, window, &attrs);
    std::snprintf(label, sizeof(label),
                  "shrinking follows the host allocation (got %dx%d)",
                  attrs.width, attrs.height);
    check(attrs.width == 700 && attrs.height == 420, label);
    check(host.calls == 0, "shrinking does not call back into the host resize");
  }

  // ---------------------------------------------------------------------
  // Remote edits (Suite Control) never reach the host, so the plug-in tells
  // the UI which peer slot it owns; the UI must then mirror that slot and
  // push the mirrored values back to the host.
  // ---------------------------------------------------------------------
  {
    suite::peer::Client client;
    const int slotIndex =
        client.registerSlot(1, "Suite Channel Plus", suite::P_COUNT);
    check(slotIndex >= 0, "peer slot registers for the mirror test");
    client.publish(1, 0.9);
    client.beat();

    const float slot = static_cast<float>(slotIndex + 1);
    ui->port_event(handle, 4 + suite::P_COUNT + 7, sizeof(float), 0, &slot);

    char label[128];
    const bool mirrored = sawWrite(4 + 1, 0.9f, 2500);
    std::snprintf(label, sizeof(label),
                  "mirrors remote edits into the host%s", "");
    check(mirrored, label);
    client.unregisterSlot();
  }

  // A degenerate allocation must not crash or wedge the idle loop.
  {
    XResizeWindow(dpy, window, 40, 20);
    XSync(dpy, False);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    check(true, "degenerate allocation is survivable");
  }

  // If the host does call idle from its main loop it must stay cheap: the
  // frame loop is ours now, so idle only nudges it.
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
