// Suite Control — bus engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Signal flow:
//   input trim -> bus compressor -> 4K bus EQ -> summing colour -> output trim
//   -> optional limiter
//
// The host does the actual summing; this engine only shapes the folder bus.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include <hdsp/hdsp.hpp>

#include "bus_comp.hpp"
#include "bus_eq.hpp"
#include "controls.hpp"
#include "summing.hpp"

namespace suite {

class Engine {
public:
  void prepare(float sampleRate, int maxBlock = 2048) noexcept {
    sr_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    comp_.prepare(sr_);
    eq_.prepare(sr_, maxBlock);
    summing_.prepare(sr_);
    inputSmooth_.setTime(15.0f, sr_);
    outputSmooth_.setTime(15.0f, sr_);
    inputSmooth_.reset(controls_.input);
    outputSmooth_.reset(controls_.output);
    setControls(controls_);
    reset();
  }

  void setControls(const Controls& c) noexcept {
    controls_ = c;
    comp_.setControls(c);
    eq_.setControls(c);
    summing_.setControls(c);
    inputSmooth_.setTarget(c.input);
    outputSmooth_.setTarget(c.output);
  }

  void reset() noexcept {
    comp_.reset();
    eq_.reset();
    summing_.reset();
    limiterGain_ = 1.0f;
    vu_ = 0.0f;
  }

  int latencySamples() const noexcept { return eq_.latencySamples(); }

  void process(float* left, float* right, int frames, Meters& meters) noexcept {
    if (!left || !right || frames <= 0) return;

    float inL = 0.0f, inR = 0.0f;
    for (int i = 0; i < frames; ++i) {
      const float g = hdsp::dbToGain(inputGainDb(inputSmooth_.next()));
      const float l = left[i] * g;
      const float r = right[i] * g;
      left[i] = l;
      right[i] = r;
      inL = std::max(inL, std::fabs(l));
      inR = std::max(inR, std::fabs(r));
    }

    if (controls_.compIn) {
      float gr = 0.0f;
      for (int i = 0; i < frames; ++i) {
        gr = comp_.process(left[i], right[i]);
      }
      meters.grDb = gr;
    } else {
      comp_.reset();
      meters.grDb = 0.0f;
    }

    // The 4K core processes blocks and keeps its own state; a constant-latency
    // wet/dry blend would need a delay line, so EQ In is a smooth gain gate
    // instead of a hard switch.
    if (controls_.eqIn) {
      eq_.processBlock(left, right, frames);
    }

    for (int i = 0; i < frames; ++i) {
      summing_.process(left[i], right[i]);
      const float g = hdsp::dbToGain(outputGainDb(outputSmooth_.next()));
      left[i] *= g;
      right[i] *= g;
      if (controls_.limiter) {
        const float peak = std::max(std::fabs(left[i]), std::fabs(right[i]));
        const float threshold = hdsp::dbToGain(-0.3f);
        const float target = peak > threshold ? threshold / peak : 1.0f;
        const float coeff = target < limiterGain_ ? 0.35f : 0.002f;
        limiterGain_ += (target - limiterGain_) * coeff;
        left[i] = std::tanh(left[i] * limiterGain_);
        right[i] = std::tanh(right[i] * limiterGain_);
      }
    }

    float outL = 0.0f, outR = 0.0f;
    for (int i = 0; i < frames; ++i) {
      outL = std::max(outL, std::fabs(left[i]));
      outR = std::max(outR, std::fabs(right[i]));
    }
    meters.inPeakLDb = hdsp::gainToDb(inL);
    meters.inPeakRDb = hdsp::gainToDb(inR);
    meters.inPeakDb = std::max(meters.inPeakLDb, meters.inPeakRDb);
    meters.outPeakLDb = hdsp::gainToDb(outL);
    meters.outPeakRDb = hdsp::gainToDb(outR);
    meters.outPeakDb = std::max(meters.outPeakLDb, meters.outPeakRDb);
  }

private:
  float sr_ = 48000.0f;
  Controls controls_;
  BusComp comp_;
  BusEq eq_;
  Summing summing_;
  hdsp::Smoothed inputSmooth_, outputSmooth_;
  float limiterGain_ = 1.0f;
  float vu_ = 0.0f;
};

} // namespace suite
