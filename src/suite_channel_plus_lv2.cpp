// Suite Channel — LV2 plugin
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The control ports mirror the CLAP parameter ids: port = 4 + ParamId.

#include <lv2/core/lv2.h>

#include <cstring>

#include "dsp/engine.hpp"
#include "dsp/inline_display.hpp"
#include "params.hpp"

namespace {

constexpr const char* kPluginUri =
    "https://github.com/tylorismusic-commits/suite-channel";

enum Port : uint32_t {
  PORT_AUDIO_IN_L = 0,
  PORT_AUDIO_IN_R = 1,
  PORT_AUDIO_OUT_L = 2,
  PORT_AUDIO_OUT_R = 3,
  PORT_FIRST_PARAM = 4,
  PORT_GR_METER = PORT_FIRST_PARAM + suite::P_COUNT,
  PORT_GATE_GR_METER,
  PORT_OUT_PEAK_L,
  PORT_OUT_PEAK_R,
  PORT_IN_PEAK_L,
  PORT_IN_PEAK_R,
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
  float* gateGr = nullptr;
  float* outPeakL = nullptr;
  float* outPeakR = nullptr;
  float* inPeakL = nullptr;
  float* inPeakR = nullptr;
  float* latency = nullptr;

  suite::InlineDisplay display;
  const suite::InlineDisplayQueueDraw* queueDraw = nullptr;
  uint32_t drawCounter = 0;
};

LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*,
                       const LV2_Feature* const* features) {
  auto* p = new Plugin();
  p->sampleRate = static_cast<float>(rate);
  p->engine.prepare(p->sampleRate, static_cast<float>(p->shared.get(suite::P_OVERSAMPLE)));
  p->engine.setControls(suite::controlsFrom(p->shared));

  for (const LV2_Feature* const* f = features; f && *f; ++f) {
    if (!std::strcmp((*f)->URI, suite::kInlineDisplayQueueDrawURI) &&
        (*f)->data) {
      p->queueDraw = static_cast<const suite::InlineDisplayQueueDraw*>(
          static_cast<const void*>((*f)->data));
    }
  }
  return p;
}

void connectPort(LV2_Handle handle, uint32_t port, void* data) {
  auto* p = static_cast<Plugin*>(handle);
  if (port >= PORT_COUNT) {
    return;
  }
  float* fp = static_cast<float*>(data);
  p->ports[port] = fp;
  switch (port) {
    case PORT_AUDIO_IN_L: p->audioInL = fp; break;
    case PORT_AUDIO_IN_R: p->audioInR = fp; break;
    case PORT_AUDIO_OUT_L: p->audioOutL = fp; break;
    case PORT_AUDIO_OUT_R: p->audioOutR = fp; break;
    case PORT_GR_METER: p->gr = fp; break;
    case PORT_GATE_GR_METER: p->gateGr = fp; break;
    case PORT_OUT_PEAK_L: p->outPeakL = fp; break;
    case PORT_OUT_PEAK_R: p->outPeakR = fp; break;
    case PORT_IN_PEAK_L: p->inPeakL = fp; break;
    case PORT_IN_PEAK_R: p->inPeakR = fp; break;
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
  if (!p->audioInL || !p->audioOutL) {
    return;
  }
  if (!p->audioInR) {
    p->audioInR = p->audioInL;
  }
  if (!p->audioOutR) {
    p->audioOutR = p->audioOutL;
  }

  for (uint32_t i = 0; i < suite::P_COUNT; ++i) {
    float* v = p->ports[PORT_FIRST_PARAM + i];
    if (!v) {
      continue;
    }
    if (!p->portsSeen || *v != p->lastPort[i]) {
      p->shared.setFromHost(i, static_cast<double>(*v));
      p->lastPort[i] = *v;
    }
  }
  p->portsSeen = true;
  p->engine.setControls(suite::controlsFrom(p->shared));

  if (p->audioOutL != p->audioInL) {
    std::memcpy(p->audioOutL, p->audioInL, frames * sizeof(float));
  }
  if (p->audioOutR != p->audioInR) {
    std::memcpy(p->audioOutR, p->audioInR, frames * sizeof(float));
  }

  p->engine.process(p->audioOutL, p->audioOutR, static_cast<int>(frames),
                    p->meters);

  if (p->gr) {
    *p->gr = p->meters.grDb;
  }
  if (p->gateGr) {
    *p->gateGr = p->meters.gateGrDb;
  }
  if (p->outPeakL) {
    *p->outPeakL = p->meters.outPeakLDb;
  }
  if (p->outPeakR) {
    *p->outPeakR = p->meters.outPeakRDb;
  }
  if (p->inPeakL) {
    *p->inPeakL = p->meters.inPeakLDb;
  }
  if (p->inPeakR) {
    *p->inPeakR = p->meters.inPeakRDb;
  }
  if (p->latency) {
    *p->latency = static_cast<float>(p->engine.latencySamples());
  }

  p->display.setGainReductionDb(p->meters.grDb);
  if (p->queueDraw && p->queueDraw->queue_draw &&
      ((p->drawCounter++ & 31u) == 0u)) {
    p->queueDraw->queue_draw(p->queueDraw->handle);
  }
}

void cleanup(LV2_Handle handle) { delete static_cast<Plugin*>(handle); }

suite::InlineDisplaySurface* renderInlineDisplay(void* instance, uint32_t width,
                                                 uint32_t height) {
  auto* p = static_cast<Plugin*>(instance);
  return p ? p->display.render(width, height) : nullptr;
}

const suite::InlineDisplayInterface kInlineDisplay = {&renderInlineDisplay};

const void* extensionData(const char* uri) {
  if (uri && !std::strcmp(uri, suite::kInlineDisplayInterfaceURI)) {
    return &kInlineDisplay;
  }
  return nullptr;
}

const LV2_Descriptor kDescriptor = {
    kPluginUri, instantiate, connectPort, activate, run, deactivate,
    cleanup, extensionData};

} // namespace

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
  return index == 0 ? &kDescriptor : nullptr;
}
