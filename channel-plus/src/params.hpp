// Suite Channel — params.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "dsp/controls.hpp"

namespace suite {

// Parameter ids are the CLAP parameter ids and the index into Shared::values.
// They are also the ordering of the LV2 control ports (port = 4 + id), so this
// enum must only ever be appended to.
enum ParamId : uint32_t {
  P_ENABLED = 0,
  P_INPUT,
  P_POLARITY,
  P_COLOR_MODEL,
  P_COLOR_AMOUNT,
  P_HPF,
  P_LPF,
  P_EQ_TYPE,
  P_EQ_IN,
  P_LF_GAIN,
  P_LF_FREQ,
  P_LF_BELL,
  P_LM_GAIN,
  P_LM_FREQ,
  P_LM_Q,
  P_HM_GAIN,
  P_HM_FREQ,
  P_HM_Q,
  P_HF_GAIN,
  P_HF_FREQ,
  P_HF_BELL,
  P_COMP_TYPE,
  P_COMP_IN,
  P_COMP_THRESHOLD,
  P_COMP_MAKEUP,
  P_COMP_FAST,
  P_COMP_AUTO,
  P_GATE_IN,
  P_GATE_THRESHOLD,
  P_GATE_RANGE,
  P_GATE_RELEASE,
  P_GATE_FAST,
  P_GATE_MODE,
  P_DYN_POST,
  P_OUTPUT,
  P_OVERSAMPLE,
  P_GATE_HOLD,
  P_LIMITER_ON,
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

// `symbol` is the LV2 port symbol and part of the saved-session format.
struct ParamSpec {
  const char* symbol;
  const char* name;
  const char* module;
  double min;
  double max;
  double def;
  bool stepped;
  bool hidden;
  bool toggled;
  const char* unit;
  Labels labels;
};

inline constexpr const char* kColourLabels[] = {"Valve", "Console", "Bus",
                                                "Tape", "Transformer"};
inline constexpr const char* kCompTypeLabels[] = {"VCA", "76Blue"};
inline constexpr const char* kGateModeLabels[] = {"Expander", "Gate", "Ducker"};
inline constexpr const char* kBusLabels[] = {"Off", "A", "B", "C", "D",
                                             "E",   "F", "G", "H"};
inline constexpr const char* kOversampleLabels[] = {"Off", "2x", "4x"};
inline constexpr const char* kEqTypeLabels[] = {"Brown", "Black"};

inline constexpr ParamSpec kParamSpecs[P_COUNT] = {
    {"power", "Power", "Global", 0.0, 1.0, 1.0, true, false, true, nullptr, {}},
    {"input", "Input", "Input", 0.0, 1.0, 0.5, false, false, false, "dB", {}},
    {"polarity", "Polarity", "Input", 0.0, 1.0, 0.0, true, false, true, nullptr, {}},
    {"color_model", "Colour", "Colour", 0.0, 4.0, 1.0, true, false, false,
     nullptr, labels(kColourLabels)},
    {"color_amount", "Colour Amount", "Colour", 0.0, 1.0, 0.35, false, false,
     false, nullptr, {}},
    {"hpf", "HPF", "Filters", 0.0, 1.0, 0.0, false, false, false, "Hz", {}},
    {"lpf", "LPF", "Filters", 0.0, 1.0, 0.0, false, false, false, "Hz", {}},
    {"eq_type", "EQ Type", "EQ", 0.0, 1.0, 0.0, true, false, false, nullptr,
     labels(kEqTypeLabels)},
    {"eq_in", "EQ In", "EQ", 0.0, 1.0, 1.0, true, false, true, nullptr, {}},
    {"lf_gain", "LF Gain", "EQ", 0.0, 1.0, 0.5, false, false, false, "dB", {}},
    {"lf_freq", "LF Frequency", "EQ", 0.0, 1.0, 0.70, false, false, false, "Hz",
     {}},
    {"lf_bell", "LF Bell", "EQ", 0.0, 1.0, 0.0, true, false, true, nullptr, {}},
    {"lm_gain", "LMF Gain", "EQ", 0.0, 1.0, 0.5, false, false, false, "dB", {}},
    {"lm_freq", "LMF Frequency", "EQ", 0.0, 1.0, 0.637, false, false, false,
     "Hz", {}},
    {"lm_q", "LMF Q", "EQ", 0.0, 1.0, 0.4, false, false, false, nullptr, {}},
    {"hm_gain", "HMF Gain", "EQ", 0.0, 1.0, 0.5, false, false, false, "dB", {}},
    {"hm_freq", "HMF Frequency", "EQ", 0.0, 1.0, 0.655, false, false, false,
     "Hz", {}},
    {"hm_q", "HMF Q", "EQ", 0.0, 1.0, 0.4, false, false, false, nullptr, {}},
    {"hf_gain", "HF Gain", "EQ", 0.0, 1.0, 0.5, false, false, false, "dB", {}},
    {"hf_freq", "HF Frequency", "EQ", 0.0, 1.0, 0.707, false, false, false,
     "Hz", {}},
    {"hf_bell", "HF Bell", "EQ", 0.0, 1.0, 0.0, true, false, true, nullptr, {}},
    {"comp_type", "Compressor Type", "Dynamics", 0.0, 1.0, 0.0, true, false,
     false, nullptr, labels(kCompTypeLabels)},
    {"comp_in", "Compressor In", "Dynamics", 0.0, 1.0, 0.0, true, false, true,
     nullptr, {}},
    {"comp_threshold", "Compressor Threshold", "Dynamics", 0.0, 1.0, 0.73,
     false, false, false, "dB", {}},
    {"comp_makeup", "Compressor Makeup", "Dynamics", 0.0, 1.0, 0.5, false,
     false, false, "dB", {}},
    {"comp_fast", "Compressor Fast Attack", "Dynamics", 0.0, 1.0, 0.0, true,
     false, true, nullptr, {}},
    {"comp_auto", "Compressor Auto Makeup", "Dynamics", 0.0, 1.0, 1.0, true,
     false, true, nullptr, {}},
    {"gate_in", "Gate In", "Dynamics", 0.0, 1.0, 0.0, true, false, true, nullptr,
     {}},
    {"gate_threshold", "Gate Threshold", "Dynamics", 0.0, 1.0, 0.25, false,
     false, false, "dB", {}},
    {"gate_range", "Gate Range", "Dynamics", 0.0, 1.0, 0.6, false, false, false,
     "dB", {}},
    {"gate_release", "Gate Release", "Dynamics", 0.0, 1.0, 0.5, false, false,
     false, "s", {}},
    {"gate_fast", "Gate Fast Attack", "Dynamics", 0.0, 1.0, 0.0, true, false,
     true, nullptr, {}},
    {"gate_mode", "Gate Mode", "Dynamics", 0.0, 2.0, 1.0, true, false, false,
     nullptr, labels(kGateModeLabels)},
    {"dyn_post", "Dynamics Post EQ", "Dynamics", 0.0, 1.0, 0.0, true, false,
     true, nullptr, {}},
    {"output", "Output", "Output", 0.0, 1.0, 0.75, false, false, false, "dB", {}},
    {"oversample", "Oversampling", "Global", 0.0, 2.0, 2.0, true, false, false,
     nullptr, labels(kOversampleLabels)},
    {"gate_hold", "Gate Hold", "Dynamics", 0.0, 1.0, 0.0, false, false, false,
     "ms", {}},
    {"limiter", "Output Limiter", "Output", 0.0, 1.0, 1.0, true, false, true,
     nullptr, {}},
    {"bus", "Control Bus", "Global", 0.0, 8.0, 1.0, true, false, false, nullptr,
     labels(kBusLabels)},
};

static_assert(sizeof(kParamSpecs) / sizeof(kParamSpecs[0]) == P_COUNT,
              "kParamSpecs must cover every parameter id");
static_assert(P_COUNT == 39, "parameter table and enum must stay in sync");

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
    if (!std::isfinite(v)) {
      return;
    }
    const ParamSpec& s = kParamSpecs[p];
    v = std::min(std::max(v, s.min), s.max);
    if (s.stepped) {
      v = static_cast<double>(static_cast<int>(v + (v >= 0 ? 0.5 : -0.5)));
    }
    if (v == values_[p].load(std::memory_order_relaxed)) {
      // Hosts echo our own writes back as port events; marking an unchanged
      // value dirty would write it straight back and loop forever.
      return;
    }
    values_[p].store(v, std::memory_order_relaxed);
    dirty_[p].store(true, std::memory_order_release);
  }

