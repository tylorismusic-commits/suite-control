# Changelog

## Unreleased

- Forked from Suite Channel 0.3.0 as **Suite Channel Plus**: identical DSP and
  presets, plus a `Control Bus` parameter, a BUS selector in the header, and a
  shared-memory peer registry for Suite Control.
- States and presets saved by Suite Channel load unchanged; the appended BUS
  parameter defaults to Off.


## 0.3.0 — 2026-11-19

- User preset bank: save, rename, duplicate, delete, export and drag-drop
  import, alongside the factory presets.
- Preset files live in `~/.config/SuiteChannel/presets` and export to
  `~/Documents/Suite Channel Presets`.

## 0.2.5 — 2026-11-16

- Sharper rendering at reduced UI scales:
  - full FreeType hinting with integer hint metrics for the Jost face,
  - text baselines and origins snapped to physical pixels,
  - nearest-neighbour present from the backing surface to the X11 window.
- Applies to both CLAP and LV2 editors and to the 0.9x preview renders.

## 0.2.4 — 2026-11-16

- Input and Colour knobs moved down and their name labels lifted, tightening
  the group under the section header.
- Filters knobs centred in the left column; Input/Colour, compressor and gate
  knob rows nudged onto the panel centre line.
- Phase and EQ IN buttons moved fully above their section rules.
- EQ Freq and Q knobs use a wider scale radius so their scale labels clear the
  knob bodies; the Q knobs moved right and down to stop colliding with the
  Freq scale labels.
- Knob name labels pulled closer to their scale labels across the compressor,
  gate and EQ rows.

## 0.2.3 — 2026-11-16

- Gate GR meter now works: the CLAP editor publishes gate gain reduction, and
  LV2 has a new **Gate Gain Reduction** meter port.
- Phase button moved into the Input/Colour header row, made compact and
  aligned with the title.
- EQ IN sits in the Equaliser header row, aligned with the title.
- Removed the Input knob's top "0" scale label, which collided with the section
  title and looked like a stray phase symbol.
- Knob name labels share one baseline per row (input/colour, compressor, gate,
  HF/HMF/LMF/LF gain+freq), so labels no longer sit at different heights.
- Wordmark centred between PASTE and OS 4X.
- Compressor buttons nudged down for a little more space under their labels.

## 0.2.2 — 2026-11-16

- Fixed the gate staying shut with Range at maximum: a host that provides a
  silent buffer for the unpatched optional sidechain port no longer keys the
  gate. The sidechain is ignored until it carries signal, then latched as the
  key.
- COPY and PASTE stay grey and flash green for ~220 ms when they succeed.
- COPY/PASTE buttons are slightly smaller; the oversampling control is sized
  to fit "OS OFF" instead of a fixed wide pill, easing header crowding.

## 0.2.1 — 2026-11-16

- Fixed typed-value entry: the field stays anchored where it opened instead of
  following the pointer, clicking inside it keeps editing, and the plugin
  window takes X keyboard focus on click so typed characters reach the field.
- **PASTE** button added beside COPY: COPY captures the live settings and
  PASTE applies them. COPY no longer mirrors A into B by itself.
- Gate button rows (F.ATK / GATE / EXP / DUCK and GATE IN / HOLD) are centred
  in the left column.
- Gate subheader now reports **DUCKER** when Ducker mode is selected; it was
  previously clamped to Gate. The section title follows the mode.
- Gate Range/Release printed scales trimmed to three labels so "30" and "0.3"
  no longer render as "300.3".
- The editor and CLAP parameter strings now share one value formatter/parser,
  so every option reports the same labels in both places.

## 0.2.0 — 2026-11-16

- Gate **HOLD**: 0 / 50 / 100 / 200 / 400 / 500 ms, shown as a compact pill
  that cycles on click and accepts typed values.
- Gate **DUCK** mode: attenuates material above the threshold, with the same
  range, release and fast-attack controls. A patched external sidechain keys
  gate and ducker detection.
- Gate detector now uses a fast peak envelope, fixing zero-crossing chatter and
  making ducker gain reduction stable.
- Output **LIMIT** pill switches the −0.2 dBFS safety ceiling on or off.
- Typed value entry: right-click any knob, fader or the HOLD pill; Enter
  applies, Esc cancels, Backspace edits. Accepts units and `on`/`off`.
- Double-click a knob or fader to restore its default.
- Dragging a control shows an exact value bubble.
- **A / B / COPY** compare controls in the header (editor-local, not saved in
  host state).
