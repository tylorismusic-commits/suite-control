// Suite Channel — minimal CLAP host smoke test
// SPDX-License-Identifier: GPL-3.0-or-later
#include <clap/all.h>
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "params.hpp"

namespace {

int gFailures = 0;

uint32_t inSize(const clap_input_events_t*) { return 0; }
const clap_event_header_t* inGet(const clap_input_events_t*, uint32_t) {
  return nullptr;
}
bool outPush(const clap_output_events_t*, const clap_event_header_t*) {
  return true;
}

const clap_host_t kHost = [] {
  clap_host_t h{};
  h.clap_version = CLAP_VERSION;
  h.name = "suite-channel-test";
  h.vendor = "tylorismusic";
  h.url = "https://example.invalid";
  h.version = "0";
  return h;
}();

bool check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++gFailures;
  }
  return ok;
}

} // namespace

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "build/clap/SuiteChannel.clap";
  void* lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!check(lib != nullptr, dlerror())) {
    return 1;
  }
  auto* entry = static_cast<const clap_plugin_entry_t*>(
      dlsym(lib, "clap_entry"));
  if (!check(entry != nullptr, "clap_entry")) {
    return 1;
  }
  if (!check(entry->init(path), "entry init")) {
    return 1;
  }
  const auto* factory = static_cast<const clap_plugin_factory_t*>(
      entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  if (!check(factory != nullptr, "factory")) {
    return 1;
  }
  const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);
  if (!check(desc != nullptr, "descriptor")) {
    return 1;
  }
  std::printf("loaded: %s (%s)\n", desc->name, desc->id);

  const clap_plugin_t* plugin = factory->create_plugin(factory, &kHost, desc->id);
  if (!check(plugin != nullptr, "create_plugin")) {
    return 1;
  }
  if (!check(plugin->init(plugin), "plugin init")) {
    return 1;
  }

  const auto* params = static_cast<const clap_plugin_params_t*>(
      plugin->get_extension(plugin, CLAP_EXT_PARAMS));
  if (!check(params != nullptr, "params extension")) {
    return 1;
  }
  if (check(params->count(plugin) == static_cast<uint32_t>(suite::P_COUNT),
            "parameter count")) {
    std::printf("parameters: %u\n", params->count(plugin));
  }

  if (!check(plugin->activate(plugin, 48000.0, 1, 512), "activate")) {
    return 1;
  }
  plugin->start_processing(plugin);

  std::vector<float> l(512), r(512), ol(512), orr(512);
  for (int i = 0; i < 512; ++i) {
    l[i] = 0.1f * std::sin(2.0f * 3.14159265f * 1000.0f * i / 48000.0f);
    r[i] = l[i];
  }
  float* inPtrs[2] = {l.data(), r.data()};
  float* outPtrs[2] = {ol.data(), orr.data()};
  clap_audio_buffer_t in{};
  in.data32 = inPtrs;
  in.channel_count = 2;
  clap_audio_buffer_t out{};
  out.data32 = outPtrs;
  out.channel_count = 2;
  clap_process_t proc{};
  proc.frames_count = 512;
  proc.audio_inputs = &in;
  proc.audio_outputs = &out;
  proc.audio_inputs_count = 1;
  proc.audio_outputs_count = 1;
  clap_input_events_t inEvents{};
  inEvents.ctx = nullptr;
  inEvents.size = inSize;
  inEvents.get = inGet;
  clap_output_events_t outEvents{};
  outEvents.ctx = nullptr;
  outEvents.try_push = outPush;
  proc.in_events = &inEvents;
  proc.out_events = &outEvents;

  check(plugin->process(plugin, &proc) == CLAP_PROCESS_CONTINUE, "process");
  for (int i = 0; i < 512; ++i) {
    if (!std::isfinite(ol[i]) || !std::isfinite(orr[i])) {
      check(false, "finite output");
      break;
    }
  }

  const auto* latency = static_cast<const clap_plugin_latency_t*>(
      plugin->get_extension(plugin, CLAP_EXT_LATENCY));
  if (latency) {
    std::printf("latency: %u samples\n", latency->get(plugin));
  }

  // Exercise the editor under the host's X11 display (normally Xvfb).
  const auto* gui = static_cast<const clap_plugin_gui_t*>(
      plugin->get_extension(plugin, CLAP_EXT_GUI));
  if (gui && gui->is_api_supported &&
      gui->is_api_supported(plugin, CLAP_WINDOW_API_X11, false)) {
    uint32_t gw = 0;
    uint32_t gh = 0;
    if (gui->get_size && gui->get_size(plugin, &gw, &gh)) {
      std::printf("gui size: %ux%u\n", gw, gh);
    }
    Display* dpy = XOpenDisplay(nullptr);
    if (dpy) {
      const Window win = XCreateSimpleWindow(
          dpy, DefaultRootWindow(dpy), 0, 0, 700, 920, 0, 0, 0);
      XMapWindow(dpy, win);
      XFlush(dpy);
      clap_window_t cw{};
      cw.api = CLAP_WINDOW_API_X11;
      cw.x11 = win;
      if (gui->create(plugin, CLAP_WINDOW_API_X11, false)) {
        if (gui->set_parent) {
          check(gui->set_parent(plugin, &cw), "gui set_parent");
        }
        gui->show(plugin);
        XFlush(dpy);
        usleep(200000);
        gui->hide(plugin);
        gui->destroy(plugin);
        std::printf("gui: opened\n");
      }
      XDestroyWindow(dpy, win);
      XCloseDisplay(dpy);
    }
  }

  plugin->stop_processing(plugin);
  plugin->deactivate(plugin);
  plugin->destroy(plugin);
  entry->deinit();
  dlclose(lib);
  std::printf("suite_clap_host: ok\n");
  if (gFailures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", gFailures);
    return 1;
  }
  return 0;
}