  void setFromHost(uint32_t p, double v) noexcept {
    if (!std::isfinite(v)) {
      return;
    }
    const ParamSpec& s = kParamSpecs[p];
    v = std::min(std::max(v, s.min), s.max);
    if (s.stepped) {
      v = static_cast<double>(static_cast<int>(v + (v >= 0 ? 0.5 : -0.5)));
    }
    values_[p].store(v, std::memory_order_relaxed);
  }

  bool takeDirty(uint32_t p) noexcept {
    return dirty_[p].exchange(false, std::memory_order_acquire);
  }

  bool isDirty(uint32_t p) const noexcept {
    return dirty_[p].load(std::memory_order_acquire);
  }

  bool anyDirty() const noexcept {
    for (uint32_t p = 0; p < P_COUNT; ++p) {
      if (dirty_[p].load(std::memory_order_acquire)) {
        return true;
      }
    }
    return false;
  }

  std::atomic<float> grDb{0.0f};
  std::atomic<float> gateGrDb{0.0f};
  std::atomic<float> inPeakDb{-120.0f};
  std::atomic<float> outPeakDb{-120.0f};
  std::atomic<float> outVuDb{-120.0f};
  std::atomic<float> inPeakLDb{-120.0f};
  std::atomic<float> inPeakRDb{-120.0f};
  std::atomic<float> outPeakLDb{-120.0f};
  std::atomic<float> outPeakRDb{-120.0f};

private:
  std::atomic<double> values_[P_COUNT];
  std::atomic<bool> dirty_[P_COUNT];
};

