// Suite Control — minimal LV2 host smoke test
// SPDX-License-Identifier: GPL-3.0-or-later
#include <lv2/core/lv2.h>
#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

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

  LV2_Handle handle = desc->instantiate(desc, 48000.0, path, nullptr);
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

  desc->deactivate(handle);
  desc->cleanup(handle);
  dlclose(lib);
  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
