// Suite Control — parameters
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "dsp/controls.hpp"

namespace suite {

enum ParamId : uint32_t {
  P_COMP_IN = 0,
  P_COMP_THRESHOLD,
  P_COMP_RATIO,
  P_COMP_ATTACK,
  P_COMP_RELEASE,
  P_COMP_AUTO,
  P_COMP_MIX,
  P_EQ_IN,
  P_EQ_TYPE,
  P_EQ_LF_GAIN,
  P_EQ_LF_FREQ,
  P_EQ_LF_BELL,
  P_EQ_LM_GAIN,
  P_EQ_LM_FREQ,
  P_EQ_LM_Q,
  P_EQ_HM_GAIN,
  P_EQ_HM_FREQ,
  P_EQ_HM_Q,
  P_EQ_HF_GAIN,
  P_EQ_HF_FREQ,
  P_EQ_HF_BELL,
  P_SUM_AMOUNT,
  P_INPUT,
  P_OUTPUT,
  P_LIMITER,
  P_BUS,
  P_COUNT
};

struct Labels {
  const char* const* names = nullptr;
  int count = 0;
};

template <size_t N>
constexpr Labels labels(const char* const (&arr)[N]) noexcept {
  return Labels{arr, static_cast<int>(N)};
}

inline constexpr const char* kBusLabels[] = {"All", "A", "B", "C",
                                             "D",   "E", "F", "G", "H"};
inline constexpr const char* kEqTypeLabels[] = {"Brown", "Black"};
inline constexpr const char* kRatioLabels[] = {"2", "4", "10"};
inline constexpr const char* kAutoLabels[] = {"MAN", "AUTO"};

struct ParamSpec {
  const char* symbol;
  const char* name;
  const char* module;
  double min;
  double max;
  double def;
  bool stepped;
  bool hidden;
  bool automatable;
  const char* unit;
  Labels valueLabels;
};

inline constexpr ParamSpec kParamSpecs[P_COUNT] = {
    {"comp_in", "Comp In", "Compressor", 0.0, 1.0, 1.0, true, false, true,
     nullptr, {}},
    {"comp_threshold", "Threshold", "Compressor", 0.0, 1.0, 0.72, false, false,
     true, "dB", {}},
    {"comp_ratio", "Ratio", "Compressor", 0.0, 2.0, 0.0, true, false, true,
     nullptr, labels(kRatioLabels)},
    {"comp_attack", "Attack", "Compressor", 0.0, 1.0, 0.35, false, false, true,
     "ms", {}},
    {"comp_release", "Release", "Compressor", 0.0, 1.0, 0.45, false, false,
     true, "ms", {}},
    {"comp_auto", "Auto Release", "Compressor", 0.0, 1.0, 1.0, true, false,
     true, nullptr, labels(kAutoLabels)},
    {"comp_mix", "Parallel", "Compressor", 0.0, 1.0, 1.0, false, false, true,
     "%", {}},
    {"eq_in", "EQ In", "EQ", 0.0, 1.0, 1.0, true, false, true, nullptr, {}},
    {"eq_type", "EQ Type", "EQ", 0.0, 1.0, 0.0, true, false, true, nullptr,
     labels(kEqTypeLabels)},
    {"eq_lf_gain", "LF Gain", "EQ", 0.0, 1.0, 0.5, false, false, true, "dB", {}},
    {"eq_lf_freq", "LF Freq", "EQ", 0.0, 1.0, 0.55, false, false, true, "Hz",
     {}},
    {"eq_lf_bell", "LF Bell", "EQ", 0.0, 1.0, 0.0, true, false, true, nullptr,
     {}},
    {"eq_lm_gain", "LMF Gain", "EQ", 0.0, 1.0, 0.5, false, false, true, "dB",
     {}},
    {"eq_lm_freq", "LMF Freq", "EQ", 0.0, 1.0, 0.45, false, false, true, "Hz",
     {}},
    {"eq_lm_q", "LMF Q", "EQ", 0.0, 1.0, 0.4, false, false, true, nullptr, {}},
    {"eq_hm_gain", "HMF Gain", "EQ", 0.0, 1.0, 0.5, false, false, true, "dB",
     {}},
    {"eq_hm_freq", "HMF Freq", "EQ", 0.0, 1.0, 0.55, false, false, true, "Hz",
     {}},
    {"eq_hm_q", "HMF Q", "EQ", 0.0, 1.0, 0.4, false, false, true, nullptr, {}},
    {"eq_hf_gain", "HF Gain", "EQ", 0.0, 1.0, 0.5, false, false, true, "dB", {}},
    {"eq_hf_freq", "HF Freq", "EQ", 0.0, 1.0, 0.6, false, false, true, "Hz", {}},
    {"eq_hf_bell", "HF Bell", "EQ", 0.0, 1.0, 0.0, true, false, true, nullptr,
     {}},
    {"sum_amount", "Summing", "Summing", 0.0, 1.0, 0.35, false, false, true,
     "%", {}},
    {"input", "Input", "Output", 0.0, 1.0, 0.5, false, false, true, "dB", {}},
    {"output", "Output", "Output", 0.0, 1.0, 0.5, false, false, true, "dB", {}},
    {"limiter", "Limiter", "Output", 0.0, 1.0, 0.0, true, false, true, nullptr,
     {}},
    {"bus", "Control Bus", "Global", 0.0, 8.0, 1.0, true, false, false, nullptr,
     labels(kBusLabels)},
};

static_assert(sizeof(kParamSpecs) / sizeof(kParamSpecs[0]) == P_COUNT,
              "kParamSpecs must cover every parameter id");

class Shared {
public:
  Shared() {
    for (uint32_t p = 0; p < P_COUNT; ++p) {
      values_[p].store(kParamSpecs[p].def, std::memory_order_relaxed);
      dirty_[p].store(false, std::memory_order_relaxed);
    }
  }

