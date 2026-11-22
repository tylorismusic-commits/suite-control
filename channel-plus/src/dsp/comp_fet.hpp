// Suite Channel — comp_fet.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// 76Blue FET compressor, adapted from Comp Suite's FetLimiter Blue variant.
// The strip's Threshold is a real threshold and the FET input is held at
// unity, so the same control surface works as the VCA. The ratio is fixed at
// 4:1 and the F.ATK switch selects the slow or fast attack step.
#pragma once

#include <algorithm>
#include <cmath>

#include <hdsp/hdsp.hpp>

#include "controls.hpp"

namespace suite {

class FetLimiter {
public:
  void prepare(float sampleRate) noexcept {
    sr_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    inputBias_ = 0.26f;
    fetBias_ = 0.0f;
    baseDrive_ = 1.30f;
    inputDriveScale_ = 1.60f;
    for (int ch = 0; ch < 2; ++ch) {
      lpOut_[ch].set(20000.0f, sr_);
      dcLp_[ch].set(5.0f, sr_);
      lpOut_[ch].reset();
      dcLp_[ch].reset();
      inShaper_[ch].reset();
      fetShaper_[ch].reset();
    }
    inputSmooth_.setTime(25.0f, sr_);
    driveSmooth_.setTime(25.0f, sr_);
    makeupSmooth_.setTime(15.0f, sr_);
    outputSmooth_.setTime(15.0f, sr_);
    overDb_ = 0.0f;
    setControls(controls_);
    inputSmooth_.snap();
    driveSmooth_.snap();
    makeupSmooth_.snap();
    outputSmooth_.snap();
  }

  void setControls(const Controls& c) noexcept {
    controls_ = c;

    // Keep the FET input at unity and use the strip's Threshold as a real
    // threshold.  The 1176 has no threshold control, but a channel strip does;
    // this keeps the same control surface as the VCA while retaining the FET
    // gain cell, attack character and program-dependent colour.
    constexpr float kUnityInput = 0.603f;  // 0 dB through the -80..+23 dB law
    inputSmooth_.setTarget(1.0f);
    driveSmooth_.setTarget(baseDrive_ + inputDriveScale_ * kUnityInput);
    thresholdDb_ = compThresholdDb(c.compThreshold);

    ratio_ = 4.0f;
    allButtons_ = false;

    const float attack = c.compFast ? 0.0f : 0.85f;
    const float attackStep =
        1.0f + std::round(clamp01(attack) * 6.0f);
    attackMs_ = 0.04f * std::pow(40.0f, (attackStep - 1.0f) / 6.0f);

    const float release = 0.65f;
    const float releaseStep =
        1.0f + std::round(clamp01(release) * 6.0f);
    releaseMs_ = 50.0f * std::pow(22.0f, (releaseStep - 1.0f) / 6.0f);

    attackCoeff_ = hdsp::timeCoeff(attackMs_, sr_);
    releaseCoeff_ = hdsp::timeCoeff(releaseMs_, sr_);
    releaseDeepCoeff_ = hdsp::timeCoeff(releaseMs_ * 1.35f, sr_);

    outputSmooth_.setTarget(1.0f);
    makeupSmooth_.setTarget(hdsp::dbToGain(18.0f * (clamp01(c.compMakeup) - 0.5f)));
  }

  // Processes one stereo frame in place. Returns gain reduction in dB (>= 0).
  float process(float& left, float& right) noexcept {
    const float inputGain = inputSmooth_.next();
    const float drive = driveSmooth_.next();
    const float inL = left * inputGain;
    const float inR = right * inputGain;

    const float peak = std::max(std::fabs(inL), std::fabs(inR));
    const float levelDb = hdsp::gainToDb(peak);

    constexpr float kKneeDb = 1.0f;
    const float over = hdsp::gainReductionDbSlope(
        levelDb, thresholdDb_, 1.0f, kKneeDb, 1.0e9f);

    if (over > overDb_) {
      overDb_ += (over - overDb_) * (1.0f - attackCoeff_);
    } else {
      overDb_ += (over - overDb_) * (1.0f - releaseCoeff_);
    }

    const float slope = 1.0f - 1.0f / std::max(ratio_, 1.0f);
    const float grDb = std::max(overDb_ * slope, 0.0f);
    const float gain = hdsp::dbToGain(-grDb);

    const float grNorm = std::min(std::max(grDb / 20.0f, 0.0f), 1.5f);
    const float fetDrive = 2.2f + 3.2f * grNorm + 0.66f;

    const float makeup = makeupSmooth_.next();
    const float output = outputSmooth_.next();
    left = processChannel(inL * gain, 0, fetDrive, drive) * makeup * output;
    right = processChannel(inR * gain, 1, fetDrive, drive) * makeup * output;
    return grDb;
  }

  void reset() noexcept {
    for (int ch = 0; ch < 2; ++ch) {
      lpOut_[ch].reset();
      dcLp_[ch].reset();
      inShaper_[ch].reset();
      fetShaper_[ch].reset();
    }
    overDb_ = 0.0f;
    makeupSmooth_.snap();
    outputSmooth_.snap();
  }

private:
  float processChannel(float x, int ch, float fetDrive,
                       float inputDrive) noexcept {
    const float y1 = inShaper_[ch].process(x, inputDrive, inputBias_);
    const float y2 = fetShaper_[ch].process(y1, fetDrive, fetBias_);
    return lpOut_[ch].process(y2 - dcLp_[ch].process(y2));
  }

  float sr_ = 48000.0f;
  Controls controls_;
  float inputBias_ = 0.26f;
  float fetBias_ = 0.0f;
  float baseDrive_ = 1.30f;
  float inputDriveScale_ = 1.60f;
  float ratio_ = 4.0f;
  float thresholdDb_ = -28.0f;
  bool allButtons_ = false;
  float attackMs_ = 0.9f;
  float releaseMs_ = 370.0f;
  float attackCoeff_ = 0.0f;
  float releaseCoeff_ = 0.0f;
  float releaseDeepCoeff_ = 0.0f;
  float overDb_ = 0.0f;
  hdsp::OnePoleLP lpOut_[2];
  hdsp::OnePoleLP dcLp_[2];
  hdsp::Smoothed inputSmooth_;
  hdsp::Smoothed driveSmooth_;
  hdsp::Smoothed makeupSmooth_;
  hdsp::Smoothed outputSmooth_;
  hdsp::AsymShaper inShaper_[2];
  hdsp::AsymShaper fetShaper_[2];
};

} // namespace suite
