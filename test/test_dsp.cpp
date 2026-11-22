// Suite Channel — DSP smoke and static-curve tests
// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "dsp/engine.hpp"
#include "params.hpp"

namespace {

constexpr float kPi = 3.14159265f;

bool finite(const std::vector<float>& v) {
  for (float x : v) {
    if (!std::isfinite(x)) {
      return false;
    }
  }
  return true;
}

void fillSine(std::vector<float>& l, std::vector<float>& r, float dbfs,
              float hz, int start) {
  const float amp = std::pow(10.0f, dbfs / 20.0f);
  for (size_t i = 0; i < l.size(); ++i) {
    const float s = amp * std::sin(2.0f * kPi * hz * (start + i) / 48000.0f);
    l[i] = s;
    r[i] = s;
  }
}

void fillConst(std::vector<float>& l, std::vector<float>& r, float dbfs) {
  const float amp = std::pow(10.0f, dbfs / 20.0f);
  for (size_t i = 0; i < l.size(); ++i) {
    l[i] = amp;
    r[i] = amp;
  }
}

} // namespace

int main() {
  // Fader law: unity at centre, +12 dB at the top, silence at the bottom.
  assert(std::fabs(suite::outputGainLinear(0.75f) - 1.0f) < 1.0e-5f);
  assert(std::fabs(suite::outputGainDb(0.75f)) < 1.0e-4f);
  assert(std::fabs(suite::outputGainDb(1.0f) - 12.0f) < 0.01f);
  assert(suite::outputGainLinear(0.0f) == 0.0f);

  // Every option name must round-trip between the editor and the host text.
  {
    assert(suite::formatParamValue(suite::P_GATE_MODE, 0.0) == "Expander");
    assert(suite::formatParamValue(suite::P_GATE_MODE, 1.0) == "Gate");
    assert(suite::formatParamValue(suite::P_GATE_MODE, 2.0) == "Ducker");
    assert(suite::formatParamValue(suite::P_COMP_TYPE, 1.0) == "76Blue");
    assert(suite::formatParamValue(suite::P_OVERSAMPLE, 0.0) == "Off");
    assert(suite::formatParamValue(suite::P_HPF, 0.0) == "OUT");
    assert(suite::formatParamValue(suite::P_GATE_HOLD, 0.1) == "50 ms");
    double v = 0.0;
    assert(suite::parseParamValue(suite::P_GATE_MODE, "Ducker", &v));
    assert(v == 2.0);
    assert(suite::parseParamValue(suite::P_GATE_HOLD, "250 ms", &v));
    assert(std::fabs(v - 0.5) < 1.0e-9);
  }

  suite::Shared shared;
  suite::Engine engine;
  suite::Meters meters;
  engine.prepare(48000.0f, 2.0f);
  engine.setControls(suite::controlsFrom(shared));

  std::vector<float> l(512);
  std::vector<float> r(512);

  // --- VCA static curve: 3.5:1 at threshold -12 dB -------------------------
  shared.set(suite::P_EQ_IN, 0.0);     // keep the EQ out of the measurement
  shared.set(suite::P_COLOR_AMOUNT, 0.0);
  shared.set(suite::P_COMP_IN, 1.0);
  shared.set(suite::P_COMP_AUTO, 0.0);
  shared.set(suite::P_COMP_MAKEUP, 0.5);
  shared.set(suite::P_COMP_THRESHOLD, (10.0 + 12.0) / 30.0);  // -12 dB
  engine.setControls(suite::controlsFrom(shared));
  engine.prepare(48000.0f, 2.0f);

  for (int block = 0; block < 100; ++block) {
    fillConst(l, r, -6.0f);
    engine.process(l.data(), r.data(), 512, meters);
    assert(finite(l));
    assert(finite(r));
  }
  // 6 dB over threshold at 3.5:1 = 6 * (1 - 1/3.5) = 4.29 dB.
  const float expected = 6.0f * (1.0f - 1.0f / 3.5f);
  std::printf("VCA GR at -6 dBFS / -12 dB thr: %.2f dB (expected %.2f)\n",
              meters.grDb, expected);
  assert(std::fabs(meters.grDb - expected) < 0.8f);

  // --- 76Blue stays finite and compresses ----------------------------------
  shared.set(suite::P_COMP_TYPE, 1.0);
  engine.setControls(suite::controlsFrom(shared));
  for (int block = 0; block < 20; ++block) {
    fillSine(l, r, -6.0f, 1000.0f, block * 512);
    engine.process(l.data(), r.data(), 512, meters);
    assert(finite(l));
    assert(finite(r));
  }
  std::printf("76Blue GR at -6 dBFS: %.2f dB\n", meters.grDb);
  assert(meters.grDb > 0.5f);

  // ... and back to VCA.
  shared.set(suite::P_COMP_TYPE, 0.0);
  engine.setControls(suite::controlsFrom(shared));
  for (int block = 0; block < 20; ++block) {
    fillSine(l, r, -6.0f, 1000.0f, block * 512);
    engine.process(l.data(), r.data(), 512, meters);
    assert(finite(l));
    assert(finite(r));
  }
  assert(meters.grDb > 0.5f);

  // --- Gate: closed on a quiet signal, open on a loud one ------------------
  shared.set(suite::P_COMP_IN, 0.0);
  shared.set(suite::P_GATE_IN, 1.0);
  shared.set(suite::P_GATE_MODE, 1.0);
  shared.set(suite::P_GATE_THRESHOLD, (-20.0 + 30.0) / 40.0);  // -20 dB
  shared.set(suite::P_GATE_RANGE, 1.0);                        // 40 dB
  shared.set(suite::P_GATE_RELEASE, 0.3);
  engine.setControls(suite::controlsFrom(shared));

  for (int block = 0; block < 100; ++block) {
    fillSine(l, r, -45.0f, 1000.0f, block * 512);
    engine.process(l.data(), r.data(), 512, meters);
  }
  std::printf("gate GR on -45 dBFS: %.2f dB\n", meters.gateGrDb);
  assert(meters.gateGrDb > 30.0f);

  for (int block = 0; block < 20; ++block) {
    fillSine(l, r, -6.0f, 1000.0f, block * 512);
    engine.process(l.data(), r.data(), 512, meters);
  }
  std::printf("gate GR on -6 dBFS: %.2f dB\n", meters.gateGrDb);
  assert(meters.gateGrDb < 1.0f);

  // Expander mode should settle into a fixed 1:2 curve, not the gate floor.
  shared.set(suite::P_GATE_MODE, 0.0);
  shared.set(suite::P_GATE_RELEASE, 0.3);
  engine.setControls(suite::controlsFrom(shared));
  for (int block = 0; block < 60; ++block) {
    fillSine(l, r, -30.0f, 1000.0f, block * 512);
    engine.process(l.data(), r.data(), 512, meters);
  }
  std::printf("expander GR on -30 dBFS: %.2f dB\n", meters.gateGrDb);
  assert(meters.gateGrDb > 2.0f && meters.gateGrDb < 20.0f);

  // --- Gate hold keeps the gate open after the signal falls ----------------
  {
    suite::Shared holdShared;
    suite::Engine holdEngine;
    suite::Meters holdMeters;
    holdShared.set(suite::P_COMP_IN, 0.0);
    holdShared.set(suite::P_GATE_IN, 1.0);
    holdShared.set(suite::P_GATE_MODE, 1.0);
    holdShared.set(suite::P_GATE_THRESHOLD, (-20.0 + 30.0) / 40.0);
    holdShared.set(suite::P_GATE_RANGE, 1.0);
    holdShared.set(suite::P_GATE_RELEASE, 0.0);  // 100 ms
    holdShared.set(suite::P_GATE_HOLD, 0.5);     // 250 ms
    holdEngine.prepare(48000.0f, 0.0f);
    holdEngine.setControls(suite::controlsFrom(holdShared));
    std::vector<float> hl(512), hr(512);

    for (int block = 0; block < 20; ++block) {
      fillSine(hl, hr, -6.0f, 1000.0f, block * 512);
      holdEngine.process(hl.data(), hr.data(), 512, holdMeters);
    }
    assert(holdMeters.gateGrDb < 1.0f);

    // 4000 samples ≈ 83 ms of silence: well inside the 250 ms hold.
    for (int block = 0; block < 7; ++block) {
      fillSine(hl, hr, -45.0f, 1000.0f, block * 512);
      holdEngine.process(hl.data(), hr.data(), 512, holdMeters);
    }
    std::printf("gate GR after 83 ms of silence with 250 ms hold: %.2f dB\n",
                holdMeters.gateGrDb);
    assert(holdMeters.gateGrDb < 6.0f);

    // Once the hold expires the gate closes.
    for (int block = 0; block < 60; ++block) {
      fillSine(hl, hr, -45.0f, 1000.0f, block * 512);
      holdEngine.process(hl.data(), hr.data(), 512, holdMeters);
    }
    std::printf("gate GR after the hold expires: %.2f dB\n",
                holdMeters.gateGrDb);
    assert(holdMeters.gateGrDb > 30.0f);
  }

  // --- Ducker attenuates loud material above the threshold ----------------
  {
    suite::Shared duckShared;
    suite::Engine duckEngine;
    suite::Meters duckMeters;
    duckShared.set(suite::P_COMP_IN, 0.0);
    duckShared.set(suite::P_GATE_IN, 1.0);
    duckShared.set(suite::P_GATE_MODE, 2.0);
    duckShared.set(suite::P_GATE_THRESHOLD, (-20.0 + 30.0) / 40.0);
    duckShared.set(suite::P_GATE_RANGE, 1.0);
    duckShared.set(suite::P_GATE_RELEASE, 0.0);
    duckEngine.prepare(48000.0f, 0.0f);
    duckEngine.setControls(suite::controlsFrom(duckShared));
    std::vector<float> dl(512), dr(512);

    for (int block = 0; block < 60; ++block) {
      fillSine(dl, dr, -45.0f, 1000.0f, block * 512);
      duckEngine.process(dl.data(), dr.data(), 512, duckMeters);
    }
    std::printf("ducker GR on -45 dBFS: %.2f dB\n", duckMeters.gateGrDb);
    assert(duckMeters.gateGrDb < 1.0f);

    for (int block = 0; block < 60; ++block) {
      fillSine(dl, dr, -6.0f, 1000.0f, block * 512);
      duckEngine.process(dl.data(), dr.data(), 512, duckMeters);
    }
    std::printf("ducker GR on -6 dBFS: %.2f dB\n", duckMeters.gateGrDb);
    assert(duckMeters.gateGrDb > 30.0f);
  }

  // --- A silent unpatched sidechain must not lock the gate closed ---------
  {
    suite::Shared scShared;
    suite::Engine scEngine;
    suite::Meters scMeters;
    scShared.set(suite::P_COMP_IN, 0.0);
    scShared.set(suite::P_GATE_IN, 1.0);
    scShared.set(suite::P_GATE_MODE, 1.0);
    scShared.set(suite::P_GATE_THRESHOLD, (-20.0 + 30.0) / 40.0);
    scShared.set(suite::P_GATE_RANGE, 1.0);
    scShared.set(suite::P_GATE_RELEASE, 0.0);
    scEngine.prepare(48000.0f, 0.0f);
    scEngine.setControls(suite::controlsFrom(scShared));
    std::vector<float> ml(512), mr(512), sl(512, 0.0f), sr(512, 0.0f);

    // Host offers a silent sidechain buffer but it was never patched: the
    // gate must still key from the main signal and open on a hit.
    for (int block = 0; block < 30; ++block) {
      fillSine(ml, mr, -6.0f, 1000.0f, block * 512);
      scEngine.process(ml.data(), mr.data(), 512, scMeters, sl.data(),
                       sr.data());
    }
    std::printf("gate GR with silent unpatched sidechain: %.2f dB\n",
                scMeters.gateGrDb);
    assert(scMeters.gateGrDb < 1.0f);

    // Once the sidechain carries signal it becomes the key: a silent key now
    // closes the gate even though the main signal is loud.
    fillSine(sl, sr, -6.0f, 2000.0f, 0);
    scEngine.process(ml.data(), mr.data(), 512, scMeters, sl.data(),
                     sr.data());
    std::fill(sl.begin(), sl.end(), 0.0f);
    std::fill(sr.begin(), sr.end(), 0.0f);
    for (int block = 0; block < 60; ++block) {
      fillSine(ml, mr, -6.0f, 1000.0f, block * 512);
      scEngine.process(ml.data(), mr.data(), 512, scMeters, sl.data(),
                       sr.data());
    }
    std::printf("gate GR keyed by silent sidechain: %.2f dB\n",
                scMeters.gateGrDb);
    assert(scMeters.gateGrDb > 30.0f);
  }

  // --- Output limiter is a real switch ------------------------------------
  {
    auto measure = [](bool limiterOn) {
      suite::Shared s;
      suite::Engine e;
      suite::Meters m;
      s.set(suite::P_COMP_IN, 0.0);
      s.set(suite::P_GATE_IN, 0.0);
      s.set(suite::P_EQ_IN, 0.0);
      s.set(suite::P_COLOR_AMOUNT, 0.0);
      s.set(suite::P_OUTPUT, 1.0);  // +12 dB
      s.set(suite::P_LIMITER_ON, limiterOn ? 1.0 : 0.0);
      e.prepare(48000.0f, 0.0f);
      e.setControls(suite::controlsFrom(s));
      std::vector<float> xl(512), xr(512);
      for (int block = 0; block < 50; ++block) {
        fillConst(xl, xr, -6.0f);
        e.process(xl.data(), xr.data(), 512, m);
      }
      return std::fabs(xl[511]);
    };
    const float limited = measure(true);
    const float unlimited = measure(false);
    std::printf("output peak: %.3f limited, %.3f unlimited\n", limited,
                unlimited);
    assert(limited < 0.978f);
    assert(unlimited > 1.5f);
  }

  // --- EQ IN is a constant-latency crossfade -------------------------------
  const int latencyBefore = engine.latencySamples();
  shared.set(suite::P_EQ_IN, 1.0);
  engine.setControls(suite::controlsFrom(shared));
  for (int block = 0; block < 8; ++block) {
    fillSine(l, r, -20.0f, 1000.0f, block * 512);
    engine.process(l.data(), r.data(), 512, meters);
    assert(finite(l));
    assert(finite(r));
  }
  assert(engine.latencySamples() == latencyBefore);

  // --- Colour amount 0 is dry; amount 1 stays finite -----------------------
  shared.set(suite::P_GATE_IN, 0.0);
  shared.set(suite::P_COLOR_AMOUNT, 0.0);
  engine.setControls(suite::controlsFrom(shared));
  for (int block = 0; block < 4; ++block) {
    fillSine(l, r, -20.0f, 1000.0f, block * 512);
    engine.process(l.data(), r.data(), 512, meters);
    assert(finite(l));
    assert(finite(r));
  }
  shared.set(suite::P_COLOR_AMOUNT, 1.0);
  for (int model = 0; model < 5; ++model) {
    shared.set(suite::P_COLOR_MODEL, static_cast<double>(model));
    engine.setControls(suite::controlsFrom(shared));
    for (int block = 0; block < 4; ++block) {
      fillSine(l, r, -20.0f, 1000.0f, block * 512);
      engine.process(l.data(), r.data(), 512, meters);
      assert(finite(l));
      assert(finite(r));
    }
  }

  // --- Oversampling modes load and stay finite -----------------------------
  for (float os : {0.0f, 1.0f, 2.0f}) {
    suite::Shared osShared;
    suite::Engine osEngine;
    suite::Meters osMeters;
    osShared.set(suite::P_OVERSAMPLE, os);
    osEngine.prepare(48000.0f, os);
    osEngine.setControls(suite::controlsFrom(osShared));
    std::vector<float> ol(512), orr(512);
    fillSine(ol, orr, -20.0f, 1000.0f, 0);
    osEngine.process(ol.data(), orr.data(), 512, osMeters);
    assert(finite(ol));
    assert(finite(orr));
    std::printf("OS %.0f latency: %d\n", os, osEngine.latencySamples());
  }

  // --- Power bypass settles to the exact dry input -------------------------
  {
    suite::Shared dryShared;
    suite::Engine dryEngine;
    suite::Meters dryMeters;
    dryEngine.prepare(48000.0f, 0.0f);
    dryEngine.setControls(suite::controlsFrom(dryShared));
    dryShared.set(suite::P_ENABLED, 0.0);
    dryEngine.setControls(suite::controlsFrom(dryShared));
    std::vector<float> dl(512), dr(512);
    for (int block = 0; block < 100; ++block) {
      fillConst(dl, dr, -12.0f);
      dryEngine.process(dl.data(), dr.data(), 512, dryMeters);
    }
    assert(std::fabs(dl[511] - 0.2511886f) < 0.01f);
  }

  // --- Input meter follows the Input knob, not the raw input --------------
  {
    suite::Shared inShared;
    suite::Engine inEngine;
    suite::Meters inMeters;
    inShared.set(suite::P_COMP_IN, 0.0);
    inShared.set(suite::P_GATE_IN, 0.0);
    inShared.set(suite::P_INPUT, 0.5);  // 0 dB
    inEngine.prepare(48000.0f, 0.0f);
    inEngine.setControls(suite::controlsFrom(inShared));
    std::vector<float> il(512), ir(512);
    for (int block = 0; block < 30; ++block) {
      fillConst(il, ir, -20.0f);
      inEngine.process(il.data(), ir.data(), 512, inMeters);
    }
    const float atUnity = inMeters.inPeakLDb;
    inShared.set(suite::P_INPUT, 1.0);  // +18 dB
    inEngine.setControls(suite::controlsFrom(inShared));
    for (int block = 0; block < 30; ++block) {
      fillConst(il, ir, -20.0f);
      inEngine.process(il.data(), ir.data(), 512, inMeters);
    }
    const float boosted = inMeters.inPeakLDb;
    std::printf("input meter: %.1f dB at 0 dB trim, %.1f dB at +18 dB\n",
                atUnity, boosted);
    assert(boosted - atUnity > 15.0f);
  }

  // --- Output fader reaches true off and +12 dB ----------------------------
  {
    suite::Shared outShared;
    suite::Engine outEngine;
    suite::Meters outMeters;
    outShared.set(suite::P_COMP_IN, 0.0);
    outShared.set(suite::P_GATE_IN, 0.0);
    outShared.set(suite::P_EQ_IN, 0.0);
    outShared.set(suite::P_COLOR_AMOUNT, 0.0);
    outShared.set(suite::P_OUTPUT, 0.0);
    outEngine.prepare(48000.0f, 0.0f);
    outEngine.setControls(suite::controlsFrom(outShared));
    std::vector<float> fl(512), fr(512);
    for (int block = 0; block < 50; ++block) {
      fillConst(fl, fr, -20.0f);
      outEngine.process(fl.data(), fr.data(), 512, outMeters);
    }
    assert(std::fabs(fl[511]) < 0.001f);

    outShared.set(suite::P_OUTPUT, 1.0);
    outEngine.setControls(suite::controlsFrom(outShared));
    for (int block = 0; block < 50; ++block) {
      fillConst(fl, fr, -20.0f);
      outEngine.process(fl.data(), fr.data(), 512, outMeters);
    }
    assert(std::fabs(fl[511] - 0.3981f) < 0.01f);
  }

  // --- Brown / Black must change the audio path, not just the UI ----------
  {
    suite::Controls brownControls;
    brownControls.eqIn = true;
    brownControls.lfGain = 0.9f;
    brownControls.lfFreq = 0.36f;
    brownControls.lfBell = true;
    brownControls.colourAmount = 0.0f;
    brownControls.eqType = 0;

    suite::Eq4k brown;
    suite::Eq4k black;
    brown.prepare(48000.0f, 512, 2);
    black.prepare(48000.0f, 512, 2);
    brown.setControls(brownControls);
    suite::Controls blackControls = brownControls;
    blackControls.eqType = 1;
    black.setControls(blackControls);

    std::vector<float> bl(4096), br(4096), kl(4096), kr(4096);
    fillSine(bl, br, -20.0f, 100.0f, 0);
    fillSine(kl, kr, -20.0f, 100.0f, 0);
    brown.processBlock(bl.data(), br.data(), 4096);
    black.processBlock(kl.data(), kr.data(), 4096);

    double diff = 0.0;
    for (int i = 2048; i < 4096; ++i) {
      diff += std::fabs(bl[i] - kl[i]);
    }
    diff /= 2048.0;
    std::printf("Brown/Black LF average difference: %.6f\n", diff);
    assert(diff > 1.0e-5);
  }

  assert(engine.latencySamples() >= 0);
  std::printf("suite_dsp: ok (latency %d samples)\n", engine.latencySamples());
  return 0;
}
