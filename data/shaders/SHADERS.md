# HyperShader Authoring Guide 🎨

For a native time-mosaic example using previous-output feedback, see
[Slit Scan Tape](../../docs/slit-scan-tape.md).

This guide explains how to write custom effect files (`.effect`) for the HyperShader plugin.

Native runtime identifiers use the reserved `hs_` namespace.
See [native shader vocabulary and migration](../../docs/NATIVE-SHADER-API.md)
for the contract and the opt-in custom-effect migration tool.

## 1. Quick Start

A basic shader file looks like this:

```hlsl
// @min 0.0 @max 10.0 @label "Speed Control"
uniform float speed = 1.0;

uniform float hs_time;
uniform float3 hs_resolution;

float4 PSMain(VertData v_in) : TARGET {
    return float4(1.0, 0.0, 0.0, 1.0); // Red
}
```

The plugin **automatically parses comments** starting with `@` to build the UI sliders/controls for you!

---

## 2. Metadata Tags (UI Controls)

Add these tags in comments directly above your `uniform` variables. The parser searches backwards up to 3 lines from the variable declaration.

### Float Sliders
For `uniform float x;`

| Tag | Description | Example |
|-----|-------------|---------|
| `@min <val>` | Minimum value | `// @min 0.0` |
| `@max <val>` | Maximum value | `// @max 10.0` |
| `@step <val>` | Step size (default 0.01) | `// @step 0.1` |
| `@default <val>` | Initial value.
  - Scalar: `@default 0.5`
  - Vector: `@default "0.5,0.0,1.0"` (Note quotes)
 | `// @default 1.0` |
| `@label "..."` | Custom display name | `// @label "Intensity"` |

### Integer Dropdowns
For `uniform int x;`

| Tag | Description | Example |
|-----|-------------|---------|
| `@list "A,B"` | Comma-separated options | `// @list "Red,Green,Blue"` |

*Note: The options map to 0, 1, 2...*

### Conditional visibility

Use @show_if "control" on a uniform to show it only when the named
boolean/integer/float control is truthy, or @show_if "!control" to invert it.
Numeric comparisons are supported: ==, !=, <, <=, >, >=. For example:

~~~hlsl
uniform bool show_vectors; // @default false @label "Show Motion Vectors"
uniform float vector_brightness; // @min 0 @max 3 @default 1 @show_if "show_vectors"
uniform int mode; // @list "Simple,Advanced" @default 0
uniform float sharpness; // @min 0 @max 1 @show_if "mode == 1"
~~~

This controls OBS property visibility only: values stay saved when hidden
and shader execution still receives the uniform. The controller must be an
available shader uniform; invalid or missing dependencies fail closed.
Compound and/or expressions are not part of the native @show_if syntax.

### Grouping logic
| Tag | Description | Example |
|-----|-------------|---------|
| `@group "Name"` | Puts control in a collapsible group | `// @group "Advanced Settings"` |

---

## 3. Audio Reactivity 🎵

You can bind any `float` uniform to audio analysis data!

**Syntax:** `// @audio "source" @gain <g> @smooth <s>`

### Audio Sources
- `"bass"` : Low frequencies (60Hz - 250Hz)
- `"mid"`  : Mid frequencies (250Hz - 2kHz)
- `"treble"`: High frequencies (2kHz - 16kHz)
- `"level"` : Overall volume (RMS)
- `"peak"`  : Peak volume

### Modifiers
- `@gain <val>` : Multiplier (default 1.0). Use higher values (2.0-5.0) for quiet sources.
- `@smooth <val>` : Smoothing factor (0.0=instant, 0.99=very slow). Default 0.5.

**Example:**
```hlsl
// @audio "bass" @gain 3.0 @smooth 0.8
uniform float audio_level;
```

---

## 4. Built-in Uniforms

The HyperShader runtime automatically provides these standard uniforms:

| Type | Name | Description |
|------|------|-------------|
| `float` | `hs_time` | Seconds since shader start |
| `float` | `hs_delta_time` | Time since last frame |
| `int` | `hs_frame_index` | Frame count |
| `float4` | `hs_date` | year, month, day, seconds |
| `float3` | `hs_resolution` | Viewport size (pixels) in xy |
| `float4` | `hs_pointer` | xy = current pos, zw = click pos |
| `sampler2D` | `hs_input0`..`3` | Texture inputs (set in plugin UI) |
| `float4` | `hs_input_resolution0`…`3` | Resolution of each input in xy |

