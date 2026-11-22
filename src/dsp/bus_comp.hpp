// Suite Control — SSL-style bus compressor
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

#include <hdsp/hdsp.hpp>

#include "controls.hpp"

namespace suite {

// Feed-forward VCA bus compressor: soft knee, program-dependent auto release,
// parallel mix, and the gentle output shaper the channel strip uses.
class BusComp {
public:
  void prepare(float sampleRate) noexcept {
    sr_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    holdSamples_ = static_cast<int>(0.002f * sr_);
    scHpL_.highPass(90.0f, 0.707f, sr_);
    scHpR_.highPass(90.0f, 0.707f, sr_);
    reset();
    setControls(controls_);
  }

  void setControls(const Controls& c) noexcept {
    controls_ = c;
    thresholdDb_ = compThresholdDb(c.compThreshold);
    ratio_ = compRatioValue(c.compRatio);
    attackCoeff_ = hdsp::timeCoeff(compAttackMs(c.compAttack), sr_);
    const float releaseMs =
        c.compAuto ? 380.0f : compReleaseMs(c.compRelease);
    releaseCoeff_ = hdsp::timeCoeff(releaseMs, sr_);
    mix_ = compMixValue(c.compMix);
  }

  float process(float& left, float& right) noexcept {
    // Sidechain is the stereo sum, high-passed so bass does not pump the bus.
    const float scL = scHpL_.process(left);
    const float scR = scHpR_.process(right);
    const float loudest = std::max(std::fabs(scL), std::fabs(scR));

    const float target = loudest;
    if (target > env_) {
      env_ = attackCoeff_ * env_ + (1.0f - attackCoeff_) * target;
      hold_ = holdSamples_;
    } else if (hold_ > 0) {
      --hold_;
    } else {
      env_ = releaseCoeff_ * env_ + (1.0f - releaseCoeff_) * target;
    }

    const float gr = hdsp::gainReductionDb(hdsp::gainToDb(env_), thresholdDb_,
                                           ratio_, 4.0f);
    lastGr_ = std::max(gr, 0.0f);
    const float gain = hdsp::dbToGain(-gr);
    const float wetL = shaper_[0].process(left * gain, 1.6f, 0.12f);
    const float wetR = shaper_[1].process(right * gain, 1.6f, 0.12f);
    left = left + (wetL - left) * mix_;
    right = right + (wetR - right) * mix_;
    return lastGr_;
  }

  void reset() noexcept {
    env_ = 0.0f;
    hold_ = 0;
    scHpL_.reset();
    scHpR_.reset();
    shaper_[0].reset();
    shaper_[1].reset();
  }

  float gainReductionDb() const noexcept { return lastGr_; }

private:
  float sr_ = 48000.0f;
  Controls controls_;
  float thresholdDb_ = -12.0f;
  float ratio_ = 4.0f;
  float attackCoeff_ = 0.0f;
  float releaseCoeff_ = 0.0f;
  float mix_ = 1.0f;
  int holdSamples_ = 0;
  float env_ = 0.0f;
  int hold_ = 0;
  float lastGr_ = 0.0f;
  hdsp::Biquad scHpL_, scHpR_;
  hdsp::AsymShaper shaper_[2];
};

} // namespace suite
