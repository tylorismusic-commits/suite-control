// Suite Control — DSP test
// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/engine.hpp"
#include "params.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL  %s\n", what);
    ++failures;
  } else {
    std::printf("PASS  %s\n", what);
  }
}

std::vector<float> sine(int n, double hz, double amp) {
  std::vector<float> v(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    v[static_cast<size_t>(i)] =
        static_cast<float>(amp * std::sin(2.0 * 3.141592653589793 * hz * i /
                                          48000.0));
  }
  return v;
}

struct Fixed { std::vector<float> l, r; };

Fixed run(const suite::Controls& c, const std::vector<float>& input,
          int frames = 2048) {
  suite::Engine e;
  e.prepare(48000.0f, frames);
  e.setControls(c);
  Fixed out{input, input};
  suite::Meters m;
  for (size_t i = 0; i + frames <= input.size(); i += frames) {
    e.process(out.l.data() + i, out.r.data() + i, frames, m);
  }
  return out;
}

double rms(const std::vector<float>& v, size_t from, size_t to) {
  double e = 0.0;
  for (size_t i = from; i < to; ++i) e += static_cast<double>(v[i]) * v[i];
  return std::sqrt(e / (to - from));
}

double mean(const std::vector<float>& v, size_t from, size_t to) {
  double s = 0.0;
  for (size_t i = from; i < to; ++i) s += v[i];
  return s / (to - from);
}
} // namespace

int main() {
  const int n = 48000;
  const auto tone = sine(n, 120.0, 0.35);

  // Compressor: heavy threshold, 10:1 should pull the level down.
  {
    suite::Controls c;
    c.compIn = true;
    c.compThreshold = 0.95f;  // about -18 dB
    c.compRatio = 2;         // 10:1
    c.compAttack = 0.0f;     // fastest
    c.compAuto = false;
    c.compRelease = 0.0f;
    c.eqIn = false;
    c.sumAmount = 0.0f;
    c.input = 0.5f;
    c.output = 0.5f;
    const Fixed out = run(c, tone);
    const double inRms = rms(tone, 24000, n);
    const double outRms = rms(out.l, 24000, n);
    std::printf("comp: %.3f -> %.3f\n", inRms, outRms);
    check(outRms < inRms * 0.9, "compressor reduces gain");
  }

  // Summing: amount 0 is bit-exact.
  {
    suite::Controls c;
    c.compIn = false;
    c.eqIn = false;
    c.sumAmount = 0.0f;
    c.limiter = false;
    const Fixed out = run(c, tone);
    bool same = true;
    for (int i = 0; i < n; ++i) {
      if (out.l[static_cast<size_t>(i)] != tone[static_cast<size_t>(i)]) {
        same = false;
        break;
      }
    }
    check(same, "summing at 0 is bit-exact dry");
  }

  // Summing: amount 1 changes the signal but keeps it finite and centred.
  {
    suite::Controls c;
    c.compIn = false;
    c.eqIn = false;
    c.sumAmount = 1.0f;
    c.limiter = false;
    const Fixed out = run(c, tone);
    bool finite = true;
    double diff = 0.0;
    for (int i = 0; i < n; ++i) {
      if (!std::isfinite(out.l[static_cast<size_t>(i)])) finite = false;
      diff += std::fabs(out.l[static_cast<size_t>(i)] - tone[static_cast<size_t>(i)]);
    }
    std::printf("summing diff %.4f dc %.4f\n", diff / n,
                mean(out.l, 12000, n));
    check(finite, "summing stays finite");
    check(diff / n > 1e-4, "summing colours the signal");
    check(std::fabs(mean(out.l, 12000, n)) < 0.01, "summing adds no DC");
    check(rms(out.l, 24000, n) < 0.6, "summing is level-neutral");
  }

  // EQ: full LF boost lifts 60 Hz.
  {
    suite::Controls c;
    c.compIn = false;
    c.sumAmount = 0.0f;
    c.eqIn = true;
    c.lfGain = 1.0f;
    c.lfFreq = 0.0f;
    const auto low = sine(n, 60.0, 0.2);
    const Fixed with = run(c, low);
    c.eqIn = false;
    const Fixed without = run(c, low);
    const double a = rms(with.l, 24000, n);
    const double b = rms(without.l, 24000, n);
    std::printf("eq 60Hz: %.3f vs %.3f\n", a, b);
    check(a > b * 1.2, "EQ low boost raises 60 Hz");
  }

  // Limiter: a hot signal is caught.
  {
    suite::Controls c;
    c.compIn = false;
    c.eqIn = false;
    c.sumAmount = 0.0f;
    c.input = 1.0f;  // +18 dB
    c.limiter = true;
    const auto hot = sine(n, 100.0, 0.4);
    const Fixed out = run(c, hot);
    double peak = 0.0;
    for (float v : out.l) peak = std::max(peak, static_cast<double>(std::fabs(v)));
    std::printf("limiter peak %.3f\n", peak);
    check(peak <= 1.01, "limiter holds the ceiling");
  }

  // Latency reporting.
  {
    suite::Engine e;
    e.prepare(48000.0f);
    std::printf("latency %d\n", e.latencySamples());
    check(e.latencySamples() >= 0 && e.latencySamples() <= 64,
          "reports a small latency");
  }

  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