inline Controls controlsFrom(const Shared& s) noexcept {
  Controls c;
  c.enabled = s.get(P_ENABLED) > 0.5;
  c.input = static_cast<float>(s.get(P_INPUT));
  c.polarity = s.get(P_POLARITY) > 0.5;
  c.colour = static_cast<int>(s.get(P_COLOR_MODEL) + 0.5);
  c.colourAmount = static_cast<float>(s.get(P_COLOR_AMOUNT));
  c.hpf = static_cast<float>(s.get(P_HPF));
  c.lpf = static_cast<float>(s.get(P_LPF));
  c.eqType = static_cast<int>(s.get(P_EQ_TYPE) + 0.5);
  c.eqIn = s.get(P_EQ_IN) > 0.5;
  c.lfGain = static_cast<float>(s.get(P_LF_GAIN));
  c.lfFreq = static_cast<float>(s.get(P_LF_FREQ));
  c.lfBell = s.get(P_LF_BELL) > 0.5;
  c.lmGain = static_cast<float>(s.get(P_LM_GAIN));
  c.lmFreq = static_cast<float>(s.get(P_LM_FREQ));
  c.lmQ = static_cast<float>(s.get(P_LM_Q));
  c.hmGain = static_cast<float>(s.get(P_HM_GAIN));
  c.hmFreq = static_cast<float>(s.get(P_HM_FREQ));
  c.hmQ = static_cast<float>(s.get(P_HM_Q));
  c.hfGain = static_cast<float>(s.get(P_HF_GAIN));
  c.hfFreq = static_cast<float>(s.get(P_HF_FREQ));
  c.hfBell = s.get(P_HF_BELL) > 0.5;
  c.compType = static_cast<int>(s.get(P_COMP_TYPE) + 0.5);
  c.compIn = s.get(P_COMP_IN) > 0.5;
  c.compThreshold = static_cast<float>(s.get(P_COMP_THRESHOLD));
  c.compMakeup = static_cast<float>(s.get(P_COMP_MAKEUP));
  c.compFast = s.get(P_COMP_FAST) > 0.5;
  c.compAuto = s.get(P_COMP_AUTO) > 0.5;
  c.gateIn = s.get(P_GATE_IN) > 0.5;
  c.gateThreshold = static_cast<float>(s.get(P_GATE_THRESHOLD));
  c.gateRange = static_cast<float>(s.get(P_GATE_RANGE));
  c.gateRelease = static_cast<float>(s.get(P_GATE_RELEASE));
  c.gateHold = static_cast<float>(s.get(P_GATE_HOLD));
  c.gateFast = s.get(P_GATE_FAST) > 0.5;
  c.gateMode = static_cast<int>(s.get(P_GATE_MODE) + 0.5);
  c.dynPost = s.get(P_DYN_POST) > 0.5;
  c.output = static_cast<float>(s.get(P_OUTPUT));
  c.oversample = static_cast<float>(s.get(P_OVERSAMPLE));
  c.limiterOn = s.get(P_LIMITER_ON) > 0.5;
  c.bus = static_cast<int>(s.get(P_BUS) + 0.5);
  return c;
}

