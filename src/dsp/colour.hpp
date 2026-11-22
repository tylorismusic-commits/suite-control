// Suite Channel — colour.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Single-band Patina saturation engine: Valve, Console, Bus, Tape, Transformer.
// One Amount control drives both the pre-shaper drive and the dry/wet mix, so
// 0 is exactly dry and 1 is the full model.
#pragma once

#include <algorithm>
#include <cmath>

#include <hdsp/hdsp.hpp>

#include "controls.hpp"

namespace suite {

class ColourStage {
public:
  void prepare(float sampleRate) noexcept {
    rate_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    inputSmooth_.setTime(12.0f, rate_);
    driveSmooth_.setTime(15.0f, rate_);
    mixSmooth_.setTime(10.0f, rate_);
    outputSmooth_.setTime(15.0f, rate_);
    autoPowerCoeff_ = hdsp::timeCoeff(120.0f, rate_);
    autoDbCoeff_ = hdsp::timeCoeff(250.0f, rate_);
    inPower_ = 0.0f;
    outPower_ = 0.0f;
    autoDb_ = 0.0f;
    reset();
    setFilters();
    snap();
  }

  void setControls(int model, float amount) noexcept {
    model_ = std::min(std::max(model, 0), 4);
    amount_ = clamp01(amount);
    inputSmooth_.setTarget(1.0f);
    driveSmooth_.setTarget(hdsp::dbToGain(24.0f * amount_ - 12.0f));
    mixSmooth_.setTarget(amount_);
    outputSmooth_.setTarget(1.0f);
    setFilters();
  }

  void snap() noexcept {
    inputSmooth_.snap();
    driveSmooth_.snap();
    mixSmooth_.snap();
    outputSmooth_.snap();
  }

  void reset() noexcept {
    for (int ch = 0; ch < 2; ++ch) {
      pre_[ch].reset();
      tone_[ch].reset();
      lp_[ch].reset();
      hp_[ch].reset();
      shaper_[ch].reset();
    }
    inPower_ = 0.0f;
    outPower_ = 0.0f;
    autoDb_ = 0.0f;
  }

  inline void process(float& left, float& right) noexcept {
    const float in = inputSmooth_.next();
    const float drive = driveSmooth_.next();
    const float mix = mixSmooth_.next();
    const float out = outputSmooth_.next();

    const float dryL = left * in;
    const float dryR = right * in;

    const float k = 1.0f + 5.0f * amount_;
    const float trim = hdsp::dbToGain(kModelTrimDb[model_]);

    float xL = pre_[0].process(dryL * drive);
    float xR = pre_[1].process(dryR * drive);

    xL = shaper_[0].process(xL, k, biasFor(0));
    xR = shaper_[1].process(xR, k, biasFor(1));
    xL = tone_[0].process(xL);
    xR = tone_[1].process(xR);
    if (model_ == static_cast<int>(Colour::Tape)) {
      xL = lp_[0].process(xL);
      xR = lp_[1].process(xR);
    }
    xL = hp_[0].process(xL);
    xR = hp_[1].process(xR);
    xL *= trim;
    xR *= trim;

    // Slow RMS auto-gain so the Amount knob is level-neutral.
    const float inPower = 0.5f * (dryL * dryL + dryR * dryR);
    const float outPower = 0.5f * (xL * xL + xR * xR);
    inPower_ += (inPower - inPower_) * (1.0f - autoPowerCoeff_);
    outPower_ += (outPower - outPower_) * (1.0f - autoPowerCoeff_);
    const float wanted =
        hdsp::gainToDb(std::sqrt(inPower_ + 1e-20f)) -
        hdsp::gainToDb(std::sqrt(outPower_ + 1e-20f));
    autoDb_ += (wanted - autoDb_) * (1.0f - autoDbCoeff_);
    const float make =
        hdsp::dbToGain(std::min(std::max(autoDb_, -12.0f), 12.0f));
    xL *= make;
    xR *= make;

    const float yL = (dryL + mix * (xL - dryL)) * out;
    const float yR = (dryR + mix * (xR - dryR)) * out;
    left = std::isfinite(yL) ? yL : 0.0f;
    right = std::isfinite(yR) ? yR : 0.0f;
  }

private:
  void setFilters() noexcept {
    const float tone = 0.5f;
    const float character = 0.5f;
    const float high = (tone - 0.5f) * 12.0f;
    const float low = (0.5f - tone) * 8.0f;
    for (int ch = 0; ch < 2; ++ch) {
      float preHz = 160.0f;
      float preDb = low * 0.35f;
      switch (static_cast<Colour>(model_)) {
        case Colour::Console:
          preHz = 150.0f;
          preDb += 1.5f * character;
          break;
        case Colour::Bus:
          preHz = 120.0f;
          preDb += 1.0f * character;
          break;
        case Colour::Tape:
          preHz = 90.0f;
          preDb += 4.0f * character;
          break;
        case Colour::Transformer:
          preHz = 60.0f;
          preDb += 6.0f * character;
          break;
        default:
          preDb += 1.2f * character;
          break;
      }
      pre_[ch].lowShelf(preHz, preDb, 0.7f, rate_);
      tone_[ch].highShelf(3500.0f + 1500.0f * tone, high, 0.7f, rate_);
      if (model_ == static_cast<int>(Colour::Tape)) {
        lp_[ch].lowPass(12000.0f + 6000.0f * tone, 0.7071f, rate_);
      } else {
        lp_[ch].lowPass(21000.0f, 0.7071f, rate_);
      }
      hp_[ch].highPass(model_ == static_cast<int>(Colour::Transformer) ? 22.0f
                                                                       : 10.0f,
                       0.7071f, rate_);
    }
  }

  float biasFor(int ch) const noexcept {
    const float character = 0.5f;
    float bias = 0.10f + 0.20f * character;
    switch (static_cast<Colour>(model_)) {
      case Colour::Console:
        bias = 0.04f + 0.10f * character +
               (ch == 0 ? 0.02f : -0.02f) * character;
        break;
      case Colour::Bus:
        bias = 0.02f + 0.08f * character;
        break;
      case Colour::Tape:
        bias = 0.06f + 0.12f * character;
        break;
      case Colour::Transformer:
        bias = 0.14f + 0.22f * character;
        break;
      default:
        break;
    }
    return bias;
  }

  static constexpr float kModelTrimDb[5] = {0.0f, -1.0f, -1.5f, -1.0f, -1.0f};

  float rate_ = 48000.0f;
  int model_ = 1;
  float amount_ = 0.35f;
  hdsp::Smoothed inputSmooth_;
  hdsp::Smoothed driveSmooth_;
  hdsp::Smoothed mixSmooth_;
  hdsp::Smoothed outputSmooth_;
  hdsp::Biquad pre_[2];
  hdsp::Biquad tone_[2];
  hdsp::Biquad lp_[2];
  hdsp::Biquad hp_[2];
  hdsp::AsymShaper shaper_[2];
  float inPower_ = 0.0f;
  float outPower_ = 0.0f;
  float autoDb_ = 0.0f;
  float autoPowerCoeff_ = 0.0f;
  float autoDbCoeff_ = 0.0f;
};

} // namespace suite
