// Suite Channel — gate.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stereo-linked SSL-style expander/gate/ducker.  Gate mode uses variable
// hysteresis and Hold; expander mode is a fixed 1:2 downward expansion; ducker
// mode attenuates above the threshold.  A patched sidechain keys the detector
// when available, otherwise the main signal is used.
#pragma once

#include <algorithm>
#include <cmath>

#include <hdsp/hdsp.hpp>

#include "controls.hpp"

namespace suite {

class GateExpander {
public:
  void prepare(float sampleRate) noexcept {
    sr_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    attackSlow_ = hdsp::timeCoeff(1.5f, sr_);
    attackFast_ = hdsp::timeCoeff(0.1f, sr_);
    // Peak envelope for the detector so a sine does not chatter the gate on
    // every zero crossing.
    detAttackCoeff_ = hdsp::timeCoeff(0.5f, sr_);
    detReleaseCoeff_ = hdsp::timeCoeff(20.0f, sr_);
    reset();
    setControls(controls_);
  }

  void setControls(const Controls& c) noexcept {
    controls_ = c;
    enabled_ = c.gateIn;
    mode_ = static_cast<GateMode>(std::min(std::max(c.gateMode, 0), 2));
    openDb_ = gateThresholdDb(c.gateThreshold);
    rangeDb_ = gateRangeDb(c.gateRange);
    releaseCoeff_ = hdsp::timeCoeff(gateReleaseMs(c.gateRelease), sr_);
    attackCoeff_ = c.gateFast ? attackFast_ : attackSlow_;
    holdSamples_ =
        static_cast<int>(gateHoldMs(c.gateHold) * 0.001f * sr_);
    // Variable hysteresis: more as the threshold is lowered.
    hysteresisDb_ = 2.0f + 6.0f * (1.0f - clamp01(c.gateThreshold));
    closeDb_ = openDb_ - hysteresisDb_;
    if (holdCounter_ > holdSamples_) {
      holdCounter_ = holdSamples_;
    }
  }

  // Processes one stereo frame in place. Returns gate gain reduction in dB.
  // scL/scR key the detector when scActive is true.
  float process(float& left, float& right, float scL = 0.0f,
                float scR = 0.0f, bool scActive = false) noexcept {
    if (!enabled_ || rangeDb_ <= 0.01f) {
      grDb_ = 0.0f;
      holdCounter_ = 0;
      env_ = 0.0f;
      return 0.0f;
    }

    const float detector =
        scActive ? std::max(std::fabs(scL), std::fabs(scR))
                 : std::max(std::fabs(left), std::fabs(right));
    const float detCoeff =
        detector > env_ ? detAttackCoeff_ : detReleaseCoeff_;
    env_ = detCoeff * env_ + (1.0f - detCoeff) * detector;
    const float levelDb = hdsp::gainToDb(env_ + 1.0e-12f);

    float targetGr = 0.0f;
    if (mode_ == GateMode::Expander) {
      // Fixed 1:2 downward expansion, capped by Range.
      targetGr = std::min(std::max(0.5f * (openDb_ - levelDb), 0.0f), rangeDb_);
    } else {
      if (levelDb >= openDb_) {
        state_ = true;
        holdCounter_ = holdSamples_;
      } else if (levelDb <= closeDb_) {
        if (holdCounter_ > 0) {
          --holdCounter_;
        } else {
          state_ = false;
        }
      }
      if (mode_ == GateMode::Gate) {
        targetGr = state_ ? 0.0f : rangeDb_;
      } else {
        targetGr = state_ ? rangeDb_ : 0.0f;
      }
    }

    const float coeff = targetGr < grDb_ ? attackCoeff_ : releaseCoeff_;
    grDb_ = coeff * grDb_ + (1.0f - coeff) * targetGr;
    if (grDb_ < 1.0e-5f) {
      grDb_ = 0.0f;
    }

    const float gain = hdsp::dbToGain(-grDb_);
    left *= gain;
    right *= gain;
    return grDb_;
  }

  void reset() noexcept {
    grDb_ = 0.0f;
    state_ = false;
    holdCounter_ = 0;
    env_ = 0.0f;
  }

private:
  float sr_ = 48000.0f;
  Controls controls_;
  bool enabled_ = false;
  GateMode mode_ = GateMode::Gate;
  bool state_ = false;
  float openDb_ = -20.0f;
  float closeDb_ = -26.0f;
  float hysteresisDb_ = 6.0f;
  float rangeDb_ = 24.0f;
  float attackSlow_ = 0.0f;
  float attackFast_ = 0.0f;
  float attackCoeff_ = 0.0f;
  float releaseCoeff_ = 0.0f;
  int holdSamples_ = 0;
  int holdCounter_ = 0;
  float detAttackCoeff_ = 0.0f;
  float detReleaseCoeff_ = 0.0f;
  float env_ = 0.0f;
  float grDb_ = 0.0f;
};

} // namespace suite
