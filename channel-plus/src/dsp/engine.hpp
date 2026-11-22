// Suite Channel — engine.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Input → Colour → Gate → Compressor → HPF/LPF → 4-band EQ → Output.
// DYN POST moves gate+comp after the EQ.  The Dusk EQ core keeps its own
// oversampling; everything else is zero-latency.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include <hdsp/hdsp.hpp>

#include "colour.hpp"
#include "comp_fet.hpp"
#include "comp_vca.hpp"
#include "controls.hpp"
#include "eq4k.hpp"
#include "gate.hpp"

namespace suite {

class Engine {
public:
  static constexpr int kMaxBlock = 8192;

  void prepare(float sampleRate, float oversampleMode = -1.0f) noexcept {
    baseRate_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    if (oversampleMode >= 0.0f) {
      controls_.oversample = oversampleMode;
    }
    eq_.prepare(baseRate_, 4096,
                static_cast<int>(controls_.oversample + 0.5f));
    colour_.prepare(baseRate_);
    gate_.prepare(baseRate_);
    vca_.prepare(baseRate_);
    fet_.prepare(baseRate_);
    inputSmooth_.setTime(15.0f, baseRate_);
    outputSmooth_.setTime(15.0f, baseRate_);
    enableSmooth_.setTime(10.0f, baseRate_);
    eqEnableSmooth_.setTime(10.0f, baseRate_);
    autoCoeff_ = hdsp::timeCoeff(2000.0f, baseRate_);
    typeFadeStep_ = 1.0f / (0.02f * baseRate_);
    setControls(controls_);
    inputSmooth_.snap();
    outputSmooth_.snap();
    enableSmooth_.snap();
    eqEnableSmooth_.snap();
    reset();
  }

  // Kept for the CLAP wrapper's activation flow; the EQ core handles its own
  // oversampling and reports latency from the current factor.
  void setDeferOversample(bool) noexcept {}

  void setControls(const Controls& c) noexcept {
    if (c.compType != controls_.compType) {
      typeFrom_ = controls_.compType;
      typeTarget_ = c.compType;
      typeFade_ = 0.0f;
      if (typeTarget_ == static_cast<int>(CompType::Vca)) {
        vca_.reset();
      } else {
        fet_.reset();
      }
    }
    controls_ = c;
    colour_.setControls(c.colour, c.colourAmount);
    gate_.setControls(c);
    vca_.setControls(c);
    fet_.setControls(c);
    eq_.setControls(c);
    inputSmooth_.setTarget(hdsp::dbToGain(inputGainDb(c.input)));
    outputSmooth_.setTarget(outputGainLinear(c.output));
    enableSmooth_.setTarget(c.enabled ? 1.0f : 0.0f);
    eqEnableSmooth_.setTarget(c.eqIn ? 1.0f : 0.0f);
  }

