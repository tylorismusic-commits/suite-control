// Suite Control — end-to-end peer test against a real Suite Channel Plus
// instance loaded in the same process.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <clap/all.h>
#include <dlfcn.h>

#include <chrono>
#include <cmath>
#include <thread>
#include <cstdio>
#include <cstring>
#include <vector>

#include "peer.hpp"

namespace {
int failures = 0;
bool check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL  %s\n", what);
    ++failures;
  } else {
    std::printf("PASS  %s\n", what);
  }
  return ok;
}

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
  h.name = "suite-peer-integration";
  h.vendor = "tylorismusic";
  h.url = "https://example.invalid";
  h.version = "0";
  return h;
}();

clap_process_status process(const clap_plugin_t* plugin) {
  std::vector<float> l(256, 0.0f), r(256, 0.0f);
  float* chans[2] = {l.data(), r.data()};
  clap_audio_buffer_t inBuf{};
  inBuf.data32 = chans;
  inBuf.channel_count = 2;
  clap_audio_buffer_t outBuf{};
  outBuf.data32 = chans;
  outBuf.channel_count = 2;
  clap_input_events_t in{nullptr, inSize, inGet};
  clap_output_events_t out{nullptr, outPush};
  clap_process_t proc{};
  proc.frames_count = 256;
  proc.audio_inputs = &inBuf;
  proc.audio_inputs_count = 1;
  proc.audio_outputs = &outBuf;
  proc.audio_outputs_count = 1;
  proc.in_events = &in;
  proc.out_events = &out;
  return plugin->process(plugin, &proc);
}
} // namespace

int main(int argc, char** argv) {
  const char* plusPath = argc > 1 ? argv[1] : "SuiteChannelPlus.clap";
  void* lib = dlopen(plusPath, RTLD_NOW | RTLD_LOCAL);
  if (!check(lib != nullptr, dlerror())) return 1;
  auto* entry =
      static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
  if (!check(entry && entry->init(plusPath), "plus entry")) return 1;
  const auto* factory = static_cast<const clap_plugin_factory_t*>(
      entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);
  const clap_plugin_t* plugin =
      factory->create_plugin(factory, &kHost, desc->id);
  if (!check(plugin && plugin->init(plugin), "plus instance")) return 1;
  if (!check(plugin->activate(plugin, 48000.0, 16, 256), "plus activate")) {
    return 1;
  }
  process(plugin);
  process(plugin);

  const auto* params = static_cast<const clap_plugin_params_t*>(
      plugin->get_extension(plugin, CLAP_EXT_PARAMS));
  if (!check(params != nullptr, "plus params")) return 1;

  suite::peer::Client control;
  auto* seg = control.segment();
  if (!check(seg != nullptr, "control attaches to the peer bus")) return 1;

  int slot = -1;
  for (int i = 0; i < suite::peer::kSlots; ++i) {
    if (seg->slot[i].used.load() &&
        std::strcmp(seg->slot[i].name, "Suite Channel Plus") == 0) {
      slot = i;
    }
  }
  if (!check(slot >= 0, "plus instance registers a slot")) return 1;

  // The surface writes INPUT; the plug-in must adopt it.
  suite::peer::Client::writeRemote(seg->slot[slot], 1, 0.91);
  // The peer timer thread adopts remote writes; give it a cycle.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  process(plugin);
  double v = 0.0;
  check(params->get_value(plugin, 1, &v), "plus reads input");
  std::printf("input after remote write: %.3f\n", v);
  check(std::fabs(v - 0.91) < 1e-6, "remote write reaches the plus parameter");

  // And the plug-in publishes its own value so the surface can display it.
  double published = seg->slot[slot].param[1].value.load();
  check(std::fabs(published - 0.91) < 1e-6, "plus mirrors the value back");

  plugin->deactivate(plugin);
  check(!suite::peer::Client::alive(seg->slot[slot]),
        "deactivate drops the slot");
  plugin->destroy(plugin);
  entry->deinit();
  dlclose(lib);
  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
