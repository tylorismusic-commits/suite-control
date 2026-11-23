# Suite roadmap

Suite Control and Suite Channel Plus ship as **one distribution**: a single
`suite.lv2` bundle plus `SuiteControl.clap` and `SuiteChannelPlus.clap`
installed together by `install.sh`.

Every phase is gated on REAPER **and** Ardour (CLAP and LV2 respectively). A
phase is not done until both hosts pass its acceptance checks.

## Phase 1 — One bundle (done)

- Both plug-ins live in this repository; `channel-plus/` keeps its own history.
- Shared third-party tree (`third_party/`), one version (`VERSION`).
- `install.sh` / `uninstall.sh`; the installer removes the pre-bundle
  `suite-control.lv2` and `suite-channel-plus.lv2` so no plug-in URI is
  discovered twice.
- Tests: `bundle_layout` walks the composed bundle, and both plug-in and both
  UI host tests run against `build/lv2/suite.lv2`.

Acceptance: one `~/.lv2/suite.lv2` announces both plug-ins; both UIs open in
REAPER; lilv discovery (Ardour's loader) resolves both.

## Phase 2 — LV2 parity with CLAP (done)

- `LV2_State_Interface` for Suite Control: channel names, mute and solo.
- `LV2_State_Interface` for Suite Channel Plus: parameter values, so remote
  edits survive save/load even with the channel window closed.
- GTK/suil embedding test locally; Ardour 9.8 load/open/save test.
- Round-trip tests for both state blobs.

Acceptance met: names/mute/fader persist in REAPER 7.84 and Ardour 9.8, with
the Ardour UI embedded via suil; the channel's LV2 state restores parameter
values and ignores the host's stale control ports on load.

## Phase 3 — Inserts, protocol v3, Comp Suite pilot (done)

- Versioned peer protocol: stable instance id, parameter metadata, hidden
  LINK parameter, bypass/power index.
- Comp Suite adapter (CLAP + LV2), piloting the path.
- Suite Control OUT view: four insert cells, `●` / `○` bypass, `+ ADD` picker,
  `CHANNEL | COMP | SPACE | EQ` tabs rendered from published metadata.

Acceptance met in REAPER 7.84 (CLAP) and Ardour 9.8 (LV2 registration):
picker assignment, `●`/`○` bypass, metadata-driven Comp page, remote parameter
edits, links restored from the plug-in's saved state after a reload.  Four
slots per channel; Rack Suite stays deferred.

## Phase 4 — Space Suite and Common Sense EQ inserts (done)

- Readouts are already exact: each insert publishes its own formatted value
  text (protocol v4), so a new adapter only needs metadata + a formatter.

- Adapters for the remaining trio; metadata-driven tabs only, no per-plug-in UI
  code in Suite Control. Rack Suite stays deferred.
- Verified in REAPER: three insert cells, tabs, bypass, scrolling EQ page,
  model/label-accurate readouts and remote edits.
- Insert pages also honour plug-in short names and an optional response curve
  (the EQ draws its curve above the controls).

## Phase 5 — macOS

- Cocoa editor for Suite Control (Channel Plus already has one).
- clap-wrapper targets (AUv2 for Logic, CLAP for REAPER mac, VST3 optional).
- Portable peer transport (`shm_open` + registry) so the remote surface and
  inserts work off Linux.

Acceptance: REAPER mac and Logic show the remote surface and inserts; `auval`
passes.

## Phase 6 — Hardening and 1.0

- `lv2lint` / `sord_validate`, ASan/valgrind, multi-instance stress, docs,
  changelog, tag 1.0.