  double get(uint32_t p) const noexcept {
    return values_[p].load(std::memory_order_relaxed);
  }

  void set(uint32_t p, double v) noexcept {
    if (!std::isfinite(v)) return;
    const ParamSpec& s = kParamSpecs[p];
    v = std::min(std::max(v, s.min), s.max);
    if (s.stepped) v = std::floor(v + 0.5);
    const double previous = values_[p].load(std::memory_order_relaxed);
    if (v == previous) {
      // Hosts echo our own writes back as port events; marking the value dirty
      // again would write it straight back and loop forever.
      return;
    }
    values_[p].store(v, std::memory_order_relaxed);
    dirty_[p].store(true, std::memory_order_release);
    revision_.fetch_add(1, std::memory_order_relaxed);
  }

  void setFromHost(uint32_t p, double v) noexcept { set(p, v); }

  bool takeDirty(uint32_t p) noexcept {
    return dirty_[p].exchange(false, std::memory_order_acquire);
  }
  uint64_t revision() const noexcept {
    return revision_.load(std::memory_order_relaxed);
  }

  // Console name table, keyed by bus + position (BUS A CH1 = A/1).  UI and
  // state only; the audio thread never touches it.
  static constexpr int kNameSlots = 9 * 32;
  static constexpr int kNameBytes = 24;
  char names[kNameSlots][kNameBytes] = {};
  bool muted[kNameSlots] = {};
  bool soloed[kNameSlots] = {};
  bool gainForced[kNameSlots] = {};   // we pulled this channel's fader down
  float savedGain[kNameSlots] = {};   // fader value to restore

  std::atomic<float> inPeakDb{-120.0f};
  std::atomic<float> outPeakDb{-120.0f};
  std::atomic<float> inPeakLDb{-120.0f};
  std::atomic<float> inPeakRDb{-120.0f};
  std::atomic<float> outPeakLDb{-120.0f};
  std::atomic<float> outPeakRDb{-120.0f};
  std::atomic<float> grDb{0.0f};

private:
  std::atomic<double> values_[P_COUNT];
  std::atomic<bool> dirty_[P_COUNT];
  std::atomic<uint64_t> revision_{0};
};

inline Controls controlsFrom(const Shared& s) noexcept {
  Controls c;
  c.compIn = s.get(P_COMP_IN) > 0.5;
  c.compThreshold = static_cast<float>(s.get(P_COMP_THRESHOLD));
  c.compRatio = static_cast<int>(s.get(P_COMP_RATIO) + 0.5);
  c.compAttack = static_cast<float>(s.get(P_COMP_ATTACK));
  c.compRelease = static_cast<float>(s.get(P_COMP_RELEASE));
  c.compAuto = s.get(P_COMP_AUTO) > 0.5;
  c.compMix = static_cast<float>(s.get(P_COMP_MIX));
  c.eqIn = s.get(P_EQ_IN) > 0.5;
  c.eqType = static_cast<int>(s.get(P_EQ_TYPE) + 0.5);
  c.lfGain = static_cast<float>(s.get(P_EQ_LF_GAIN));
  c.lfFreq = static_cast<float>(s.get(P_EQ_LF_FREQ));
  c.lfBell = s.get(P_EQ_LF_BELL) > 0.5;
  c.lmGain = static_cast<float>(s.get(P_EQ_LM_GAIN));
  c.lmFreq = static_cast<float>(s.get(P_EQ_LM_FREQ));
  c.lmQ = static_cast<float>(s.get(P_EQ_LM_Q));
  c.hmGain = static_cast<float>(s.get(P_EQ_HM_GAIN));
  c.hmFreq = static_cast<float>(s.get(P_EQ_HM_FREQ));
  c.hmQ = static_cast<float>(s.get(P_EQ_HM_Q));
  c.hfGain = static_cast<float>(s.get(P_EQ_HF_GAIN));
  c.hfFreq = static_cast<float>(s.get(P_EQ_HF_FREQ));
  c.hfBell = s.get(P_EQ_HF_BELL) > 0.5;
  c.sumAmount = static_cast<float>(s.get(P_SUM_AMOUNT));
  c.input = static_cast<float>(s.get(P_INPUT));
  c.output = static_cast<float>(s.get(P_OUTPUT));
  c.limiter = s.get(P_LIMITER) > 0.5;
  c.bus = static_cast<int>(s.get(P_BUS) + 0.5);
  return c;
}

} // namespace suite
