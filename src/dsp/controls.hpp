// Suite Control — controls.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

namespace suite {

inline float clamp01(float x) noexcept {
  return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

inline float mapLin(float x, float lo, float hi) noexcept {
  return lo + (hi - lo) * clamp01(x);
}

inline float mapLog(float x, float lo, float hi) noexcept {
  return lo * std::pow(hi / lo, clamp01(x));
}

struct Controls {
  bool compIn = true;
  float compThreshold = 0.72f;  // +10 .. -20 dB
  int compRatio = 0;            // 0 = 2:1, 1 = 4:1, 2 = 10:1
  float compAttack = 0.35f;     // 0.1 .. 30 ms
  float compRelease = 0.45f;    // 0.1 .. 1.2 s
  bool compAuto = true;
  float compMix = 1.0f;

  bool eqIn = true;
  int eqType = 0;
  float lfGain = 0.5f, lfFreq = 0.55f;
  bool lfBell = false;
  float lmGain = 0.5f, lmFreq = 0.45f, lmQ = 0.4f;
  float hmGain = 0.5f, hmFreq = 0.55f, hmQ = 0.4f;
  float hfGain = 0.5f, hfFreq = 0.6f;
  bool hfBell = false;

  float sumAmount = 0.35f;
  float input = 0.5f;
  float output = 0.5f;
  bool limiter = false;
  int bus = 1;
};

struct Meters {
  float grDb = 0.0f;
  float inPeakLDb = -120.0f;
  float inPeakRDb = -120.0f;
  float outPeakLDb = -120.0f;
  float outPeakRDb = -120.0f;
  float inPeakDb = -120.0f;
  float outPeakDb = -120.0f;
};

// Threshold: +10 dB at 0, -20 dB at 1.
inline float compThresholdDb(float v) noexcept {
  return mapLin(v, 10.0f, -20.0f);
}
inline float compRatioValue(int idx) noexcept {
  static constexpr float kRatio[3] = {2.0f, 4.0f, 10.0f};
  return kRatio[std::clamp(idx, 0, 2)];
}
inline float compAttackMs(float v) noexcept {
  return mapLog(v, 0.1f, 30.0f);
}
inline float compReleaseMs(float v) noexcept {
  return mapLog(v, 100.0f, 1200.0f);
}
inline float compMixValue(float v) noexcept { return clamp01(v); }

inline float inputGainDb(float v) noexcept { return mapLin(v, -18.0f, 18.0f); }
inline float outputGainDb(float v) noexcept { return mapLin(v, -18.0f, 18.0f); }

// EQ bands follow Suite Channel's 4K laws so the bus sounds like the strips.
inline float bandGainDb(float v) noexcept { return mapLin(v, -15.0f, 15.0f); }
inline float lfHz(float v) noexcept { return mapLog(v, 30.0f, 450.0f); }
inline float lmHz(float v) noexcept { return mapLog(v, 200.0f, 2500.0f); }
inline float hmHz(float v) noexcept { return mapLog(v, 600.0f, 7000.0f); }
inline float hfHz(float v) noexcept { return mapLog(v, 1500.0f, 16000.0f); }
inline float bandQ(float v) noexcept { return mapLog(v, 0.5f, 3.0f); }

} // namespace suite
