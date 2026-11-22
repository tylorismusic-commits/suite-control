# Changelog

## 0.1.5

- Fix controls snapping back and sticking: Suite Channel Plus used two
  publishers (audio + timer), so a stale publish could overwrite a newer remote
  write before it was adopted.  The timer thread is now the single writer and
  adopts remote changes before publishing, so edits apply without playback.
- Mute never captures a stale zero for its restore value.

## 0.1.4

- Fix the name table never loading: the loader required table version 1 while
  the saver wrote 2.  Saved sessions now recall channel names.
- Right-click a knob to type a value; right-click the same knob again closes it,
  another knob switches the editor, empty space cancels.  Left-click also
  cancels.  Keypad digits and keypad Enter are accepted.
- Strips end above the corner screw; mute/solo moved up to fit.

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
