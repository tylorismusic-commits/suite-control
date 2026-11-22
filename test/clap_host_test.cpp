// Suite Control — minimal CLAP host smoke test
// SPDX-License-Identifier: GPL-3.0-or-later
#include <clap/all.h>
#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <vector>

#include "params.hpp"

namespace {
int failures = 0;
bool check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
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
  h.name = "suite-control-test";
  h.vendor = "tylorismusic";
  h.url = "https://example.invalid";
  h.version = "0";
  return h;
}();

struct MemStream {
  std::vector<char> data;
  size_t read = 0;
};
int64_t memWrite(const clap_ostream_t* s, const void* buf, uint64_t size) {
  auto* m = static_cast<MemStream*>(s->ctx);
  m->data.insert(m->data.end(), static_cast<const char*>(buf),
                 static_cast<const char*>(buf) + size);
  return static_cast<int64_t>(size);
}
int64_t memRead(const clap_istream_t* s, void* buf, uint64_t size) {
  auto* m = static_cast<MemStream*>(s->ctx);
  const size_t left = m->data.size() - m->read;
  const size_t n = static_cast<size_t>(std::min<uint64_t>(size, left));
  std::memcpy(buf, m->data.data() + m->read, n);
  m->read += n;
  return static_cast<int64_t>(n);
}
} // namespace

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "build/clap/SuiteControl.clap";
  void* lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!check(lib != nullptr, dlerror())) return 1;
  auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
  if (!check(entry != nullptr, "clap_entry")) return 1;
  if (!check(entry->init(path), "entry init")) return 1;
  const auto* factory = static_cast<const clap_plugin_factory_t*>(
      entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  if (!check(factory != nullptr, "factory")) return 1;
  const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);
  if (!check(desc != nullptr, "descriptor")) return 1;
  std::printf("loaded: %s (%s)\n", desc->name, desc->id);

  const clap_plugin_t* plugin = factory->create_plugin(factory, &kHost, desc->id);
  if (!check(plugin != nullptr, "create_plugin")) return 1;
  if (!check(plugin->init(plugin), "plugin init")) return 1;

  const auto* params = static_cast<const clap_plugin_params_t*>(
      plugin->get_extension(plugin, CLAP_EXT_PARAMS));
  if (!check(params != nullptr, "params extension")) return 1;
  check(params->count(plugin) == suite::P_COUNT, "parameter count");

  const auto* state = static_cast<const clap_plugin_state_t*>(
      plugin->get_extension(plugin, CLAP_EXT_STATE));
  if (!check(state != nullptr, "state extension")) return 1;
  MemStream stream;
  clap_ostream_t os{&stream, memWrite};
  check(state->save(plugin, &os), "state save");
  stream.read = 0;
  clap_istream_t is{&stream, memRead};
  check(state->load(plugin, &is), "state load");

  if (!check(plugin->activate(plugin, 48000.0, 64, 2048), "activate")) return 1;
  check(plugin->start_processing(plugin), "start_processing");

  std::vector<float> l(256), r(256);
  for (int i = 0; i < 256; ++i) {
    l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
        0.2f * std::sin(2.0f * 3.14159265f * 220.0f * i / 48000.0f);
  }
  float* chans[2] = {l.data(), r.data()};
  clap_audio_buffer_t inBuf{};
  inBuf.data32 = chans;
  inBuf.channel_count = 2;
  clap_audio_buffer_t outBuf{};
  outBuf.data32 = chans;
  outBuf.channel_count = 2;
  clap_input_events_t events{nullptr, inSize, inGet};
  clap_output_events_t outEvents{nullptr, outPush};
  clap_process_t proc{};
  proc.frames_count = 256;
  proc.audio_inputs = &inBuf;
  proc.audio_inputs_count = 1;
  proc.audio_outputs = &outBuf;
  proc.audio_outputs_count = 1;
  proc.in_events = &events;
  proc.out_events = &outEvents;
  for (int block = 0; block < 8; ++block) {
    if (plugin->process(plugin, &proc) == CLAP_PROCESS_ERROR) {
      check(false, "process");
      return 1;
    }
  }
  // Name table round-trip: a v2 state with a name must load and save back.
  {
    std::vector<char> buf;
    auto put = [&](const void* p, size_t n) {
      buf.insert(buf.end(), static_cast<const char*>(p),
                 static_cast<const char*>(p) + n);
    };
    double values[suite::P_COUNT] = {};
    put(values, sizeof(values));
    const uint32_t magic = 0x53434e4d, ver = 2, count = suite::Shared::kNameSlots;
    put(&magic, sizeof(magic));
    put(&ver, sizeof(ver));
    put(&count, sizeof(count));
    for (uint32_t i = 0; i < count; ++i) {
      char name[suite::Shared::kNameBytes] = {};
      if (i == 5) std::snprintf(name, sizeof(name), "GTR L");
      put(name, sizeof(name));
      const uint8_t m = 0, forced = 0;
      put(&m, 1);
      put(&forced, 1);
      const float g = 0.0f;
      put(&g, sizeof(g));
    }
    MemStream in;
    in.data = buf;
    clap_istream_t is2{&in, memRead};
    check(state->load(plugin, &is2), "v2 state loads");
    MemStream out;
    clap_ostream_t os2{&out, memWrite};
    check(state->save(plugin, &os2), "state saves again");
    const char needle[] = "GTR L";
    const bool found =
        std::search(out.data.begin(), out.data.end(), needle, needle + 5) !=
        out.data.end();
    check(found, "names survive a state round-trip");
  }

  bool finite = true;
  for (int i = 0; i < 256; ++i) {
    if (!std::isfinite(l[static_cast<size_t>(i)]) ||
        !std::isfinite(r[static_cast<size_t>(i)])) {
      finite = false;
    }
  }
  check(finite, "output is finite");

  plugin->stop_processing(plugin);
  plugin->deactivate(plugin);
  plugin->destroy(plugin);
  entry->deinit();
  dlclose(lib);
  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
