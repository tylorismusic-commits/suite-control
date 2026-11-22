# Changelog

## 0.1.8

- LV2 UI is now driven by its own frame/event loop.  REAPER never calls the
  optional idle interface, so the window kept the single frame painted before
  the FX container was mapped and stayed black; it also ignored every click.
  The idle interface is still honoured, but only nudges the loop.
- CLAP and LV2 symbols are hidden except the entry points.  Hosts that
  `dlopen` with global scope were binding this plug-in's `Panel`, `Shared` and
  parameter table to Suite Channel Plus's copies, which made the BUS tabs read
  the wrong parameter (clicks reverted immediately) whenever both were loaded.
- Parameter echoes from the host are ignored briefly after a UI write, so a
  clicked value cannot be snapped back by a stale `port_event`.
- `Shared::set` only marks a parameter dirty when it actually changes; a host
  that echoes port events no longer causes a write/echo loop at frame rate.
- Bus tabs label index 0 consistently, and the UI host test now covers the
  "no idle + parent mapped late" path REAPER uses.

## 0.1.7

- LV2 UI opens again: the cairo surface now matches the host window's visual
  and depth (ARGB hosts stayed black), and the first frame is painted at open.
- Peer layer is Linux-only; other platforms compile a no-op so the macOS builds
  are unaffected.
- New LV2 UI host test grabs the window and fails on an all-black frame.

## 0.1.6

- LV2 build: `suite-control.lv2` with the same engine, UI and remote surface
  as the CLAP plug-in.  Channel names and mute states are not persisted in
  LV2 (session only); parameters are saved through the control ports.

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
