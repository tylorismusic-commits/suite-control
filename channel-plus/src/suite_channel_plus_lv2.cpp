// Suite Channel — LV2 plugin
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The control ports mirror the CLAP parameter ids: port = 4 + ParamId.

#include <lv2/core/lv2.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

#include "dsp/engine.hpp"
#include "dsp/inline_display.hpp"
#include "params.hpp"
#include "peer.hpp"

namespace {

constexpr const char* kPluginUri =
    "https://github.com/tylorismusic-commits/suite-channel-plus";

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
  // Tells our own UI which peer slot we registered, so it can mirror remote
  // edits (Suite Control) that the host never hears about.
  PORT_PEER_ID,
  PORT_COUNT
};

struct Plugin {
  float sampleRate = 48000.0f;
  float lastPort[suite::P_COUNT] = {};

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
  float* peerId = nullptr;

  suite::peer::Client peer;
  bool peerRegistered = false;
  bool portsSynced = false;
  int slotIndex = -1;
  // LV2 hosts only hand us new port values inside run(), which they skip while
  // the transport is stopped.  Poll the ports so BUS changes reach Suite
  // Control without playback.
  std::thread peerThread;
  std::atomic<bool> peerRun{false};

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
    case PORT_PEER_ID: p->peerId = fp; break;
    default: break;
  }
}

void activate(LV2_Handle handle) {
  auto* p = static_cast<Plugin*>(handle);
  p->engine.reset();
  const int slot = p->peer.registerSlot(
      static_cast<uint32_t>(p->shared.get(suite::P_BUS) + 0.5),
      "Suite Channel Plus", suite::P_COUNT);
  if (slot >= 0) {
    p->peerRegistered = true;
    p->slotIndex = slot;
  }
  if (p->latency) {
    *p->latency = static_cast<float>(p->engine.latencySamples());
  }
  if (p->peerId) {
    *p->peerId = static_cast<float>(p->slotIndex + 1);
  }
  if (!p->peerRun.exchange(true)) {
    p->peerThread = std::thread([p] {
      while (p->peerRun.load(std::memory_order_relaxed)) {
        if (p->peerRegistered) {
          for (uint32_t i = 0; i < suite::P_COUNT; ++i) {
            const float* v = p->ports[PORT_FIRST_PARAM + i];
            // The first pass adopts every port, including values that happen
            // to equal the zero-initialised cache (bus Off is 0).
            if (v && (!p->portsSynced || *v != p->lastPort[i])) {
              p->shared.setFromHost(i, static_cast<double>(*v));
              p->lastPort[i] = *v;
            }
          }
          p->portsSynced = true;
          for (uint32_t i = 0; i < suite::P_COUNT; ++i) {
            double v = 0.0;
            if (p->peer.takeRemote(static_cast<int>(i), &v) &&
                std::fabs(v - p->shared.get(i)) > 1e-12) {
              p->shared.set(i, v);
              // Leave lastPort alone.  The host's control port still holds the
              // pre-edit value; if we recorded the remote value as "seen", the
              // next poll would treat the untouched port as a host change and
              // undo the edit - the knob would snap back and the strip would
              // never move.
            }
          }
          for (uint32_t i = 0; i < suite::P_COUNT; ++i) {
            p->peer.publish(static_cast<int>(i), p->shared.get(i));
          }
          p->peer.setBus(
              static_cast<uint32_t>(p->shared.get(suite::P_BUS) + 0.5));
          p->peer.beat();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
      }
    });
  }
}

void deactivate(LV2_Handle handle) {
  auto* p = static_cast<Plugin*>(handle);
  if (p->peerRun.exchange(false) && p->peerThread.joinable()) {
    p->peerThread.join();
  }
  if (p->peerRegistered) {
    p->peer.unregisterSlot();
    p->peerRegistered = false;
    p->slotIndex = -1;
  }
}

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

  // Port -> parameter sync belongs to the peer thread above: the audio thread
  // must not race it, and REAPER skips run() entirely while stopped.
  p->peer.publishMeters(p->meters.inPeakLDb, p->meters.inPeakRDb,
                        p->meters.outPeakLDb, p->meters.outPeakRDb,
                        p->meters.grDb);
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
  if (p->peerId) {
    *p->peerId = static_cast<float>(p->slotIndex + 1);
  }

  p->display.setGainReductionDb(p->meters.grDb);
  if (p->queueDraw && p->queueDraw->queue_draw &&
      ((p->drawCounter++ & 31u) == 0u)) {
    p->queueDraw->queue_draw(p->queueDraw->handle);
  }
}

void cleanup(LV2_Handle handle) {
  auto* p = static_cast<Plugin*>(handle);
  if (p->peerRun.exchange(false) && p->peerThread.joinable()) {
    p->peerThread.join();
  }
  delete p;
}

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
