# Changelog

## 0.1.3

- Double-click resets a control to its default; right-click types a value in
  the unit shown; the wheel adjusts the knob under the cursor and scrolls the
  console elsewhere; Shift fine-tunes both.
- Input / colour group nudged down and kept inside its plate.

## 0.1.2

- Mute and solo now ride the channel's output fader instead of its bypass,
  restoring the saved fader position when released.  Mute state and the saved
  fader are stored in the plug-in state.
- Vertical GR meters fill from the top like a console ladder; the strip GR bar
  stays a left-to-right fill.
- Channel page mirrors Suite Channel's section layout, with explicit VCA/76 and
  Brown/Black buttons.
- Channel names persist per bus position in the plug-in state.

## 0.1.1

- The console page scrolls through every channel (8 per screen) with `<`/`>`
  and the mouse wheel; the header shows `SHOWING 1-8 OF N`.
- Strips are named by bus and order (`BUS A CH1`, `BUS D CH2`) instead of by
  their position on screen; a renamed channel keeps its name.
- MixHub-style inline views: MIX / INPUT / EQ / DYN / OUT switch what every
  strip shows, with the OUTPUT view using a vertical fader that matches the
  Suite Channel Plus output slider.
- `REN` renames a channel; Enter commits, Escape cancels.

## 0.1.0

- First build: folder-track mix bus (VCA compressor, 4K EQ, console summing,
  limiter) and an eight-strip remote surface for Suite Channel Plus.
- Shared-memory peer registry with bus discovery, capture and two-way parameter
  mirroring; remote moves reach the host automation lane.
