# Changelog

## 0.9.0

- Insert pages show gain reduction when the insert publishes it; Comp Suite's
  page has a live `GR` bar with the reduction in dB.
- The CLAP editor can be resized, keeping the 1280:760 aspect (0.5x–2.5x).
  It still opens at the fit-to-screen size, and the host's size is honoured
  through `set_size`; the LV2 UI already advertised `ui:resize`.
- Peer protocol v6 also carries a gain-reduction flag.

## 0.8.0

- Insert short names are published by the plug-ins (`COMP`, `SPAC`, `CSEQ`)
  instead of being derived from the long name, so Common Sense EQ shows
  `CSEQ` in its cell and tab.
- Inserts can publish a response curve.  The EQ page draws it above the
  controls — grid, filled response, live update while dragging — and the
  parameter grid scrolls beneath it.  Comp and Space pages are unchanged.
- Protocol v6: slot short name and up to 96 curve points.

## 0.7.0 — Inserts, all three plug-ins

- Space Suite and Common Sense EQ join Comp Suite as inserts.  Assigning one
  adds its tab (`CHANNEL | COMP | SPACE | EQ`) and its page, all generated
  from published metadata.
- Insert pages scroll with the wheel and are clipped below the tab bar, so the
  EQ's 101 parameters fit without a separate layout.
- Enum pills use the plug-in's value for each label instead of the label
  index, so EQ slopes (6/12/18/24/30 dB/Oct) and processing modes land on the
  real values.
- Protocol v5: up to 128 parameters per insert and per-label values.

## 0.6.1

- Insert pages show the plug-in's own formatted readouts instead of raw steps:
  `6.0:1` for a ratio, `-20.0 dB`, `253 us`, `60 %`, `On`, `76Blue`.  The
  insert publishes the string with each value (protocol v4, seqlock-protected),
  so model-dependent mappings (ratio/input/threshold across the Comp models)
  always match what the plug-in is doing.
- The `INSERTS` caption is centred in the gap between the strip name and the
  insert cells instead of colliding with the name.

## 0.6.0 — Inserts (Linux pilot)

- Peer protocol v3: stable instance ids, published parameter metadata, slot
  kind (strip/insert), bypass index and a LINK assignment that each insert
  plug-in saves in its own host state.
- Inserts live on the OUT strip: four cells per channel, `●` engaged /
  `○` bypassed.  Clicking a linked cell toggles its power/bypass; an empty
  cell opens the one-click picker of unassigned inserts (own host process
  first).
- Channel page tabs are `CHANNEL | <inserts>`; insert pages are generated
  entirely from the published metadata (groups, knobs, toggles, enum pills),
  with no per-plug-in UI code.  Dragging shows the requested value immediately
  instead of waiting for the peer publish.
- Remote values are now split into an owner-published value and a consumer
  request, so a plug-in's own publish can no longer overwrite a pending edit
  (this also removed the timer race that made strip controls feel jumpy).
- Comp Suite is the pilot insert; Space Suite and Common Sense EQ follow.

## 0.5.0 — Suite bundle

- First release of Suite Control and Suite Channel Plus as one distribution:
  a single `suite.lv2` with both plug-ins, plus the two CLAP bundles.
- LV2 state: channel names, mute and the saved fader position now persist in
  the host's session.  LV2 state lives in the plug-in, but the console table is
  edited in the UI, so both sides meet in a small per-process segment
  (`src/console_state.hpp`); it is Linux-only, like the peer bus.
- Verified end to end in REAPER 7.84 and Ardour 9.8: names survive save/reload
  and the Ardour-embedded UI (suil X11-in-GTK2) comes back with them.

## 0.1.9

- Remote edits on the strip surface reach the channel plug-ins again (they were
  reverted by the channel's control-port poll, so knobs, mute and solo looked
  dead).  Verified in REAPER 7.84: strip input, output fader, mute and solo now
  change the channel's shared state and stay there.

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
