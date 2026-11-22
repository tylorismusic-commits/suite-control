// Suite Control — console summing character
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The host already sums the folder; this stage adds the console colour that a
// summing box contributes: input-transformer saturation, a touch of low-end
// weight, inter-channel crosstalk, a noise floor and a soft clip.  Amount 0
// is bit-exact dry.
#pragma once

#include <algorithm>
#include <cmath>

#include <hdsp/hdsp.hpp>

#include "controls.hpp"

namespace suite {

class Summing {
public:
  void prepare(float sampleRate) noexcept {
    sr_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    hpL_.highPass(20.0f, 0.707f, sr_);
    hpR_.highPass(20.0f, 0.707f, sr_);
    tiltL_.lowShelf(90.0f, 1.5f, 0.6f, sr_);
    tiltR_.lowShelf(90.0f, 1.5f, 0.6f, sr_);
    reset();
  }

  void setControls(const Controls& c) noexcept {
    amount_ = clamp01(c.sumAmount);
  }

  void reset() noexcept {
    hpL_.reset();
    hpR_.reset();
    tiltL_.reset();
    tiltR_.reset();
    shaperL_.reset();
    shaperR_.reset();
    noiseState_ = 0x12345678u;
  }

  inline void process(float& left, float& right) noexcept {
    const float a = amount_;
    if (a <= 0.0f) return;

    float l = tiltL_.process(hpL_.process(left));
    float r = tiltR_.process(hpR_.process(right));

    // Input transformer: asymmetric, mostly even-order.
    l = shaperL_.process(l, 0.25f + 1.1f * a, 0.05f);
    r = shaperR_.process(r, 0.25f + 1.1f * a, 0.05f);

    // Console crosstalk.
    const float bleed = 0.035f * a;
    const float cl = l * (1.0f - bleed) + r * bleed;
    const float cr = r * (1.0f - bleed) + l * bleed;

    // Noise floor.
    const float n = (nextNoise() - 0.5f) * 0.00035f * a;

    // Soft clip and a little make-up so the amount is level-neutral.
    const float drive = 1.0f + 0.45f * a;
    const float comp = 1.0f / (1.0f + 0.10f * a);
    const float ol = std::tanh((cl + n) * drive) * comp;
    const float orr = std::tanh((cr + n) * drive) * comp;

    left += (ol - left) * a;
    right += (orr - right) * a;
  }

private:
  float nextNoise() noexcept {
    noiseState_ = noiseState_ * 1664525u + 1013904223u;
    return static_cast<float>(noiseState_ >> 8) / 16777216.0f;
  }

  float sr_ = 48000.0f;
  float amount_ = 0.0f;
  uint32_t noiseState_ = 0x12345678u;
  hdsp::Biquad hpL_, hpR_, tiltL_, tiltR_;
  hdsp::AsymShaper shaperL_, shaperR_;
};

} // namespace suite
