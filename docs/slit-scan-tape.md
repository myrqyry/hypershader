# Slit Scan Tape — hypershader

**Native shader:** `data/shaders/root/slit_scan_tape.effect`

This is an original hypershader HLSL/OBS-effect implementation of the
**slit-scan time mosaic** technique, inspired by Image/ine (ImWeb). It was
recovered from the project's local *round-two uninstalled draft* and tested
against real libobs/OpenGL rendering. No Image/ine source code was copied.

Unlike `warp_tape.effect`, which cyclically overwrites selected columns at
their original positions, this filter samples the **same selected input slit**
each time and shifts the **previous output** across the image. As recording
continues, horizontal/vertical position in the resulting picture represents
successive captures over time.

## Controls

- **Horizontal Scan (Rows):** capture a horizontal row/strip, pushing the
  accumulated tape upward rather than leftward
- **Input Scan Position:** select which part of the input frame is sampled
- **Scan Width:** insert 1–32 pixels/rows per update
- **Frames Per Advance:** 1–12 *rendered frames* per recording update
- **Reverse Scroll Direction:** insert at left/top instead of right/bottom
- **Hold Tape:** freeze the current tape without advancing
- **Erase Tape:** clear the history to transparent while enabled

Apply as an **effect filter on an existing camera/game/video source**. The
standalone hypershader Source has no live input image, so it cannot build a
video tape without an external texture feed.

## Rendering contract

The effect uses `hs_history` as the actual **previous output** through
hypershader's current ping-pong render history. Only the new strip samples
the *current* `image`; remaining pixels are read from the previous output at
a shifted coordinate. There is no CPU framebuffer readback or external state.

- Recording cadence is in rendered **frames**, not elapsed seconds. FPS changes
  change effective tape speed.
- Changing direction or scan axis reuses existing tape content, so **Erase**
  first when changing layouts if you need a clean composition.
- The initial history may be empty or may inherit a seeded source frame
  depending on the host; Erase clears it intentionally.
- This is not a whole-image per-pixel time-displacement ring or an Image/ine
  project serializer. Window resizing triggers host history reset.
- For large or high-FPS sources, using narrow scan widths makes the effect
  visually more gradual but still requires full-frame sampling of the history.

## Native verification

From repository root:

```bash
bash scripts/verify-slit-scan-gpu.sh
```

The test compiles the **actual `.effect` file** using libobs/OpenGL, then
executes GPU ping-pong and reads real output pixels. It verifies successive
red/green/blue time bands, persistent history, hold, erase, reverse insertion,
frames-per-advance gating, horizontal history and scan-position sensitivity
using a spatially varying input (not just a solid-color fixture). The existing
`verify-warp-tape-gpu.sh` remains a separate regression for Warp Tape.

This validates effect-level GPU behavior, **not** live OBS UI/property changes,
installed module lifecycle and frame pacing, Direct3D/Metal, or performance at
streaming resolutions. No system or user plugin installation occurs in tests.
