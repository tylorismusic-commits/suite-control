// Suite Channel — eq4k.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Thin wrapper around the vendored, framework-free Dusk 4K EQ 2 core.  The
// core keeps its own oversampling, native console colour and calibrated
// Brown/Black laws; we only map the strip's controls onto it.
#pragma once

#include <algorithm>
#include <cmath>

#include "FourKEQDSP.hpp"

#include "controls.hpp"

namespace suite {

class Eq4k {
public:
  void prepare(float sampleRate, int maxBlock, int oversampleMode) noexcept {
    // Configure the core's factor before prepare so its initial reported
    // latency is already correct for the selected OS mode.
    eq_.setOversampling(oversampleMode);
    eq_.prepare(static_cast<double>(sampleRate), maxBlock);
    configureFixed();
    eq_.reset();
  }

  void reset() noexcept { eq_.reset(); }

  void setControls(const Controls& c) noexcept {
    configureFixed();
    eq_.setEqType(c.eqType);

    eq_.setHpfEnabled(c.hpf > 0.001f);
    if (c.hpf > 0.001f) {
      eq_.setHpfFreqHz(hpfHz(c.hpf));
    }
    eq_.setLpfEnabled(c.lpf > 0.001f);
    if (c.lpf > 0.001f) {
      eq_.setLpfFreqHz(lpfHz(c.lpf));
    }

    eq_.setLfGain(bandGainDb(c.lfGain));
    eq_.setLfFreqHz(lfHz(c.lfFreq));
    eq_.setLfBell(c.lfBell);

    eq_.setLmGain(bandGainDb(c.lmGain));
    eq_.setLmFreqHz(lmHz(c.lmFreq));
    eq_.setLmQ(bandQ(c.lmQ));

    eq_.setHmGain(bandGainDb(c.hmGain));
    eq_.setHmFreqHz(hmHz(c.hmFreq));
    eq_.setHmQ(bandQ(c.hmQ));

    eq_.setHfGain(bandGainDb(c.hfGain));
    eq_.setHfFreqHz(hfHz(c.hfFreq));
    eq_.setHfBell(c.hfBell);

    // EQ IN is handled by a constant-latency wet/dry crossfade in Engine.
    // Never use the core's own bypass here: it changes reported latency.
    eq_.setBypass(false);
    eq_.setOversampling(static_cast<int>(c.oversample + 0.5f));
  }

  void processBlock(float* left, float* right, int frames) noexcept {
    const float* in[2] = {left, right};
    float* out[2] = {left, right};
    eq_.processBlock(in, out, 2, frames);
  }

  int latencySamples() const noexcept { return eq_.getLatencySamples(); }

private:
  void configureFixed() noexcept {
    eq_.setInputGainDb(0.0f);
    eq_.setOutputGainDb(0.0f);
    eq_.setAutoGain(false);
    eq_.setMsMode(false);
    // The core's native console nonlinearity is always present; the user
    // saturation parameter is left at zero so Patina's colour is the only
    // variable voicing stage.
    eq_.setSaturation(0.0f);
  }

  duskaudio::FourKEQDSP eq_;
};

} // namespace suite
