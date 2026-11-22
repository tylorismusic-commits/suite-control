# Suite Control

**Version 0.1.0** · GPL-3.0-or-later · CLAP · Linux

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

## Peer link

Suite Channel Plus and Suite Control talk through a per-host-process POSIX
shared-memory registry (`/suitepeer-v1-<pid>`), so there is nothing to configure.
Only Suite Channel Plus registers - plain Suite Channel is never affected.

If the host runs plug-ins in separate processes the registry cannot be shared;
Suite Control then still works as a mix bus but shows no strips.

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
