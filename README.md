# Suite Control

**Suite 0.10.0** · GPL-3.0-or-later · CLAP + LV2 · Linux

Suite Control lives on a folder or group track. It does two jobs:

1. **Mix bus.** An SSL-style VCA bus compressor, the Dusk 4K bus EQ, console
   summing colour (transformer, crosstalk, noise, soft clip) and a limiter,
   applied to the summed folder audio.
2. **Remote surface.** Eight strips mirror the **Suite Channel Plus** instances
   assigned to the selected bucket (A-H), with Input, Comp threshold/makeup,
   Comp In, EQ In and Output mirrored both ways. `CAPTURE` assigns every
   unassigned instance to the current bucket.

The host already sums the folder; Suite Control only shapes that bus and never
touches per-channel audio.

## The Suite bundle

Suite Control and **Suite Channel Plus** (`channel-plus/`) ship together as one
distribution:

```
./install.sh          # ~/.lv2/suite.lv2 + two CLAP bundles
./uninstall.sh
```

`suite.lv2` contains both plug-ins, one manifest and one set of shared
resources, so hosts discover each plug-in URI exactly once. Building:

```
cmake -S . -B build && cmake --build build -j
(cd build && ctest)
```

## Peer link

Suite Channel Plus and Suite Control talk through a per-host-process POSIX
shared-memory registry (`/dev/shm/suitepeer-v6-<pid>`), so there is nothing to configure.
Only Suite Channel Plus registers - plain Suite Channel is never affected.

If the host runs plug-ins in separate processes the registry cannot be shared;
Suite Control then still works as a mix bus but shows no strips.

## LV2

One bundle, `~/.lv2/suite.lv2`, containing both plug-ins and the shared
resources.  Both UIs are X11 and save state (parameters, channel names and
mutes, insert links) through the LV2 state interface.

## OSC transport (optional)

The row along the bottom of the bus page sends transport commands to REAPER over
localhost UDP; nothing needs configuring in the plug-in.  In REAPER, add an OSC
control surface (Preferences → Control/OSC/web) with mode **Local port**, local
listen port **8000** and pattern config **Default.ReaperOSC**.  A `Local port`
device replies to the sender, so the feedback comes back to the plug-in without
setting a device port.

- Play/stop/record go out as `/play`, `/stop`, `/record` *and* the LogicTouch
  spellings `/1/play`, `/1/stop`, `/1/record`, so either shipped pattern config
  drives them.  Start/end use `/action/40042` and `/action/40043`, which needs
  `ACTION` patterns (present in Default.ReaperOSC, disabled in LogicTouch).
- Ports default to send 8000 / receive 9000.  Override them in the host's
  environment with `SUITE_OSC_SEND_PORT` / `SUITE_OSC_RECV_PORT`; with a second
  OSC device on 8001/9001 only `SUITE_OSC_SEND_PORT=8001` is needed.
- The dot beside the row turns green when REAPER feedback is arriving.
- The row is tuned to REAPER's OSC patterns; Ardour's address space differs.

## Requirements

- Suite Channel Plus must be installed; plain Suite Channel is a separate,
  unchanged plug-in.
- CLAP first. The LV2 build of Suite Channel Plus does not register yet.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```
