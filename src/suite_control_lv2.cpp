// Suite Control — LV2 plugin
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Control ports mirror the CLAP parameter ids: port = 4 + ParamId.  Meter ports
// follow the parameter block.

#include <lv2/atom/atom.h>
#include <lv2/core/lv2.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "console_state.hpp"
#include "dsp/engine.hpp"
#include "params.hpp"

namespace {

constexpr const char* kPluginUri =
    "https://github.com/tylorismusic-commits/suite-control";
// LV2 state keys are URIDs: hosts map them back to property URIs when they
// write the session.  A raw number is silently dropped.
constexpr const char* kStateUri =
    "https://github.com/tylorismusic-commits/suite-control#console";

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

  // The console table is typed in the UI, so it lives in a small per-process
  // segment that the UI keeps current; state save reads it from there.
  suite::ConsoleBridge console;

  const LV2_URID_Map* map = nullptr;
  uint32_t chunkType = 0;
  uint32_t stateKey = 0;
};

LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*,
                       const LV2_Feature* const* features) {
  auto* p = new Plugin();
  for (const LV2_Feature* const* f = features; f && *f; ++f) {
    if (!std::strcmp((*f)->URI, LV2_URID__map) && (*f)->data) {
      p->map = static_cast<const LV2_URID_Map*>(f[0]->data);
      p->chunkType = p->map->map(p->map->handle, LV2_ATOM__Chunk);
      p->stateKey = p->map->map(p->map->handle, kStateUri);
    }
  }
  p->sampleRate = static_cast<float>(rate);
  p->console.open();
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

// --------------------------------------------------------------- LV2 state

void copyToTable(const suite::Shared& shared, suite::ConsoleTable& out) {
  std::memcpy(out.names, shared.names, sizeof(out.names));
  for (int i = 0; i < suite::ConsoleTable::kSlots; ++i) {
    out.muted[i] = shared.muted[i] ? 1 : 0;
    out.gainForced[i] = shared.gainForced[i] ? 1 : 0;
    out.savedGain[i] = shared.savedGain[i];
  }
}

void copyFromTable(const suite::ConsoleTable& table, suite::Shared& shared) {
  std::memcpy(shared.names, table.names, sizeof(table.names));
  for (int i = 0; i < suite::ConsoleTable::kSlots; ++i) {
    shared.muted[i] = table.muted[i] != 0;
    shared.gainForced[i] = table.gainForced[i] != 0;
    shared.savedGain[i] = table.savedGain[i];
  }
}

LV2_State_Status stateSave(LV2_Handle handle, LV2_State_Store_Function store,
                           LV2_State_Handle storeHandle, uint32_t,
                           const LV2_Feature* const*) {
  auto* p = static_cast<Plugin*>(handle);
  suite::ConsoleTable table{};
  if (!p->console.read(table)) copyToTable(p->shared, table);

  constexpr uint32_t kMagic = 0x53434e4d;  // SCNM
  constexpr uint32_t kVersion = 2;
  const uint32_t count = suite::ConsoleTable::kSlots;
  std::vector<uint8_t> blob;
  blob.reserve(12 + count * (suite::ConsoleTable::kBytes + 6));
  auto put = [&blob](const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    blob.insert(blob.end(), bytes, bytes + size);
  };
  put(&kMagic, sizeof(kMagic));
  put(&kVersion, sizeof(kVersion));
  put(&count, sizeof(count));
  for (uint32_t i = 0; i < count; ++i) {
    put(table.names[i], suite::ConsoleTable::kBytes);
    put(&table.muted[i], 1);
    put(&table.gainForced[i], 1);
    put(&table.savedGain[i], sizeof(float));
  }

  if (!p->chunkType || !p->stateKey) return LV2_STATE_ERR_UNKNOWN;
  return store(storeHandle, p->stateKey, blob.data(), blob.size(),
               p->chunkType, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
}

LV2_State_Status stateRestore(LV2_Handle handle,
                              LV2_State_Retrieve_Function retrieve,
                              LV2_State_Handle stateHandle, uint32_t,
                              const LV2_Feature* const*) {
  auto* p = static_cast<Plugin*>(handle);
  size_t size = 0;
  uint32_t type = 0;
  uint32_t flags = 0;
  if (!p->stateKey) return LV2_STATE_ERR_UNKNOWN;
  const void* data = retrieve(stateHandle, p->stateKey, &size, &type, &flags);
  if (!data) return LV2_STATE_ERR_NO_PROPERTY;
  if (!p->chunkType || type != p->chunkType) {
    return LV2_STATE_ERR_BAD_TYPE;
  }

  constexpr uint32_t kMagic = 0x53434e4d;
  constexpr uint32_t kVersion = 2;
  const uint32_t count = suite::ConsoleTable::kSlots;
  const size_t entry = suite::ConsoleTable::kBytes + 1 + 1 + sizeof(float);
  if (size < 12 || size != 12 + count * entry) return LV2_STATE_ERR_BAD_TYPE;

  const auto* bytes = static_cast<const uint8_t*>(data);
  uint32_t magic = 0, version = 0, stored = 0;
  std::memcpy(&magic, bytes, 4);
  std::memcpy(&version, bytes + 4, 4);
  std::memcpy(&stored, bytes + 8, 4);
  if (magic != kMagic || version != kVersion || stored != count) {
    return LV2_STATE_ERR_UNKNOWN;
  }

  suite::ConsoleTable table{};
  const uint8_t* cursor = bytes + 12;
  for (uint32_t i = 0; i < count; ++i) {
    std::memcpy(table.names[i], cursor, suite::ConsoleTable::kBytes);
    cursor += suite::ConsoleTable::kBytes;
    table.muted[i] = *cursor++;
    table.gainForced[i] = *cursor++;
    std::memcpy(&table.savedGain[i], cursor, sizeof(float));
    cursor += sizeof(float);
  }
  copyFromTable(table, p->shared);
  p->console.write(table);
  return LV2_STATE_SUCCESS;
}

const LV2_State_Interface kStateInterface = {stateSave, stateRestore};

const void* extensionData(const char* uri) {
  if (uri && !std::strcmp(uri, LV2_STATE__interface)) return &kStateInterface;
  return nullptr;
}

const LV2_Descriptor kDescriptor = {
    kPluginUri, instantiate, connectPort, activate, run, deactivate, cleanup,
    extensionData};

} // namespace

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
  return index == 0 ? &kDescriptor : nullptr;
}
