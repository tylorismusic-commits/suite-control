// Suite Channel — minimal LV2 host smoke test
// SPDX-License-Identifier: GPL-3.0-or-later
#include <lv2/core/lv2.h>

#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <cstring>
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

  LV2_Handle handle = desc->instantiate(desc, 48000.0, path, nullptr);
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
  desc->deactivate(handle);
  desc->cleanup(handle);
  dlclose(lib);
  std::printf("suite_lv2_host: ok\n");
  return 0;
}