// Human-readable value used by the editor bubbles and the CLAP parameter
// strings. Every parameter goes through this one function so the panel and the
// host always report the same option names.
inline std::string lowerText(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

inline std::string trimText(const std::string& s) {
  const size_t a = s.find_first_not_of(" \t");
  if (a == std::string::npos) {
    return {};
  }
  const size_t b = s.find_last_not_of(" \t");
  return s.substr(a, b - a + 1);
}

inline std::string formatHz(double hz) {
  char buf[48];
  if (hz >= 1000.0) {
    std::snprintf(buf, sizeof(buf), "%.2f kHz", hz / 1000.0);
  } else {
    std::snprintf(buf, sizeof(buf), "%.0f Hz", hz);
  }
  return buf;
}

inline std::string formatDb(double db) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%+.1f dB", db);
  return buf;
}

inline std::string formatMs(double ms) {
  char buf[48];
  if (ms >= 1000.0) {
    std::snprintf(buf, sizeof(buf), "%.2f s", ms / 1000.0);
  } else {
    std::snprintf(buf, sizeof(buf), "%.0f ms", ms);
  }
  return buf;
}

inline std::string formatParamValue(uint32_t p, double v) {
  const ParamSpec& s = kParamSpecs[p];
  if (s.labels.count > 0) {
    const int i = std::clamp(static_cast<int>(v + 0.5), 0, s.labels.count - 1);
    return s.labels.names[i];
  }
  char buf[64];
  switch (p) {
    case P_ENABLED:
    case P_POLARITY:
    case P_EQ_IN:
    case P_LF_BELL:
    case P_HF_BELL:
    case P_COMP_IN:
    case P_COMP_FAST:
    case P_COMP_AUTO:
    case P_GATE_IN:
    case P_GATE_FAST:
    case P_DYN_POST:
    case P_LIMITER_ON:
      return v > 0.5 ? "On" : "Off";
    case P_INPUT:
      return formatDb(inputGainDb(static_cast<float>(v)));
    case P_OUTPUT:
      return v <= 0.0005
                 ? "OFF"
                 : formatDb(outputGainDb(static_cast<float>(v)));
    case P_COMP_THRESHOLD:
      return formatDb(compThresholdDb(static_cast<float>(v)));
    case P_COMP_MAKEUP:
      return formatDb(compMakeupDb(static_cast<float>(v)));
    case P_HPF:
      return v <= 0.001 ? "OUT" : formatHz(hpfHz(static_cast<float>(v)));
    case P_LPF:
      return v <= 0.001 ? "OUT" : formatHz(lpfHz(static_cast<float>(v)));
    case P_LF_GAIN:
    case P_LM_GAIN:
    case P_HM_GAIN:
    case P_HF_GAIN:
      return formatDb(bandGainDb(static_cast<float>(v)));
    case P_LF_FREQ:
      return formatHz(lfHz(static_cast<float>(v)));
    case P_LM_FREQ:
      return formatHz(lmHz(static_cast<float>(v)));
    case P_HM_FREQ:
      return formatHz(hmHz(static_cast<float>(v)));
    case P_HF_FREQ:
      return formatHz(hfHz(static_cast<float>(v)));
    case P_LM_Q:
    case P_HM_Q:
      std::snprintf(buf, sizeof(buf), "%.2f", bandQ(static_cast<float>(v)));
      return buf;
    case P_GATE_THRESHOLD:
      return formatDb(gateThresholdDb(static_cast<float>(v)));
    case P_GATE_RANGE:
      std::snprintf(buf, sizeof(buf), "%.0f dB",
                    gateRangeDb(static_cast<float>(v)));
      return buf;
    case P_GATE_RELEASE:
      return formatMs(gateReleaseMs(static_cast<float>(v)));
    case P_GATE_HOLD:
      return formatMs(gateHoldMs(static_cast<float>(v)));
    case P_COLOR_AMOUNT:
      std::snprintf(buf, sizeof(buf), "%.1f", v * 10.0);
      return buf;
    default:
      std::snprintf(buf, sizeof(buf), "%.2f", v);
      return buf;
  }
}