  void process(float* left, float* right, int frames, Meters& meters,
               const float* scLeft = nullptr,
               const float* scRight = nullptr) noexcept {
    meters = Meters{};

    float inPeakL = 0.0f;
    float inPeakR = 0.0f;
    float outPeakL = 0.0f;
    float outPeakR = 0.0f;
    float grMax = 0.0f;
    float gateGrMax = 0.0f;

    for (int start = 0; start < frames; start += kMaxBlock) {
      const int n = std::min(kMaxBlock, frames - start);
      float* l = left + start;
      float* r = right + start;
      const float* scL = scLeft ? scLeft + start : nullptr;
      const float* scR = scRight ? scRight + start : nullptr;
      // A host may provide a silent buffer for an unpatched optional
      // sidechain. Treat the key as connected only after it carries signal;
      // otherwise an unpatched gate would be keyed by silence and never open.
      if (scL && scR) {
        for (int i = 0; i < n; ++i) {
          if (std::fabs(scL[i]) > 1.0e-6f || std::fabs(scR[i]) > 1.0e-6f) {
            sidechainSeen_ = true;
            break;
          }
        }
      }
      const bool scActive = scL && scR && sidechainSeen_;
      for (int i = 0; i < n; ++i) {
        dryL_[i] = l[i];
        dryR_[i] = r[i];

        float xl = l[i];
        float xr = r[i];
        if (controls_.polarity) {
          xl = -xl;
          xr = -xr;
        }
        const float inGain = inputSmooth_.next();
        xl *= inGain;
        xr *= inGain;
        // The input meter sits after the input knob, before the colour stage.
        inPeakL = std::max(inPeakL, std::fabs(xl));
        inPeakR = std::max(inPeakR, std::fabs(xr));

        colour_.process(xl, xr);

        if (!controls_.dynPost) {
          const float gateGr =
              processDynamics(xl, xr, scL ? scL[i] : 0.0f,
                              scR ? scR[i] : 0.0f, scActive);
          gateGrMax = std::max(gateGrMax, gateGr);
          grMax = std::max(grMax, lastCompGr_);
        }
        l[i] = xl;
        r[i] = xr;
      }

      // EQ core.  EQ IN is a constant-latency crossfade against a delayed
      // dry copy, so toggling it never changes the reported PDC.
      for (int i = 0; i < n; ++i) {
        preEqL_[i] = l[i];
        preEqR_[i] = r[i];
      }
      eq_.processBlock(l, r, n);
      const int delayLen = std::min(eq_.latencySamples(), kEqDelay - 1);
      for (int i = 0; i < n; ++i) {
        eqDelayL_[eqDelayPos_] = preEqL_[i];
        eqDelayR_[eqDelayPos_] = preEqR_[i];
        const int readPos =
            (eqDelayPos_ - delayLen + kEqDelay) % kEqDelay;
        const float dryL = eqDelayL_[readPos];
        const float dryR = eqDelayR_[readPos];
        eqDelayPos_ = (eqDelayPos_ + 1) % kEqDelay;
        const float en = eqEnableSmooth_.next();
        l[i] = dryL + en * (l[i] - dryL);
        r[i] = dryR + en * (r[i] - dryR);
      }

      if (controls_.dynPost) {
        for (int i = 0; i < n; ++i) {
          const float gateGr =
              processDynamics(l[i], r[i], scL ? scL[i] : 0.0f,
                              scR ? scR[i] : 0.0f, scActive);
          gateGrMax = std::max(gateGrMax, gateGr);
          grMax = std::max(grMax, lastCompGr_);
        }
      }

      for (int i = 0; i < n; ++i) {
        const float outGain = outputSmooth_.next();
        const float en = enableSmooth_.next();
        float yl = l[i] * outGain;
        float yr = r[i] * outGain;
        if (controls_.limiterOn) {
          yl = safetyClip(yl);
          yr = safetyClip(yr);
        }
        l[i] = dryL_[i] + en * (yl - dryL_[i]);
        r[i] = dryR_[i] + en * (yr - dryR_[i]);
        outPeakL = std::max(outPeakL, std::fabs(l[i]));
        outPeakR = std::max(outPeakR, std::fabs(r[i]));
      }
    }

    // The compressor GR meter follows the active model.
    meters.grDb = grMax;
    meters.gateGrDb = gateGrMax;
    meters.inPeakLDb = hdsp::gainToDb(inPeakL);
    meters.inPeakRDb = hdsp::gainToDb(inPeakR);
    meters.outPeakLDb = hdsp::gainToDb(outPeakL);
    meters.outPeakRDb = hdsp::gainToDb(outPeakR);
    meters.inPeakDb = std::max(meters.inPeakLDb, meters.inPeakRDb);
    meters.outPeakDb = std::max(meters.outPeakLDb, meters.outPeakRDb);
    meters.outVuDb = meters.outPeakDb;
  }

  void resetSidechainLatch() noexcept { sidechainSeen_ = false; }

