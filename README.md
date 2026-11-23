# Suite Control

**Suite 0.11.0** · GPL-3.0-or-later · CLAP + LV2 · Linux

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

## OSC

The row along the bottom of the bus page drives the host's transport and a
second strip view (`OSC`, after `OUT`) that mirrors the host's tracks.  Click
the **OSC** button in that row for the settings sheet: protocol, ports, live
status, setup instructions and `SYNC`.

REAPER (Preferences → Control/OSC/web → Add → OSC):

- Mode **Configure device IP+local port**, device IP `127.0.0.1`,
  device port = the plug-in's **receive** port, local listen port = its
  **send** port, pattern config **Default.ReaperOSC**.
- Defaults are send 8000, receive 9000; `SUITE_OSC_SEND_PORT` and
  `SUITE_OSC_RECV_PORT` override them.
- REAPER publishes track names and levels through its device-selection sweep;
  the plug-in walks that selection when the link opens, on `SYNC` and when the
  bank changes.  REAPER only reports button/level *changes* otherwise.

Ardour: enable **Control Surface → Open Sound Control (OSC)**.  Ardour listens
on UDP 3819 and replies to the plug-in's receive port; `/set_surface` is sent
on connect and on `SYNC`, which refreshes every strip.

The LV2 UI speaks both address spaces automatically (REAPER loads LV2 as well
as CLAP); the CLAP build is REAPER-only.  The LV2 and CLAP UIs share the same
settings sheet.

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
