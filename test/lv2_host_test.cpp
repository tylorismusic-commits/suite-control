// Suite Control — minimal LV2 host smoke test
// SPDX-License-Identifier: GPL-3.0-or-later
#include <lv2/atom/atom.h>
#include <lv2/core/lv2.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>
#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "console_state.hpp"
#include "params.hpp"

namespace {
int failures = 0;
bool check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL  %s\n", what);
    ++failures;
  }
  return ok;
}

constexpr uint32_t kFirstParam = 4;
constexpr uint32_t kMeterBase = kFirstParam + suite::P_COUNT;

struct UridMap {
  LV2_URID_Map map{};
  std::map<std::string, uint32_t> ids;

  UridMap() {
    map.handle = this;
    map.map = &UridMap::mapUri;
  }
  static LV2_URID mapUri(LV2_URID_Map_Handle handle, const char* uri) {
    auto* self = static_cast<UridMap*>(handle);
    auto it = self->ids.find(uri);
    if (it != self->ids.end()) return it->second;
    const uint32_t id = static_cast<uint32_t>(self->ids.size() + 1);
    self->ids.emplace(uri, id);
    return id;
  }
};

struct Captured {
  bool called = false;
  uint32_t key = 0;
  uint32_t type = 0;
  uint32_t flags = 0;
  std::vector<uint8_t> data;
};

LV2_State_Status captureStore(LV2_State_Handle handle, uint32_t key,
                              const void* value, size_t size, uint32_t type,
                              uint32_t flags) {
  auto* out = static_cast<Captured*>(handle);
  out->called = true;
  out->key = key;
  out->type = type;
  out->flags = flags;
  const auto* bytes = static_cast<const uint8_t*>(value);
  out->data.assign(bytes, bytes + size);
  return LV2_STATE_SUCCESS;
}

const void* captureRetrieve(LV2_State_Handle handle, uint32_t key, size_t* size,
                            uint32_t* type, uint32_t* flags) {
  auto* in = static_cast<Captured*>(handle);
  if (!in->called || key != in->key) return nullptr;
  *size = in->data.size();
  *type = in->type;
  *flags = in->flags;
  return in->data.data();
}
} // namespace

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "build/lv2/suite-control.lv2/suite_control.so";
  void* lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!check(lib != nullptr, dlerror())) return 1;
  auto getDesc = reinterpret_cast<const LV2_Descriptor* (*)(uint32_t)>(
      dlsym(lib, "lv2_descriptor"));
  if (!check(getDesc != nullptr, "lv2_descriptor")) return 1;
  const LV2_Descriptor* desc = getDesc(0);
  if (!check(desc != nullptr, "descriptor 0")) return 1;
  std::printf("loaded: %s\n", desc->URI);

  UridMap urid;
  LV2_Feature mapFeature{LV2_URID__map, &urid.map};
  const LV2_Feature* features[] = {&mapFeature, nullptr};

  LV2_Handle handle = desc->instantiate(desc, 48000.0, path, features);
  if (!check(handle != nullptr, "instantiate")) return 1;

  constexpr uint32_t kFrames = 256;
  std::vector<float> inL(kFrames), inR(kFrames), outL(kFrames), outR(kFrames);
  for (uint32_t i = 0; i < kFrames; ++i) {
    inL[i] = inR[i] = 0.2f * std::sin(2.0f * 3.14159265f * 220.0f * i / 48000.0f);
  }
  float params[suite::P_COUNT];
  for (uint32_t i = 0; i < suite::P_COUNT; ++i) {
    params[i] = static_cast<float>(suite::kParamSpecs[i].def);
  }
  params[suite::P_COMP_THRESHOLD] = 0.95f;  // heavy compression
  params[suite::P_COMP_RATIO] = 2.0f;
  float meters[6] = {-120, -120, -120, -120, -120, 0};

  desc->connect_port(handle, 0, inL.data());
  desc->connect_port(handle, 1, inR.data());
  desc->connect_port(handle, 2, outL.data());
  desc->connect_port(handle, 3, outR.data());
  for (uint32_t i = 0; i < suite::P_COUNT; ++i) {
    desc->connect_port(handle, kFirstParam + i, &params[i]);
  }
  for (uint32_t i = 0; i < 6; ++i) {
    desc->connect_port(handle, kMeterBase + i, &meters[i]);
  }

  desc->activate(handle);
  for (int block = 0; block < 32; ++block) {
    desc->run(handle, kFrames);
  }
  bool finite = true;
  double energy = 0.0;
  for (uint32_t i = 0; i < kFrames; ++i) {
    if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) finite = false;
    energy += static_cast<double>(outL[i]) * outL[i];
  }
  check(finite, "output is finite");
  check(energy > 0.0, "output has signal");
  std::printf("gr=%.1f outL=%.1f outR=%.1f\n", meters[0], meters[3], meters[4]);
  check(meters[0] > 0.0f, "compressor reports gain reduction");

  // ---------------------------------------------------------- LV2 state
  const auto* state = static_cast<const LV2_State_Interface*>(
      desc->extension_data(LV2_STATE__interface));
  check(state != nullptr, "advertises the LV2 state interface");

  if (state) {
    suite::ConsoleBridge bridge;
    check(bridge.open(), "console bridge maps");
    suite::ConsoleTable table{};
    std::strcpy(table.names[0], "KICK");
    std::strcpy(table.names[1], "SNARE");
    table.muted[1] = 1;
    table.gainForced[1] = 1;
    table.savedGain[1] = 0.75f;
    bridge.write(table);

    LV2_Handle a = desc->instantiate(desc, 48000.0, path, features);
    Captured first;
    check(state->save(a, captureStore, &first, 0, features) ==
              LV2_STATE_SUCCESS &&
              first.called,
          "state save stores the console table");

    LV2_Handle b = desc->instantiate(desc, 48000.0, path, features);
    check(state->restore(b, captureRetrieve, &first, 0, features) ==
              LV2_STATE_SUCCESS,
          "state restore accepts its own blob");

    suite::ConsoleTable back{};
    check(bridge.read(back), "bridge readable after restore");
    char label[128];
    std::snprintf(label, sizeof(label), "name restored (%s)", back.names[0]);
    check(std::strcmp(back.names[0], "KICK") == 0, label);
    check(back.muted[1] == 1 && back.gainForced[1] == 1 &&
              std::fabs(back.savedGain[1] - 0.75f) < 1e-6f,
          "mute state restored");

    Captured second;
    check(state->save(b, captureStore, &second, 0, features) ==
              LV2_STATE_SUCCESS,
          "state saves again after restore");
    check(second.data == first.data, "save-restore-save is stable");

    desc->cleanup(a);
    desc->cleanup(b);
  }

  desc->deactivate(handle);
  desc->cleanup(handle);
  dlclose(lib);
  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
