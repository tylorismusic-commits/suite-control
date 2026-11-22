# Changelog

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
