// Suite Control — bus EQ (vendored Dusk 4K core)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

#include "FourKEQDSP.hpp"

#include "controls.hpp"

namespace suite {

class BusEq {
public:
  void prepare(float sampleRate, int maxBlock) noexcept {
    // No oversampling on the bus: keeps the reported latency at zero and the
    // console curve is gentle enough that aliasing is not an issue.
    eq_.setOversampling(0);
    eq_.prepare(static_cast<double>(sampleRate), maxBlock);
    eq_.setInputGainDb(0.0f);
    eq_.setOutputGainDb(0.0f);
    eq_.setAutoGain(false);
    eq_.setMsMode(false);
    eq_.setSaturation(0.0f);
    eq_.setBypass(false);
    eq_.setHpfEnabled(false);
    eq_.setLpfEnabled(false);
    eq_.reset();
  }

  void reset() noexcept { eq_.reset(); }

  void setControls(const Controls& c) noexcept {
    eq_.setEqType(c.eqType);
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
  }

  void processBlock(float* left, float* right, int frames) noexcept {
    const float* in[2] = {left, right};
    float* out[2] = {left, right};
    eq_.processBlock(in, out, 2, frames);
  }

  int latencySamples() const noexcept { return eq_.getLatencySamples(); }

private:
  duskaudio::FourKEQDSP eq_;
};

} // namespace suite