inline bool parseParamValue(uint32_t p, const std::string& raw, double* out) {
  const ParamSpec& s = kParamSpecs[p];
  const std::string text = lowerText(trimText(raw));
  if (text.empty()) {
    return false;
  }
  for (int i = 0; i < s.labels.count; ++i) {
    if (text == lowerText(s.labels.names[i])) {
      *out = i;
      return true;
    }
  }
  if (text == "on" || text == "true" || text == "yes") {
    *out = 1.0;
    return true;
  }
  if (text == "off" || text == "false" || text == "no") {
    *out = 0.0;
    return true;
  }
  if (text == "out") {
    *out = 0.0;
    return true;
  }

  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (end == text.c_str()) {
    return false;
  }
  const std::string suffix = trimText(std::string(end));

  auto freq = [&]() {
    double hz = value;
    if (suffix == "khz" || suffix == "k") {
      hz *= 1000.0;
    }
    return hz;
  };
  auto ms = [&]() {
    double t = value;
    if (suffix == "s" || suffix == "sec") {
      t *= 1000.0;
    }
    return t;
  };

  switch (p) {
    case P_INPUT:
      *out = (value + 18.0) / 36.0;
      return true;
    case P_OUTPUT: {
      if (value >= 0.0) {
        *out = 0.75 + value / 48.0;
      } else {
        *out = 0.75 * std::pow(10.0, value / 60.0);
      }
      return true;
    }
    case P_COMP_THRESHOLD:
      *out = (10.0 - value) / 30.0;
      return true;
    case P_COMP_MAKEUP:
      *out = (value + 9.0) / 18.0;
      return true;
    case P_HPF: {
      const double hz = freq();
      *out = hz <= 16.0 ? 0.0
                        : std::log(hz / 16.0) / std::log(350.0 / 16.0);
      return true;
    }
    case P_LPF: {
      const double hz = freq();
      *out = hz <= 0.0
                 ? 0.0
                 : std::log(std::clamp(hz, 3000.0, 15200.0) / 3000.0) /
                       std::log(15200.0 / 3000.0);
      return true;
    }
    case P_LF_GAIN:
    case P_LM_GAIN:
    case P_HM_GAIN:
    case P_HF_GAIN:
      *out = (value + 15.0) / 30.0;
      return true;
    case P_LF_FREQ: {
      const double hz = freq();
      *out = std::log(std::clamp(hz, 30.0, 450.0) / 30.0) /
             std::log(450.0 / 30.0);
      return true;
    }
    case P_LM_FREQ: {
      const double hz = freq();
      *out = std::log(std::clamp(hz, 200.0, 2500.0) / 200.0) /
             std::log(2500.0 / 200.0);
      return true;
    }
    case P_HM_FREQ: {
      const double hz = freq();
      *out = std::log(std::clamp(hz, 600.0, 7000.0) / 600.0) /
             std::log(7000.0 / 600.0);
      return true;
    }
    case P_HF_FREQ: {
      const double hz = freq();
      *out = std::log(std::clamp(hz, 1500.0, 16000.0) / 1500.0) /
             std::log(16000.0 / 1500.0);
      return true;
    }
    case P_LM_Q:
    case P_HM_Q:
      *out = std::log(std::clamp(value, 0.5, 3.0) / 0.5) /
             std::log(3.0 / 0.5);
      return true;
    case P_GATE_THRESHOLD:
      *out = (value + 30.0) / 40.0;
      return true;
    case P_GATE_RANGE:
      *out = value / 40.0;
      return true;
    case P_GATE_RELEASE:
      *out = std::log(std::clamp(ms(), 100.0, 4000.0) / 100.0) /
             std::log(40.0);
      return true;
    case P_GATE_HOLD:
      *out = ms() / 500.0;
      return true;
    case P_COLOR_AMOUNT:
      *out = value / 10.0;
      return true;
    default:
      *out = value;
      return true;
  }
}

} // namespace suite
