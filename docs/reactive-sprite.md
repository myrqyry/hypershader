# Reactive Sprite

Native HyperShaderFX source and filter effects, independently implemented after
reviewing OBS Image Reaction's two-state interaction model.

## Setup

- Add a HyperShaderFX **source**, choose Source / Reactive Sprite, set the
  canvas size, then set Channel 0 to the idle image. Optional channels 1, 2,
  and 3 are active, strong, and accent images. Missing optional textures
  fall back to the previous available state.
- Or add a HyperShaderFX **filter** to an existing video/image source and
  choose Filter / Reactive Sprite. The original source is the idle image;
  optional channels 0, 1 and 2 provide the other three states.
- Enable Sound and select the desired OBS audio source (for example, a
  microphone). Choose the reaction driver: voice/level, peak, kick, bass,
  mid or high. Threshold is in HyperShader's empirical audio signal units,
  not dB. Adjust sensitivity and threshold to the selected source.
- Crossfade Width 0 selects binary target states; a positive width blends
  across a range of audio levels. Strong/Accent thresholds are ordered above activation.
- Bounce, Stretch, Tilt, Vertical Anchor, Idle Breathing and optional
  silhouette glow work even with only one image. Set motion and glow to zero
  for conventional image switching. Image textures fit the canvas without
  stretching, and pixels outside the texture remain transparent. Allow
  transparent padding to accommodate motion; the shader cannot expand OBS's
  source dimensions automatically.

## Temporal response (enabled by default)

- Attack and Release are separate exponential time constants in milliseconds.
  Zero removes that part of the easing. Hold keeps each state open for the
  configured interval after crossing its lower closing threshold.
- Hysteresis lowers the closing threshold relative to the opening threshold;
  it prevents a quiet syllable or noisy microphone from rapidly switching states.
- Spring Frequency and Damping drive a real second-order motion response,
  independent of the expression easing. Lower damping permits overshoot.
  The spring advances once per OBS video frame, not once per render pass.
- Disable Temporal Response to restore the original direct level-to-image
  and motion mapping, including instantaneous switching at zero crossfade width.
- State is instance-local and resets on shader change, input selection changes,
  or long render gaps. Turning Temporal Response off clears its history.
- If no fresh PCM arrives for 250 ms, the response treats it as silence and
  releases the expression; a missing or stopped audio source cannot leave
  a permanent open-mouth state. Immediate mode also uses this guard.

## Animated image playback

Choose a GIF directly in any image channel. Static PNG/JPEG/etc. still work
as before. The idle GIF in a standalone source always runs continuously;
expression GIFs use the Animated Images dropdown:

- **Continuous**: all configured animated image channels run regardless of
  expression activation.
- **Restart on reaction** (default): an expression starts at frame zero when
  its reaction target opens, runs while its weight remains visible, and
  pauses after it closes. The next activation restarts from frame zero.
- **Pause / resume**: preserves the last animation frame between reactions.

The GIF's encoded frame delays and loop count are respected. A non-looping
GIF holds its final frame until the next restart, while a looping GIF can
repeat for as long as its expression is active. Multiple reaction levels
have independent channel clocks. Repeated renders within a video frame
do not advance those clocks twice; a long render gap does not fast-forward.

This uses OBS's native animated image helpers, including its in-memory
frame cache; no file reopening on activation, extra decoder, browser source
or GPU history texture. The OBS 32-built binary's GIF advancement, reset,
pause/resume and finite replay were pixel-tested with both OBS 32 and 33.
Existing OBS source/media channels retain their own playback behavior.
## Sprite sheets

Choose a regular image containing an evenly spaced grid of animation frames
and use the matching Idle, Active, Strong or Accent Sprite Sheet controls.
Filters have only the three expression sheets; the original source is idle.

- Columns and Rows: grid dimensions (default 1 × 1, which preserves ordinary
  image/GIF sampling). Use sheets with equal cell sizes and no gutters.
- First Frame: zero-based, row-major, starting at the top-left.
- Frame Count: length of the selected sequence; zero means all remaining
  frames. Ranges are clamped to the grid.
- Frames per Second: independent playback rate per sheet, up to 60; zero
  freezes the current frame.
- Sequence: Loop, One shot (hold the final frame), Ping pong, or Random frames
  (no immediate repeats). The Animated Images playback policy also applies
  to expression sheets; it determines whether a new reaction restarts,
  resumes, or leaves their clocks running.
- Idle bursts (standalone source only): play an entire sequence in order,
  then return to its first frame for a randomly chosen rest between the
  Minimum and Maximum Rest values. Set both to the same number for fixed
  spacing. This works for blinks and occasional gestures without shuffling
  individual animation frames.

For example, use a 4 × 2 PNG, First Frame 2, Frame Count 3, and 12 FPS
to play frames 2–4 at 12 FPS. The same image file may be assigned to
several channels with different ranges and playback rates. Sprite fitting
uses the cell dimensions, not the entire sheet; texel-center clamping keeps
linear filtering from leaking neighboring frames. There are no additional
GPU history textures or file reads on each animation frame. The native
host-owned `hs_input_atlas0..3` uniforms provide columns, rows, current
frame index, and a reserved fourth component.

Suggested starting points:
| Use | Attack | Hold | Release | Frequency | Damping |
| --- | ---: | ---: | ---: | ---: | ---: |
| Speech / PNG avatar | 20 ms | 120 ms | 80 ms | 3 Hz | 0.7 |
| Bouncy mascot | 35 ms | 90 ms | 120 ms | 3 Hz | 0.45 |
| Percussion | 5 ms | 35 ms | 70 ms | 5 Hz | 0.5 |

These are starting values, not bundled presets or calibrated dB settings.

## Verification

`scripts/verify-reactive-sprite.sh` loads the actual module in isolated
libobs/OpenGL, delivers PCM to a named OBS audio source, and checks rendered
source/filter pixels for idle, active, return to silence, transparent edges,
single-image peak-driven motion, temporal controls, hold versus immediate
mode, loss of incoming PCM, and a separate metadata spring fixture. The test also rejects shader-load
and uniform-size errors. Run the deterministic dynamics checks with
`scripts/verify-reaction-dynamics.sh` (hysteresis, hold, overshoot, settling,
frame-rate comparison and independent state). The GIF integration probe
checks source/filter expression playback and repeated restart edges, plus
silent idle animation. The image-channel ABI probe independently verifies
actual GIF pixels, duplicate-frame guards, pause/resume and finite replay
on both OBS runtimes. Atlas rendering tests cover independent active,
strong and accent ranges, one-shot restart, idle random frames and idle
bursts, correct non-square cell fit, transparent letterboxing and edge
isolation. `scripts/verify-sprite-atlas.sh` tests the clock policies,
ping-pong, random selection and ordered bursts deterministically.
Glow has not received dedicated pixel tests yet.

## Native shader contract

For custom source/filter effects, opt in by declaring
`uniform float4 audio_reaction_state;` directly in the effect. The host binds
XYZ to the active/strong/accent weights (0–1), W to the signed spring position.
Do not include this declaration in a universal standard-library header:
effects without the uniform should not allocate or advance reaction state.
The settings are shader-owned scalar uniforms with the same names/defaults
as Reactive Sprite (response_driver, sensitivity, threshold,
strong_threshold, accent_threshold, transition_width, temporal_response,
hysteresis, attack_ms, hold_ms, release_ms, spring_frequency, spring_damping).
They are exposed by the normal metadata UI, even when the CPU consumes them.
The state is advanced once per video frame and rebound for graph passes.
