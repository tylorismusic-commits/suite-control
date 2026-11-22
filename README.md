# Suite Control

**Suite 0.4.0** · GPL-3.0-or-later · CLAP + LV2 · Linux

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
shared-memory registry (`/suitepeer-v1-<pid>`), so there is nothing to configure.
Only Suite Channel Plus registers - plain Suite Channel is never affected.

If the host runs plug-ins in separate processes the registry cannot be shared;
Suite Control then still works as a mix bus but shows no strips.

## LV2

The LV2 bundle is `build/lv2/suite-control.lv2` (plugin + X11 UI).  Install it
to `~/.lv2/suite-control.lv2` and rescan.  Parameter state is saved by the host
through the control ports.  The channel **name table and mute states** are
CLAP-only for now: LV2 has no place to store them without a custom state
property, so in LV2 they last for the session.

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
