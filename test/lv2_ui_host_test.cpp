// Suite Control — LV2 UI host test (X11 + Xvfb)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Loads suite_control_ui.so, embeds it in a parent window and checks the two
// things a host actually decides:
//
//   * the UI paints even when the host never calls the idle interface
//     (REAPER does not implement it), including when the frame is discarded
//     because the FX container was still unmapped at instantiate time;
//   * a reparented window repaints by itself.
//
// An all-black grab is the exact symptom users see, so that is the assertion.
#include <dlfcn.h>
#include <lv2/ui/ui.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

#include "params.hpp"
#include "ui/panel.hpp"

namespace {
int failures = 0;
void check(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

struct HostResize {
  LV2UI_Resize api{};
  int lastWidth = 0;
  int lastHeight = 0;
  int calls = 0;
};
int onResize(LV2UI_Feature_Handle handle, int w, int h) {
  auto* h2 = static_cast<HostResize*>(handle);
  h2->lastWidth = w;
  h2->lastHeight = h;
  ++h2->calls;
  return 0;
}
void onWrite(LV2UI_Controller, uint32_t, uint32_t, uint32_t, const void*) {}

bool paints(Display* dpy, Window win, int w, int h) {
  XImage* img = XGetImage(dpy, win, 0, 0, static_cast<unsigned>(w),
                          static_cast<unsigned>(h), AllPlanes, ZPixmap);
  if (!img) return false;
  bool painted = false;
  for (int y = 0; y < img->height && !painted; y += 8) {
    for (int x = 0; x < img->width; x += 8) {
      if (XGetPixel(img, x, y) != 0) {
        painted = true;
        break;
      }
    }
  }
  XDestroyImage(img);
  return painted;
}

// Wait for the UI's own frame loop to paint, without calling idle().
bool paintsWithin(Display* dpy, Window win, int w, int h, int millis) {
  const auto end = std::chrono::steady_clock::now() +
                   std::chrono::milliseconds(millis);
  while (std::chrono::steady_clock::now() < end) {
    XSync(dpy, False);
    if (paints(dpy, win, w, h)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}
} // namespace

int main(int argc, char** argv) {
  const std::string bundle =
      argc > 1 ? argv[1] : "build/lv2/suite-control.lv2";
  const std::string library = bundle + "/suite_control_ui.so";
  void* module = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!module) {
    std::printf("FAIL  cannot load %s: %s\n", library.c_str(), dlerror());
    return 1;
  }
  check(true, "UI library loads");

  auto descriptor = reinterpret_cast<const LV2UI_Descriptor* (*)(uint32_t)>(
      dlsym(module, "lv2ui_descriptor"));
  check(descriptor != nullptr, "lv2ui_descriptor is exported");
  if (!descriptor) return 1;
  const LV2UI_Descriptor* ui = descriptor(0);
  check(ui != nullptr, "UI descriptor index 0 exists");
  if (!ui) return 1;

  Display* dpy = XOpenDisplay(nullptr);
  if (!dpy) {
    std::printf("FAIL  cannot open display (run under xvfb-run)\n");
    return 1;
  }
  const int screen = DefaultScreen(dpy);

  // REAPER hands over a parent inside the FX window that is not mapped yet, so
  // the first frame is thrown away when the parent appears.  The UI must paint
  // again on its own.
  Window parent = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, 1400,
                                      820, 0, 0, 0);
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
      ui, "https://github.com/tylorismusic-commits/suite-control",
      (bundle + "/").c_str(), &onWrite, nullptr, &widget, features);
  check(handle != nullptr, "UI instantiates with a parent window");
  if (!handle) return 1;
  check(widget != nullptr, "UI returns an X11 widget");
  check(host.calls > 0, "UI asks the host for its preferred size");
  check(host.lastWidth == suite::kPanelWidth &&
            host.lastHeight == suite::kPanelHeight,
        "preferred size is the 1280x760 logical layout");

  const Window window =
      static_cast<Window>(reinterpret_cast<uintptr_t>(widget));

  // The frame painted while the parent is unmapped is discarded by the server.
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  XMapWindow(dpy, parent);
  XSync(dpy, False);

  // No idle() call here on purpose: that is how REAPER behaves.
  check(paintsWithin(dpy, window, host.lastWidth, host.lastHeight, 1500),
        "paints without the host calling idle (REAPER's path)");

  const auto* idle = static_cast<const LV2UI_Idle_Interface*>(
      ui->extension_data(LV2_UI__idleInterface));
  check(idle != nullptr, "UI offers the idle interface");
  bool idled = true;
  for (int i = 0; i < 5; ++i) {
    if (idle && idle->idle(handle) != 0) idled = false;
  }
  check(idled, "idle() is safe when the host does call it");

  // A reparented widget must repaint into its new parent.
  Window mover = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, 1400,
                                     820, 0, 0, 0);
  XMapWindow(dpy, mover);
  XReparentWindow(dpy, window, mover, 0, 0);
  XSync(dpy, False);
  check(paintsWithin(dpy, window, host.lastWidth, host.lastHeight, 1500),
        "paints after the host reparents the widget");

  // A parameter change from the host must be accepted and redrawn.
  {
    const float v = 0.3f;
    ui->port_event(handle, 4, sizeof(float), 0, &v);
    check(paintsWithin(dpy, window, host.lastWidth, host.lastHeight, 1000),
          "redraws after a port event");
  }

  ui->cleanup(handle);
  dlclose(module);
  XDestroyWindow(dpy, mover);
  XDestroyWindow(dpy, parent);
  XCloseDisplay(dpy);
  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
