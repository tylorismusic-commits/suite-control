// Suite Channel — minimal LV2 host smoke test
// SPDX-License-Identifier: GPL-3.0-or-later
#include <lv2/atom/atom.h>
#include <lv2/core/lv2.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>

#include <dlfcn.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "params.hpp"

namespace {

// Port layout from src/suite_channel_plus_lv2.cpp.
enum Port : uint32_t {
  IN_L = 0,
  IN_R = 1,
  OUT_L = 2,
  OUT_R = 3,
  FIRST_PARAM = 4,
  GR = FIRST_PARAM + suite::P_COUNT,
  OUT_PEAK_L,
  OUT_PEAK_R,
  IN_PEAK_L,
  IN_PEAK_R,
  LATENCY,
  PORT_COUNT
};

bool check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
  }
  return ok;
}

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

double blobValue(const std::vector<uint8_t>& blob, uint32_t index) {
  double value = 0.0;
  if (blob.size() < 12 + (index + 1) * sizeof(double)) return -1.0;
  std::memcpy(&value, blob.data() + 12 + index * sizeof(double), sizeof(double));
  return value;
}

} // namespace

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "build/lv2/suite-channel-plus.lv2/suite_channel_plus.so";
  void* lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!check(lib != nullptr, dlerror())) {
    return 1;
  }
  auto* descFn = reinterpret_cast<const LV2_Descriptor* (*)(uint32_t)>(
      dlsym(lib, "lv2_descriptor"));
  if (!check(descFn != nullptr, "lv2_descriptor")) {
    return 1;
  }
  const LV2_Descriptor* desc = descFn(0);
  if (!check(desc != nullptr, "descriptor")) {
    return 1;
  }
  std::printf("loaded LV2: %s\n", desc->URI);

  UridMap urid;
  LV2_Feature mapFeature{LV2_URID__map, &urid.map};
  const LV2_Feature* features[] = {&mapFeature, nullptr};

  LV2_Handle handle = desc->instantiate(desc, 48000.0, path, features);
  if (!check(handle != nullptr, "instantiate")) {
    return 1;
  }

  std::vector<float> inL(512), inR(512), outL(512), outR(512);
  float gr = 0.0f, outPl = 0.0f, outPr = 0.0f, inPl = 0.0f, inPr = 0.0f;
  float latency = 0.0f;
  float params[suite::P_COUNT];
  for (uint32_t i = 0; i < suite::P_COUNT; ++i) {
    params[i] = static_cast<float>(suite::kParamSpecs[i].def);
  }
  for (int i = 0; i < 512; ++i) {
    inL[i] = 0.1f * std::sin(2.0f * 3.14159265f * 1000.0f * i / 48000.0f);
    inR[i] = inL[i];
  }

  desc->connect_port(handle, IN_L, inL.data());
  desc->connect_port(handle, IN_R, inR.data());
  desc->connect_port(handle, OUT_L, outL.data());
  desc->connect_port(handle, OUT_R, outR.data());
  for (uint32_t i = 0; i < suite::P_COUNT; ++i) {
    desc->connect_port(handle, FIRST_PARAM + i, &params[i]);
  }
  desc->connect_port(handle, GR, &gr);
  desc->connect_port(handle, OUT_PEAK_L, &outPl);
  desc->connect_port(handle, OUT_PEAK_R, &outPr);
  desc->connect_port(handle, IN_PEAK_L, &inPl);
  desc->connect_port(handle, IN_PEAK_R, &inPr);
  desc->connect_port(handle, LATENCY, &latency);

  desc->activate(handle);
  desc->run(handle, 512);
  for (int i = 0; i < 512; ++i) {
    if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) {
      check(false, "finite output");
      break;
    }
  }
  std::printf("latency: %.0f, gr: %.1f dB, out peak: %.1f dB\n", latency, gr,
              outPl);

  // ---------------------------------------------------------- LV2 state
  const auto* state = static_cast<const LV2_State_Interface*>(
      desc->extension_data(LV2_STATE__interface));
  check(state != nullptr, "advertises the LV2 state interface");

  if (state) {
    params[suite::P_INPUT] = 0.9f;
    desc->run(handle, 512);

    Captured first;
    check(state->save(handle, captureStore, &first, 0, features) ==
              LV2_STATE_SUCCESS &&
              first.called,
          "state saves the parameters");

    LV2_Handle restored = desc->instantiate(desc, 48000.0, path, features);
    check(state->restore(restored, captureRetrieve, &first, 0, features) ==
              LV2_STATE_SUCCESS,
          "state restore accepts its own blob");

    Captured second;
    check(state->save(restored, captureStore, &second, 0, features) ==
              LV2_STATE_SUCCESS,
          "state saves after restore");
    check(second.data == first.data, "save-restore-save is stable");
    check(std::fabs(blobValue(second.data, suite::P_INPUT) - 0.9) < 1e-9,
          "restored value is the saved one");

    // The host's control port still carries the previous session value.  The
    // first poll after a restore must take it as a baseline, not apply it.
    LV2_Handle held = desc->instantiate(desc, 48000.0, path, features);
    check(state->restore(held, captureRetrieve, &first, 0, features) ==
              LV2_STATE_SUCCESS,
          "second restore succeeds");
    float stale[suite::P_COUNT];
    for (uint32_t i = 0; i < suite::P_COUNT; ++i) {
      stale[i] = static_cast<float>(suite::kParamSpecs[i].def);
    }
    stale[suite::P_INPUT] = 0.1f;
    desc->connect_port(held, IN_L, inL.data());
    desc->connect_port(held, IN_R, inR.data());
    desc->connect_port(held, OUT_L, outL.data());
    desc->connect_port(held, OUT_R, outR.data());
    for (uint32_t i = 0; i < suite::P_COUNT; ++i) {
      desc->connect_port(held, FIRST_PARAM + i, &stale[i]);
    }
    desc->activate(held);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Captured third;
    check(state->save(held, captureStore, &third, 0, features) ==
              LV2_STATE_SUCCESS,
          "state saves after the first poll");
    check(std::fabs(blobValue(third.data, suite::P_INPUT) - 0.9) < 1e-9,
          "stale host port does not undo restored values");
    desc->deactivate(held);
    desc->cleanup(held);
    desc->cleanup(restored);
  }

  desc->deactivate(handle);
  desc->cleanup(handle);
  dlclose(lib);
  std::printf("suite_lv2_host: ok\n");
  return 0;
}
