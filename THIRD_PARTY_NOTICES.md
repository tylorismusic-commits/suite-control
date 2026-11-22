# Third-party notices

Suite Channel is GPL-3.0-or-later. It contains or derives from the following
work.

## Dusk Audio — 4K EQ 2 DSP core

`third_party/dusk-4k/` contains the framework-free 4K EQ 2 DSP core from
[dusk-audio/dusk-audio-plugins](https://github.com/dusk-audio/dusk-audio-plugins),
pinned at commit `51537da6219f9d3f344b012da0cdc1ff19820a42`.

```
Copyright (C) 2026 Dusk Audio
GNU General Public License v3.0 or later
```

The files are used under GPL-3.0. See `third_party/dusk-4k/LICENSE` and
`third_party/dusk-4k/PROVENANCE.txt`. Changes in Suite Channel: the core is
wrapped by `src/dsp/eq4k.hpp`; its input/output gain, auto-gain, M/S and bypass
are held neutral, and its internal oversampling is driven by the plugin's OS
control. The per-sample spectrum-ring pushes were removed because Suite Channel
has no spectrum display; the audio/filter/calibration math is unchanged.

## hearth-dsp

`third_party/hearth-dsp/` is the shared header-only DSP library from
[tylorismusic-commits/hearth-dsp](https://github.com/tylorismusic-commits/hearth-dsp),
GPL-3.0-or-later. It supplies ADAA shaping, biquads, envelopes, smoothing and
the ratio ladder. See `third_party/hearth-dsp/THIRD_PARTY_NOTICES.md`.

## CLAP

`third_party/clap/` contains the CLAP headers, MIT licensed.

## Jost

`assets/fonts/Jost-ExtraBold.ttf` is Jost, licensed under the SIL Open Font
License 1.1. The licence text ships beside it as `assets/fonts/OFL.txt`.

## Behavioural references only

- **Tukan SumChannel S2**: studied for SSL-style gate/expander behaviour and
  module grouping. No Tukan code is copied; the repo has no explicit licence.
- **Airwindows**: MIT. No Airwindows code is included in 0.1.0.
- **Waves SSL 4000 E Channel Strip / SSL 4000 E**: the downloadable product
  image is a private layout study only. No Waves/SSL artwork, logos, knob
  bitmaps or trade dress are included in the plugin or repository.
