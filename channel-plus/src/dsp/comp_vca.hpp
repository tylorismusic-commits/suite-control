// Suite Channel — comp_vca.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SSL-style feed-forward VCA compressor, adapted from Comp Suite's BusVca.
// The ratio is fixed at 3.5:1, the attack/release are the channel-strip
// presets, and the F.ATK switch selects 30 ms or 3 ms.
#pragma once

#include <algorithm>
#include <cmath>

#include <hdsp/hdsp.hpp>

#include "controls.hpp"

namespace suite {

class BusVca {
public:
  void prepare(float sampleRate) noexcept {
    sr_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    holdSamples_ = static_cast<int>(0.002f * sr_);
    reset();
    setControls(controls_);
  }

  void setControls(const Controls& c) noexcept {
    controls_ = c;
    thresholdDb_ = compThresholdDb(c.compThreshold);
    ratio_ = 3.5f;
    attackMs_ = c.compFast ? 3.0f : 30.0f;
    releaseMs_ = 300.0f;
    attackCoeff_ = hdsp::timeCoeff(attackMs_, sr_);
    releaseCoeff_ = hdsp::timeCoeff(releaseMs_, sr_);
    makeupGain_ = hdsp::dbToGain(compMakeupDb(c.compMakeup));
  }

  // Processes one stereo frame in place. Returns gain reduction in dB (>= 0).
  float process(float& left, float& right) noexcept {
    const float levelL = std::fabs(left);
    const float levelR = std::fabs(right);
    const float loudest = std::max(levelL, levelR);

    updateDetector(0, loudest);
    updateDetector(1, loudest);
    const float grL =
        hdsp::gainReductionDb(hdsp::gainToDb(env_[0]), thresholdDb_, ratio_,
                              kKneeDb);
    const float grR =
        hdsp::gainReductionDb(hdsp::gainToDb(env_[1]), thresholdDb_, ratio_,
                              kKneeDb);

    const float gainL = hdsp::dbToGain(-grL) * makeupGain_;
    const float gainR = hdsp::dbToGain(-grR) * makeupGain_;
    left = shaper_[0].process(left * gainL, 1.7f, 0.13f);
    right = shaper_[1].process(right * gainR, 1.7f, 0.13f);
    return std::max(std::max(grL, grR), 0.0f);
  }

  void reset() noexcept {
    for (int ch = 0; ch < 2; ++ch) {
      env_[ch] = 0.0f;
      hold_[ch] = 0;
      shaper_[ch].reset();
    }
  }

private:
  static constexpr float kKneeDb = 2.0f;

  void updateDetector(int ch, float level) noexcept {
    if (level > env_[ch]) {
      env_[ch] = attackCoeff_ * env_[ch] + (1.0f - attackCoeff_) * level;
      hold_[ch] = holdSamples_;
    } else if (hold_[ch] > 0) {
      --hold_[ch];
    } else {
      env_[ch] =
          releaseCoeff_ * env_[ch] + (1.0f - releaseCoeff_) * level;
    }
  }

  float sr_ = 48000.0f;
  Controls controls_;
  float thresholdDb_ = -12.0f;
  float ratio_ = 3.5f;
  float attackMs_ = 30.0f;
  float releaseMs_ = 300.0f;
  float attackCoeff_ = 0.0f;
  float releaseCoeff_ = 0.0f;
  float makeupGain_ = 1.0f;
  int holdSamples_ = 0;
  float env_[2] = {0.0f, 0.0f};
  int hold_[2] = {0, 0};
  hdsp::AsymShaper shaper_[2];
};

} // namespace suite