- CLAP and LV2 editors route key events for text entry.
- CLAP state loading accepts states saved with the original 36 parameters.
- Parameter count is now 38; LV2 meter port indices follow automatically.

## 0.1.6 — 2026-11-16

- Output fader unity moved to **3/4 travel**. Top is **+12 dB**, bottom is
  **OFF**, and the lower range uses the cubic gain taper.
- Fader marks updated to +12 / +6 / 0 / −12 / −24 / OFF.
- Factory preset output trims remapped to preserve their previous dB values;
  the default output is now 0.75 (0 dB).
- Note: a saved session with the old 0.5 output position now plays about
  10.5 dB quieter. This is the intentional fader-law change.

## 0.1.5 — 2026-11-16

- Output fader is now **Off at the bottom**, **0 dB at centre** and **+12 dB at
  the top**. The lower half uses a cubic gain taper to reach true silence.
- Fader marks updated to +12 / +6 / 0 / −12 / −24 / OFF.
- The 27×53 Reapertips-style thumb is inset at both ends so it stays inside the
  track at full boost and full attenuation.
- Existing presets around 0.5 remain at 0 dB; no preset retune needed.

## 0.1.4 — 2026-11-16

- Output fader thumb resized to the Reapertips MCP volume-thumb proportions
  (**27×53 logical px**) with a portrait light ribbed grip.
- Gate subheader/control-row cleanup from 0.1.3 retained.

## 0.1.3 — 2026-11-16

- Gate subheader now matches the compressor subheader: title, underline rule
  and mode label (GATE / EXPANDER).
- Gate header moved down clear of the compressor block, including the silver
  76Blue panel.
- Gate controls rearranged into a single threshold/range/release row, removing
  the label collision at the bottom of the dynamics section.

## 0.1.2 — 2026-11-16

- Corrected the input meter tap: it now measures **after the input trim** and
  before the colour stage, instead of before the trim.
- Verified: −20.0 dB at 0 dB trim and −2.0 dB at +18 dB trim for a −20 dBFS
  constant input.

## 0.1.1 — 2026-11-16

- Added a stacked stereo **input level meter** above the output meter, with
  the same LED ladder and dB marks.
- Input metering is pre-trim; output remains post-fader/clip.
- Input and output meters both receive LV2 UI port notifications.

## 0.1.0 — 2026-11-15

First working vertical channel strip.

- New `suite-channel` CLAP + LV2 plugin with the Comp Suite visual language.
- 700×920 three-column panel adapted from the SSL 4000 E layout reference.
- Opens at 1.0× on a 1080p display and scales down on smaller screens.
- Console-coloured EQ fills: HF red, HMF green, LMF blue, LF amber; white
  filter knobs and green gate knobs. Black EQ mode flips the LF fill to black
  and the LF band accent to steel grey, reversibly and in sync with the DSP.
- Brushed-silver compressor panel when 76Blue is engaged.
- LED-ladder GR/output meters and a Reapertips-inspired long output fader.
- Removed the unused lower panel area; the strip now ends with the fader.
- Bug pass: corrected EQ oversampling latency at activation (0 / 23 / 27
  samples for 1x / 2x / 4x); the LV2 UI now requests control-port
  notifications so host presets and automation redraw the panel.
- Regression tests now cover Brown/Black LF colour and DSP difference,
  reversible silver 76Blue panel, every factory preset, expander mode, power
  bypass, all oversampling modes, and VCA↔76 switching. 
- Patina colour section: Valve, Console, Bus, Tape, Transformer, one Amount
  control with auto-gain.
- SSL-style expander/gate with variable hysteresis, Range, Release, Fast Attack
  and Gate/Expander modes.
- Switchable compressor: fixed 3.5:1 VCA and fixed 4:1 76Blue, same control
  surface, 20 ms type crossfade.
- Vendored Dusk 4K EQ 2 core: four bands, Brown/Black, shelf/bell, Q, HPF/LPF,
  native console colour, internal 1x/2x/4x oversampling.
- Constant-latency EQ IN crossfade.
- Vertical output fader, comp/gate GR meters and stereo output meter.
- 23 factory presets across Vocal, Drums, Bass, Guitars, Mix Bus and Utility.
- LV2 metadata generated from `params.hpp` / `presets.hpp`; `--check` test.
- Minimal CLAP and LV2 host smoke tests.
