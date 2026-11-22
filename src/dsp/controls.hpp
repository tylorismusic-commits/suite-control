// Suite Channel — controls.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace suite {

enum class Colour : int {
  Valve = 0,
  Console,
  Bus,
  Tape,
  Transformer,
  Count
};

enum class CompType : int { Vca = 0, Blue76, Count };

enum class GateMode : int { Expander = 0, Gate, Ducker, Count };

inline float clamp01(float x) noexcept {
  return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

inline float mapLin(float x, float lo, float hi) noexcept {
  return lo + (hi - lo) * clamp01(x);
}

inline float mapLog(float x, float lo, float hi) noexcept {
  const float v = clamp01(x);
  return lo * std::pow(hi / lo, v);
}

// Every host-facing control is normalised to 0..1.  The DSP maps each one to
// its own physical range.
struct Controls {
  bool enabled = true;
  float input = 0.5f;          // ±18 dB
  bool polarity = false;

  int colour = 1;              // Colour::Console
  float colourAmount = 0.35f;

  float hpf = 0.0f;            // 0 = out, else 16..350 Hz
  float lpf = 0.0f;            // 0 = out, else 3k..15.2k Hz

  int eqType = 0;              // 0 Brown, 1 Black
  bool eqIn = true;
  float lfGain = 0.5f;         // ±15 dB
  float lfFreq = 0.70f;        // 30..450 Hz
  bool lfBell = false;
  float lmGain = 0.5f;
  float lmFreq = 0.637f;       // 200..2500 Hz
  float lmQ = 0.4f;            // 0.5..3.0
  float hmGain = 0.5f;
  float hmFreq = 0.655f;       // 600..7000 Hz
  float hmQ = 0.4f;
  float hfGain = 0.5f;
  float hfFreq = 0.707f;       // 1.5k..16k
  bool hfBell = false;

  int compType = 0;            // CompType::Vca
  bool compIn = false;
  float compThreshold = 0.73f; // +10..−20 dB
  float compMakeup = 0.5f;     // ±9 dB
  bool compFast = false;
  bool compAuto = true;

  bool gateIn = false;
  float gateThreshold = 0.25f; // −30..+10 dB
  float gateRange = 0.6f;      // 0..40 dB
  float gateRelease = 0.5f;    // 0.1..4 s
  float gateHold = 0.0f;       // 0..500 ms
  bool gateFast = false;
  int gateMode = 1;            // GateMode::Gate

  bool dynPost = false;
  float output = 0.75f;        // off..+12 dB fader, 0 dB at 3/4 travel
  float oversample = 2.0f;     // 0 off, 1 2x, 2 4x
  bool limiterOn = true;       // output safety ceiling
  int bus = 0;                 // Suite Control bucket, 0 = not assigned
};

// Metering published by the engine, in dB.
struct Meters {
  float grDb = 0.0f;
  float gateGrDb = 0.0f;
  float inPeakDb = -120.0f;
  float outPeakDb = -120.0f;
  float outVuDb = -120.0f;
  float inPeakLDb = -120.0f;
  float inPeakRDb = -120.0f;
  float outPeakLDb = -120.0f;
  float outPeakRDb = -120.0f;
  float scPeakDb = -120.0f;
  bool scActive = false;
};

inline float inputGainDb(float value) noexcept {
  return mapLin(value, -18.0f, 18.0f);
}

// Output fader: unity at 3/4 travel, +12 dB at the top, and a cubic gain
// taper down to true silence at the bottom.
inline float outputGainLinear(float value) noexcept {
  const float v = clamp01(value);
  if (v <= 0.0f) {
    return 0.0f;
  }
  if (v >= 0.75f) {
    return std::pow(10.0f, (48.0f * (v - 0.75f)) / 20.0f);
  }
  const float t = v / 0.75f;
  return t * t * t;
}

inline float outputGainDb(float value) noexcept {
  const float gain = outputGainLinear(value);
  return gain > 1.0e-9f ? 20.0f * std::log10(gain) : -120.0f;
}

inline float compThresholdDb(float value) noexcept {
  // 0 = +10 dB (least), 1 = −20 dB (most).
  return 10.0f - 30.0f * clamp01(value);
}

inline float compMakeupDb(float value) noexcept {
  return mapLin(value, -9.0f, 9.0f);
}

inline float gateThresholdDb(float value) noexcept {
  return mapLin(value, -30.0f, 10.0f);
}

inline float gateRangeDb(float value) noexcept {
  return mapLin(value, 0.0f, 40.0f);
}

inline float gateReleaseMs(float value) noexcept {
  return 100.0f * std::pow(40.0f, clamp01(value));
}

inline float gateHoldMs(float value) noexcept {
  return 500.0f * clamp01(value);
}

inline float hpfHz(float value) noexcept {
  return mapLog(value, 16.0f, 350.0f);
}

inline float lpfHz(float value) noexcept {
  return mapLog(value, 3000.0f, 15200.0f);
}

inline float lfHz(float value) noexcept {
  return mapLog(value, 30.0f, 450.0f);
}

inline float lmHz(float value) noexcept {
  return mapLog(value, 200.0f, 2500.0f);
}

inline float hmHz(float value) noexcept {
  return mapLog(value, 600.0f, 7000.0f);
}

inline float hfHz(float value) noexcept {
  return mapLog(value, 1500.0f, 16000.0f);
}

inline float bandGainDb(float value) noexcept {
  return mapLin(value, -15.0f, 15.0f);
}

inline float bandQ(float value) noexcept {
  return mapLog(value, 0.5f, 3.0f);
}

} // namespace suite