  void reset() noexcept {
    sidechainSeen_ = false;
    colour_.reset();
    gate_.reset();
    vca_.reset();
    fet_.reset();
    eq_.reset();
    autoGr_ = 0.0f;
    lastCompGr_ = 0.0f;
    inputSmooth_.snap();
    outputSmooth_.snap();
    enableSmooth_.snap();
    eqEnableSmooth_.snap();
    eqDelayPos_ = 0;
    eqDelayL_.fill(0.0f);
    eqDelayR_.fill(0.0f);
    typeFrom_ = controls_.compType;
    typeTarget_ = controls_.compType;
    typeFade_ = 1.0f;
    lastCompGr_ = 0.0f;
  }

  int latencySamples() const noexcept { return eq_.latencySamples(); }

private:
  static float safetyClip(float x) noexcept {
    constexpr float kCeiling = 0.9772372f;  // −0.2 dBFS
    const float a = std::fabs(x);
    if (a <= kCeiling) {
      return x;
    }
    return std::copysign(kCeiling * std::tanh(a / kCeiling), x);
  }

  // Returns gate GR in dB for this frame; updates lastCompGr_.
  float processDynamics(float& l, float& r, float scL, float scR,
                        bool scActive) noexcept {
    float gateGr = 0.0f;
    if (controls_.gateIn) {
      gateGr = gate_.process(l, r, scL, scR, scActive);
    }
    if (!controls_.compIn) {
      lastCompGr_ = 0.0f;
      autoGr_ = 0.0f;
      return gateGr;
    }

    float gr = 0.0f;
    if (typeFade_ >= 0.9999f) {
      gr = typeTarget_ == static_cast<int>(CompType::Vca)
               ? vca_.process(l, r)
               : fet_.process(l, r);
    } else {
      float aL = l;
      float aR = r;
      float bL = l;
      float bR = r;
      const float grA = vca_.process(aL, aR);
      const float grB = fet_.process(bL, bR);
      const float f = typeFade_;
      if (typeFrom_ == static_cast<int>(CompType::Vca)) {
        l = aL * (1.0f - f) + bL * f;
        r = aR * (1.0f - f) + bR * f;
        gr = grA * (1.0f - f) + grB * f;
      } else {
        l = bL * (1.0f - f) + aL * f;
        r = bR * (1.0f - f) + aR * f;
        gr = grB * (1.0f - f) + grA * f;
      }
      typeFade_ = std::min(1.0f, typeFade_ + typeFadeStep_);
    }

    if (controls_.compAuto) {
      autoGr_ += (gr - autoGr_) * (1.0f - autoCoeff_);
      const float make = hdsp::dbToGain(std::min(autoGr_, 18.0f));
      l *= make;
      r *= make;
    }
    lastCompGr_ = gr;
    return gateGr;
  }

  float baseRate_ = 48000.0f;
  Controls controls_;
  ColourStage colour_;
  GateExpander gate_;
  BusVca vca_;
  FetLimiter fet_;
  Eq4k eq_;

  hdsp::Smoothed inputSmooth_;
  hdsp::Smoothed outputSmooth_;
  hdsp::Smoothed enableSmooth_;
  hdsp::Smoothed eqEnableSmooth_;

  float autoGr_ = 0.0f;
  float autoCoeff_ = 0.0f;
  float lastCompGr_ = 0.0f;

  int typeFrom_ = 0;
  int typeTarget_ = 0;
  float typeFade_ = 1.0f;
  float typeFadeStep_ = 1.0f / (0.02f * 48000.0f);

  static constexpr int kEqDelay = 512;

  std::array<float, kMaxBlock> dryL_{};
  std::array<float, kMaxBlock> dryR_{};
  bool sidechainSeen_ = false;
  std::array<float, kMaxBlock> preEqL_{};
  std::array<float, kMaxBlock> preEqR_{};
  std::array<float, kEqDelay> eqDelayL_{};
  std::array<float, kEqDelay> eqDelayR_{};
  int eqDelayPos_ = 0;
};

} // namespace suite
