// Suite Control — LV2 plugin
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Control ports mirror the CLAP parameter ids: port = 4 + ParamId.  Meter ports
// follow the parameter block.

#include <lv2/core/lv2.h>

#include <cstring>

#include "dsp/engine.hpp"
#include "params.hpp"

namespace {

constexpr const char* kPluginUri =
    "https://github.com/tylorismusic-commits/suite-control";

enum Port : uint32_t {
  PORT_AUDIO_IN_L = 0,
  PORT_AUDIO_IN_R = 1,
  PORT_AUDIO_OUT_L = 2,
  PORT_AUDIO_OUT_R = 3,
  PORT_FIRST_PARAM = 4,
  PORT_GR = PORT_FIRST_PARAM + suite::P_COUNT,
  PORT_IN_L,
  PORT_IN_R,
  PORT_OUT_L,
  PORT_OUT_R,
  PORT_LATENCY,
  PORT_COUNT
};

struct Plugin {
  float sampleRate = 48000.0f;
  float lastPort[suite::P_COUNT] = {};
  bool portsSeen = false;
  suite::Shared shared;
  suite::Engine engine;
  suite::Meters meters;

  float* ports[PORT_COUNT] = {};
  float* audioInL = nullptr;
  float* audioInR = nullptr;
  float* audioOutL = nullptr;
  float* audioOutR = nullptr;
  float* gr = nullptr;
  float* inL = nullptr;
  float* inR = nullptr;
  float* outL = nullptr;
  float* outR = nullptr;
  float* latency = nullptr;
};

LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*,
                       const LV2_Feature* const*) {
  auto* p = new Plugin();
  p->sampleRate = static_cast<float>(rate);
  p->engine.prepare(p->sampleRate);
  p->engine.setControls(suite::controlsFrom(p->shared));
  return p;
}

void connectPort(LV2_Handle handle, uint32_t port, void* data) {
  auto* p = static_cast<Plugin*>(handle);
  if (port >= PORT_COUNT) return;
  float* fp = static_cast<float*>(data);
  p->ports[port] = fp;
  switch (port) {
    case PORT_AUDIO_IN_L: p->audioInL = fp; break;
    case PORT_AUDIO_IN_R: p->audioInR = fp; break;
    case PORT_AUDIO_OUT_L: p->audioOutL = fp; break;
    case PORT_AUDIO_OUT_R: p->audioOutR = fp; break;
    case PORT_GR: p->gr = fp; break;
    case PORT_IN_L: p->inL = fp; break;
    case PORT_IN_R: p->inR = fp; break;
    case PORT_OUT_L: p->outL = fp; break;
    case PORT_OUT_R: p->outR = fp; break;
    case PORT_LATENCY: p->latency = fp; break;
    default: break;
  }
}

void activate(LV2_Handle handle) {
  auto* p = static_cast<Plugin*>(handle);
  p->engine.reset();
  if (p->latency) {
    *p->latency = static_cast<float>(p->engine.latencySamples());
  }
}

void deactivate(LV2_Handle) {}

void run(LV2_Handle handle, uint32_t frames) {
  auto* p = static_cast<Plugin*>(handle);
  if (!p->audioInL || !p->audioOutL) return;

  for (uint32_t i = 0; i < suite::P_COUNT; ++i) {
    float* v = p->ports[PORT_FIRST_PARAM + i];
    if (!v) continue;
    if (!p->portsSeen || *v != p->lastPort[i]) {
      p->shared.setFromHost(i, static_cast<double>(*v));
      p->lastPort[i] = *v;
    }
  }
  p->portsSeen = true;
  p->engine.setControls(suite::controlsFrom(p->shared));

  float* outL = p->audioOutL;
  float* outR = p->audioOutR ? p->audioOutR : p->audioOutL;
  const float* inL = p->audioInL;
  const float* inR = p->audioInR ? p->audioInR : p->audioInL;
  if (outL != inL) std::memcpy(outL, inL, frames * sizeof(float));
  if (outR != inR) std::memcpy(outR, inR, frames * sizeof(float));

  p->engine.process(outL, outR, static_cast<int>(frames), p->meters);

  if (p->gr) *p->gr = p->meters.grDb;
  if (p->inL) *p->inL = p->meters.inPeakLDb;
  if (p->inR) *p->inR = p->meters.inPeakRDb;
  if (p->outL) *p->outL = p->meters.outPeakLDb;
  if (p->outR) *p->outR = p->meters.outPeakRDb;
  if (p->latency) *p->latency = static_cast<float>(p->engine.latencySamples());
}

void cleanup(LV2_Handle handle) { delete static_cast<Plugin*>(handle); }

const void* extensionData(const char*) { return nullptr; }

const LV2_Descriptor kDescriptor = {
    kPluginUri, instantiate, connectPort, activate, run, deactivate, cleanup,
    extensionData};

} // namespace

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
  return index == 0 ? &kDescriptor : nullptr;
}