---

Named texture inputs can use arbitrary shader-owned names while retaining
the existing four slots:

```hlsl
uniform texture2d normalMap; // @input 0 @label "Normal Map"
uniform float4 normalMap_resolution;
uniform float normalMap_time;
```

Use unique slots (0–3); do not also sample the corresponding numbered
texture. The companions are optional read-only bindings. See
[named texture inputs](../../docs/NATIVE-SHADER-API.md#named-texture-inputs)
for limits and saved-settings behavior.

## 5. Persistence (Feedback Loops) 🔄

To create trails or motion blur, read from the **previous frame**:

```hlsl
uniform sampler2D hs_history; // Automatically provided if declared
```

**Example (Trails):**
```hlsl
float4 current = ...;
float4 prev = hs_history.Sample(textureSampler, uv);
return lerp(current, prev, 0.9); // 90% trail
```

---

### Multi-resolution feedback

Native filter effects with BufferA, BufferB and Draw techniques can
request independent internal buffer sizes using integer default uniforms
`hs_buffer_a_divisor` and `hs_buffer_b_divisor` (1, 2, 4 or 8).
Use `hs_buffer_a_format = 1` or `hs_buffer_b_format = 1` for RGBA16F state.
The shader's `hs_resolution` is the active pass size; the float3
`hs_buffer_a_resolution`, `hs_buffer_b_resolution` and `hs_graph_output_resolution`
describe each respective resource. These are runtime bindings, not
controls. Existing hs_input texture, resolution and time uniforms are
rebound in every graph pass. Missing textures report zero logical size
and sample black. Existing OBS source slots capture already-showing
independent sources and scenes in filter/source lanes; hidden or
recursive inputs are unavailable. Optionally declare an integer `hs_graph_budget_mib` (1–512) to
reduce the default logical graph allocation cap of 512 MiB. See the [two-buffer graph contract](../../docs/TWO-BUFFER-GRAPH.md)
for declarations, defaults, reset behavior and verification scope.

### Named persistent passes

New native filter graphs can use up to eight shader-defined passes,
with a required integer `hs_pass_<id>_order` starting at zero, a matching
`Pass_<id>` technique and current/previous texture2d uniforms.
For example `hs_pass_diffuse` and `hs_pass_diffuse_previous`.
The optional `_divisor`, `_format` and float3 `_resolution` companions
define and describe each target. Final Draw remains full resolution.
All passes use the same scheduler and budget as the existing A/B
contract. See [named ordered passes](../../docs/TWO-BUFFER-GRAPH.md#named-ordered-passes)
for the complete binding rules and limits.

## 6. Standard Library

You can include helper functions from `data/stdlib/` (source effects live in `data/shaders/root/`):

```hlsl
#include "../../stdlib/noise.glsl" // Perlin, Simplex noise
#include "../../stdlib/math.glsl"  // Constants, utilities
```


---

### Portable texture addressing

Include `hypershader_standard.glsl` followed by `sampling.glsl`.
The latter provides explicit texture-coordinate addressing rather than
relying on independent sampler states for a shared OBS texture.

| Helper | Behavior |
| --- | --- |
| `hsFloorMod(x, y)` | GLSL floor remainder, including negative values; scalar/vector, nonzero divisor |
| `hsTexelUV(pixel, size)` | Clamp integer pixel coordinates and return normalized texel-center UV |
| `hsFetchTexel(tex, pixel, size)` | Texel-center sampling of a non-mipmapped 2D resource |
| `hsSamplePointClamp(tex, uv, size)` | Nearest texel, clamp outside bounds |
| `hsSamplePointRepeat(tex, uv, size)` | Nearest texel, repeat across edges |
| `hsSampleLinearRepeat(tex, uv, size)` | Four-tap bilinear repeat, including the edge seam |
| `hsSampleLinearMirror(tex, uv)` | Linear mirrored repeat |

Pass the sampled texture's positive integral dimensions, not necessarily
the output resolution. For the full-resolution filter image use
`hs_resolution.xy`. For channels use `hs_input_resolution0.xy` through
`hs_input_resolution3.xy` as appropriate. Check resource availability
before sampling. Macro arguments must be side-effect-free. These helpers
use the existing linear/clamp sampler; they do not generate mipmaps,
create new resources or implement independent hardware sampler bindings.
For ordinary linear clamp use `texture(tex, uv)`.

See [shader capability roadmap](../../docs/SHADER-CAPABILITIES.md)
for the verified platform scope and remaining runtime work.

## 7. Windows/macOS Variant Workflow

For strict per-platform packaging, generate platform-specific shader files:

```bash
python3 tools/shader_tools/generate_platform_variants.py
```

This emits:
- `data/shaders/platform/windows/*.effect` (generated from native HyperShader sources)
- `data/shaders/platform/macos/*.effect` (generated from native HyperShader sources)

See `OBS_SHADER_SYNTAX.md` in this folder for a concise compatibility checklist.


## Reusing Sight's face, body and hand tracking

With **Use Sight shared snapshots (tracking, matte, text, flow, depth)** enabled on a HyperShader
filter, \`Sight Tracking Channel\` chooses an existing publisher in the same
OBS process (e.g. Sight's Face/Body/Hand Motion filter using the channel
\`face\`, \`body\`, or \`hand\`). This does **not** run a new MediaPipe model.

The native OBS/libobs shader runtime binds the following optional uniforms:

| Name | Kind | Meaning |
| --- | --- | --- |
| \`hs_sight_present\` | float | 1 when a fresh, non-empty snapshot is available; otherwise 0 |
| \`hs_sight_kind\` | int | 1=face, 2=body/pose, 3=hands; 0 when unavailable |
| \`hs_sight_point_count\` | int | Number of primary points (face up to 478, pose 33, hand 21) |
| \`hs_sight_points\` | texture2d RGBA32F | One pixel per landmark at coordinate (index,0): x, y, z, confidence |
| \`hs_sight_hand_count\` | int | 0, 1, or 2 tracked hands |
| \`hs_sight_hands\` | texture2d RGBA32F | 21 columns × 2 rows; one set of 21 landmarks per row |
| \`hs_sight_pinch0\` / \`hs_sight_pinch1\` | float | Sight's relative pinch measurement for each hand |
| \`hs_sight_gesture0\` / \`hs_sight_gesture1\` | int | 0=unknown, 1=Closed_Fist, 2=Open_Palm, 3=Pointing_Up, 4=Thumb_Down, 5=Thumb_Up, 6=Victory, 7=ILoveYou |
| \`hs_sight_blendshapes\` | texture2d RGBA32F | 64 columns × 1 row, face scores in R; order follows producer and should not be treated as a stable named-rig mapping |
| \`hs_sight_head_rows\` | texture2d RGBA32F | 4×1 texture containing raw 16-value head transform, where provided |

Always gate texture sampling on \`hs_sight_present\`, the corresponding
\`hs_sight_kind\`, and the count. Missing or expired snapshots release old
GPU data, and absent optional inputs bind the host's dummy texture.

For face shaders, \`hs_landmarks\` and \`hs_face_present\` reuse the same Sight
landmarks when this mode is selected. If the shader requests \`hs_face_surface\`
or \`hs_face_triangles\`, the native HyperShader GPU topology uses the first 468
points from Sight's 478-point face output. Sight body and hand channels do
not generate a face surface.

A concrete hand example is
\`data/shaders/root/filter.sight_hand_highlights.effect\`. It draws thumb and
index markers for up to two hands, while leaving the original image in place
when no current hand is detected.

Hand Ripples (`data/shaders/root/filter.sight_hand_ripples.effect`) replaces
the old standalone Sight Touch Field filter. On the same native source,
place LLL.sight Tracking (Hands & gestures) upstream, turn off its overlay,
then choose Sight: Hand Ripples in HyperShader and enable shared Sight
snapshots with that hand channel. One tracker supplies both fingertip and
pinch data. The consumer launches up to eight bounded wavefronts as index
fingertips move, preserving the original 1.6 second decay and aspect-aware
refraction. Radius, strength, rings, speed and pinch boost remain adjustable.
Missing/stale hand snapshots, invalid points or mismatched dimensions clear
the effect rather than freeze a ripple. Coordinates still require the
same untransformed framing. No additional inference is needed.
 A test-only effect under
\`data/shaders/tests/sight_snapshot_pipeline_probe.effect\` validates genuine
point-texture sample values through the real two-plugin OBS render path.

Sight and HyperShader must operate in the same OBS process. Landmark
coordinates are normalized to the producer's camera/image, not globally
aligned between different source crops. Face expression blendshape names
should be resolved using Sight metadata; the present HyperShader shader
interface exposes scores rather than a named rig API.

### Source-local Sight foreground geometry

Put one enabled Sight Subject Cutout (U2Net) upstream on the
same source, choose Diagnostic view → Original source, then enable shared
Sight mode on HyperShader. The landmark channel is independent.

| Uniform | Type | Meaning |
| --- | --- | --- |
| `hs_segmentation` | texture2d | Shared R8 stabilized inference matte when available |
| `hs_sight_matte_present` | float | Fresh compatible matte, including empty foreground |
| `hs_sight_subject_present` | float | At least one R8 pixel >=128 in a fresh matte |
| `hs_sight_subject_bounds` | float4 | minX,minY,maxX,maxY in normalized source UV, outer pixel edges |
| `hs_sight_subject_center` | float2 | Centroid of foreground pixel centers in normalized source UV |
| `hs_sight_subject_coverage` | float | Fraction of the entire mask with R8 >=128 |

The geometry is the union of thresholded foreground pixels, not individual
person detections or a confidence estimate. Empty, stale or unavailable
data sets all geometry and its presence flag to zero. A valid empty matte
can therefore have matte presence 1 and subject presence 0. Gate shaders on
the relevant presence flag. All uniforms are runtime-owned, not editable
shader controls. Existing Sight v1 producer ABI is unchanged: one bounded
CPU scan of the received mask only on a new sequence; no extra inference,
GPU readback, or inter-plugin payload. Coordinates use the inference matte,
not Sight's full-resolution edge-refined/calibrated output.

The native preset `root/filter.sight_subject_frame.effect` uses geometry
without sampling the matte, drawing corner brackets, an optional centroid
crosshair, and an optional bar proportional to coverage. Color, opacity
and line width are adjustable; absent foreground or zero opacity preserves
the source. This is an aggregate framing/debug overlay, not auto-cropping.

Unit checks: `scripts/verify-sight-interop.sh` (includes subject geometry
fixtures). Real two-plugin pixel and preset checks:
`live-latent-lab-sight/tests/run_shared_matte_obs.sh`, against both updated
builds and a foreground portrait.

### Source-local Sight motion

| Uniform | Type | Meaning |
| --- | --- | --- |
| `hs_sight_motion_present` | float | Fresh comparable capture pair (1), otherwise 0 |
| `hs_sight_motion_energy` | float | Whole-frame normalized mean absolute luma change, 0–1 |

Same enabled immediate-upstream Sight Subject Cutout as the matte. Choose
Original source in Sight and enable shared Sight mode. The motion probe
uses the existing inference-rate captures (not video frame rate) and
requires successful U2Net inference. Valid stationary frames yield 0
with presence 1; first/invalid/stale pairs yield zeros. Age limit 750ms
from capture; pairs more than one second apart are not comparable.
Luma change includes cuts, lighting and camera motion; it is not optical
flow or a time-normalized speed. The separate ABI leaves matte and
landmark consumers compatible; motion and matte are independently read
and can straddle consecutive revisions. Check their independent flags.
No new texture or model for the motion scalar.

`root/filter.sight_motion_pulse.effect` is a native shader using motion
energy to modulate foreground tint and edge glow. The Debug energy view
renders the measured scalar, sensitivity amplifies small changes,
and optional Audio Beat Modulation combines the existing `audio_beat`
with motion (off by default). Zero motion in normal view or missing
inputs preserves source. Motion can be used without a matte texture for
debug/other scalar-only shaders. Real OBS still/change/still and
foreground pulse checks run via sibling Sight's shared-matte test script.
Audio-reactive combination itself has not been verified with live audio.

### Source-local Sight OCR text geometry

Put exactly one enabled **Sight: Text Obfuscation (OCR)** filter
upstream of HyperShader on the same source. Select Original source in Sight
and opt in to **Use Sight shared snapshots (tracking, matte, text, flow, depth)** in
HyperShader. Requires an OCR-enabled Sight build and local language data.
The landmark channel is independent; this does not start a second OCR or
MediaPipe model.

| Uniform | Type | Meaning |
| --- | --- | --- |
| `hs_sight_text_present` | float | Fresh validated OCR snapshot (1), otherwise 0 |
| `hs_sight_text_count` | int | Number of padded line rectangles, 0–512 |
| `hs_sight_text_mask` | texture2d | 512×512 R8 union of padded rectangles, normalized source UV |
| `hs_sight_text_bounds` | texture2d | 512×1 RGBA32F, each texel is normalized x0,y0,x1,y1 |

A valid empty OCR result has presence 1, count 0, black mask and zero
bounds. Gate on presence, then count; when unavailable the count and
presence are zero and textures fall back to the existing dummy texture.
Use point sampling and read bounds at `float2((index+0.5)/512.0,0.5)`.
The rectangles come from Sight's existing OCR line snapshot, not character
glyphs. The consumer validates the ABI, dimensions, finite coordinates,
capacity and confidence, and rejects ambiguous producers. Snapshot age
limit is 2000ms from capture, independent of the producer's local effect
age setting. A mask rasterized at 512×512 rounds outward, so the edges
can differ slightly from Sight's own inference-resolution mask. One
bounded CPU raster and GPU upload per new sequence, no image-file I/O,
new capture, OCR, or export of recognized strings.

Native `root/filter.sight_text_scramble.effect`: 0 = animated synthetic
noise (default), 1 = opaque black, 2 = mask preview. Noise block size,
update rate and tint are adjustable. Generated opaque noise does not
re-use source text pixels inside the chosen mask. Zero rate freezes the
noise. Missing data passes through live source; normal treatments do
nothing for empty results. Best-effort only: moving or missed text can
be visible, just like the standalone Sight effect. No PII classifier.

Verify using the sibling Sight test runner:
```sh
cd ../live-latent-lab-sight
tests/run_text_ocr.sh bridge "$PWD/build-verified/sight_obs_effect_render_smoke" \
  "$PWD/build-verified/live-latent-lab-sight.so" /path/to/tessdata \
  /usr/local/lib/obs-plugins/image-source.so \
  "$PWD/../hypershader/build_x86_64/hypershader.so" "$PWD/../hypershader/data"
```
Use absolute module and data paths if libobs cannot resolve relative paths.
Pure union/validation fixtures run in `scripts/verify-sight-interop.sh`.

### Source-local spatial motion (DIS)

Put exactly one enabled **Sight: Spatial Motion (DIS)**
upstream of HyperShader on the same source. Choose Original source and
opt in to **Use Sight shared snapshots (tracking, matte, text, flow, depth)**.
Requires an OpenCV-enabled Sight module. Consecutive enabled analysis-only
Sight filters may now be stacked. Only Original source modes of Subject
Cutout, Spatial Motion, OCR and Depth Camera are recognized as transparent. An enabled
unknown or treatment filter blocks deeper producers; disabled filters are
transparent. One enabled producer per type on the same source, in actual
render order. Each signal retains its independent TTL; no same-frame guarantee.
The depth + matte + flow stack is verified; triple OCR composition is
not yet qualified.

| Uniform | Type | Meaning |
|---|---|---|
| `hs_sight_flow` | texture2d | 32×18 RGBA32F: backward dx,dy in source UV, confidence, reserved |
| `hs_sight_flow_present` | float | Fresh validated snapshot |
| `hs_sight_flow_interval` | float | Seconds between captures |
| `hs_sight_flow_coverage` | float | Fraction of cells accepted by the worker |
| `hs_sight_flow_step` | float | Native filter graph only: 1 on a new flow sequence after seeding, otherwise 0 |

The grid is defined at current-frame cell centers. Add RG to current UV
to address the corresponding previous capture; negate to get approximate
forward displacement, divide by interval for capture-normalized velocity.
Point sample to inspect cells, linear sample for smooth creative effects;
confidence zero means invalid, not stationary. Gate on presence before
sampling (dummy fallback when absent). The consumer validates dimensions,
sequence, finite bounded cells, coverage and interval; snapshots expire
500ms after capture. A new sequence causes one bounded GPU upload.
No additional model, RGB readback, or optical-flow analysis in the consumer.

**Sight: Flow Warp** (`root/filter.sight_flow_warp.effect`) samples the
*current* frame offset by confidence-weighted measured displacement,
with adjustable strength and optional direction visualization. Not frame
interpolation, temporal echo, or stabilization. Idle/absent input is
passthrough. See sibling Sight's spatial motion test runner for real
two-plugin image-source verification. Native validation runs through
`scripts/verify-sight-interop.sh`.

**Sight: Flow Echo** (`root/filter.sight_flow_echo.effect`) uses native
BufferA → BufferB → Draw. A stores current source; B retains the channelwise
maximum of fresh RGB and decaying, confidence-weighted advected history.
The final output blends fresh and B RGB, preserving current source alpha.
Trail half-life is in shader-time seconds (nominal render delta, affected
by time scale/extra renders), tracking strength 0–2, mix 0–1, clear
history toggle. Zero tracking gives screen-space feedback, not bypass.
Loss of Sight availability, graph initialization, zero effect mix,
or clear-history enable seeds from fresh rather than old history.
Out-of-bounds advection samples fresh instead of clamped-edge history.

`hs_sight_flow_step` is a graph-only capture-step gate; seed the first
observed sequence, advance on a larger sequence, and consume only on
successful graph completion. It is constant across all techniques in
that graph render. On absent flow the graph observes sequence zero;
reload/resize also clears it. Repeated renders with the same sequence
must not reapply the displacement. It is zero on non-graph paths.
If updates are skipped, only the latest displacement is available;
there is no exact recovery of intermediate motion fields. Capture and
render times are asynchronous. This is creative advection, not
frame-aligned reconstruction, motion blur or exact object tracking.
No new producer, optical-flow pass, or external ABI change.

Pixel-level native technique test verifies an eight-pixel translation of
retained history, no translation on step zero, zero tracking, half-life,
fresh initialization/absence/bypass/clear. Run
`scripts/verify-sight-interop.sh` and the sibling Sight
`tests/run_spatial_motion_obs.sh` with both real modules. The latter
adds real Flow Echo history, clear, missing producer and bypass tests.

The graph captures upstream filters/custom-draw sources without an
outer default-effect loop, since their nested capture owns its draw
calls. Ordinary non-custom input sources keep the Draw loop required
by obs_source_draw(). Real Sight graph and Motion-Matched Datamosh
input-source regression both pass. The actual consumption marker is
SIGHT_FLOW_GRAPH_STEP_20261118, independent of shader cache hit/miss.

### Sight: Flow Sparks

Native preset `root/filter.sight_flow_sparks.effect` adds motion-seeded
glowing tracers without another vision pass or CPU particle simulation.
Use the same source-local Spatial Motion producer upstream,
Original source output and shared Sight snapshots enabled.

Buffer A seeds jittered sparse spots on new flow steps only, using
confidence and capture-normalized displacement speed in source pixels
per second. Buffer B backtraces the previous density by confidence-
weighted backward displacement once per new snapshot, rejects out-of-
bounds history, decays using exp2(-shader_dt/half_life), and retains the
maximum of new and retained density. Two normalized RG channels encode
density (R + G/255), providing approximately 16-bit storage even in RGBA8
history and a linear decode suitable for bilinear sampling; values below
1/255 are extinguished. The resulting half-life is nominal, affected by
render timing, interpolation, threshold and time scale. Draw tints the
source with that density while preserving current alpha. The color's
alpha controls glow strength, not output alpha.

Controls: half-life (0.05–3s), tracking (0–2), emission (0–1), spacing
(6–64 source px), radius (1–8 px; constrained by spacing), minimum speed
(0–300 px/s), color (cyan by default), effect mix and clear sparks.
Zero tracking retains screen-space sparks; zero emission stops new
births while history decays. Clear, absent signal or zero mix clears
history and returns the input. Stationary motion does not emit.
This is an Eulerian tracer-density effect, not separately persistent
Lagrangian particle identities, hand segmentation, or exact object
tracking. No additional ABI, model, or runtime renderer changes.

Verification: `scripts/verify-sight-interop.sh` exercises native BufferA,
BufferB and Draw with controlled textures: sparse emission, stationary
and zero-confidence rejection, exact 8-pixel advection, step gate,
half-life, zero tracking, clear, bypass and output compositing.
The sibling Sight five-argument spatial motion runner also verifies
actual default cyan emission and red recoloring, lingering on stationary
frames, clearing, reacquisition/passthrough and the existing Flow Echo,
Flow Warp and stabilization tests. No live webcam or hardware benchmark.


Flow Sparks now offers Spark Region 0 (All), 1 (Subject), 2 (Background).
For 1 or 2, stack Subject Cutout Original source and Spatial Motion Original
source before HyperShader. Soft matte weights seeding and display, so
background tracers are hidden over the foreground silhouette; density can
persist behind it. Missing matte resets restricted modes to clean input;
All mode remains independent. This is matte occlusion, not metric depth
or tracked particle identities. Real portrait composition passed both
producer orders, disable/recovery and treatment barrier. Controlled native
GPU test checks no wrong-region seeds, missing matte and occlusion; actual
masked sparks smoke rendered on a translated portrait.

### Shared relative inverse depth

Place Sight Depth Camera (Depth Anything V2) in **Original source (share
depth)** mode in the analysis-only source chain. Existing model and
inference settings remain in Sight; no second depth model is started.
`hs_sight_depth` is a normalized R8 texture, 0 = far / 1 = near, up to
518px per axis. `hs_sight_depth_present` is an independent freshness and
compatibility flag; use it before sampling. 1500ms capture TTL.
Equal values across time do not imply equal real-world distance because
the model normalizes each inference independently. Existing temporal
smoothing may add lag. Shared uniforms rebind across native graph passes.

**Flow Sparks**: optional Depth Occlusion (off by default) uses Spark Layer
(0 far, 1 near, default .55) and Depth Edge Softness (.01–.25).
Visibility is `1-smoothstep(layer-softness, layer+softness, scene_depth)`,
multiplied by the existing matte region in seed and Draw passes.
Depth-disabled mode continues without a depth producer. Depth-enabled
mode resets density and passes through if depth is missing. This is
a screen-space tracer-density layer, not a volumetric simulation.
The sibling Sight depth bridge runner verifies all three real signals,
837 sampled depth texels against CPU, independent loss/recovery and
actual layered sparks. Native interop tests verify near/far masks.

### Sight: Depth Glass

Native single-pass `root/filter.sight_depth_glass.effect` warps the
current source with animated, pixel-scale ripples restricted to a soft
relative inverse-depth band. Requires Depth Camera in Original source
(share depth), shared Sight snapshots enabled. Optional Protect Subject
requires Subject Cutout in Original source mode upstream as well. No
new inference, additional ABI, optical flow, or feedback buffers.

Controls: refraction 0–48 source px, ripple wavelength 16–256px, speed
0–2 cycles/s, depth center 0–1 (far to near), depth band half width
.05–1, allowed depth difference .01–.3, protect subject, effect mix,
preview band. Depth band fades from full weight at 55% of its half
width to zero at its outer edge. Four evenly spaced back-sampling path
taps test depth difference, and optionally subject presence; boundaries
fade or reject the warp instead of blindly sampling across them.
Out-of-frame samples are rejected; source alpha preserved and large
alpha discontinuities suppress displaced RGB. Missing required signal
or zero mix yields the original. This does not reconstruct occluded
surfaces, raytrace refraction, guarantee thin-edge preservation, or
synchronize the independently captured depth/matte maps.

Native real-libobs test verifies band, edge and midpath rejection,
subject protection, missing signals, zero controls, preview, animation
and alpha. Real portrait runner verifies native installed filter
depth selection (91,803 altered far pixels, 0 altered near pixels
at test settings), preview, protect-subject missing/recovery and
depth missing/recovery. See sibling Sight depth bridge runner.
The preset was added as a new installed shader asset without replacing
either loaded plugin binary. If the shader list is cached, refresh or
select Custom Shader File from the installed data directory.
