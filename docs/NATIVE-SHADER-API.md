# Native shader vocabulary

hypershader owns its shader runtime contract. First-party shaders, builder
output, examples and authoring tools use the reserved `hs_` namespace;
external-format vocabulary belongs only in explicit import adapters.
The underlying OBS effect language, `ViewProj`, `image`, user-defined
uniform names, existing audio signals and saved source/settings IDs
remain unchanged. This is a shader API change, not a new renderer.

## Common runtime inputs

| Native identifier | Meaning |
| --- | --- |
| `hs_time`, `hs_delta_time` | Shader clock and time step in seconds |
| `hs_frame_index`, `hs_frame_rate` | Virtual frame index and frame rate |
| `hs_resolution` | Current pass width, height, aspect (float3) |
| `hs_pointer`, `hs_date` | Pointer and date vectors |
| `hs_sample_rate`, `hs_scale` | Audio sample rate and source scale |
| `hs_transition_progress` | Transition progress |
| `hs_input0` through `hs_input3` | Existing four texture slots |
| `hs_input_resolution0` through `hs_input_resolution3` | Texture width, height, aspect, reserved (float4) |
| `hs_input_time0` through `hs_input_time3` | Per-slot shader clock |
| `hs_history` | Previous output texture |
| `hs_buffer_a`, `hs_buffer_b` | Current graph buffer textures |
| `hs_buffer_a_previous`, `hs_buffer_b_previous` | Previous graph buffer textures |
| `hs_graph_frame`, `hs_graph_output_resolution` | Graph frame and output size |
| `hs_sight_*` | Sight provider-specific signals |
| `hs_segmentation`, `hs_face_*`, `hs_landmarks` | Existing analysis resources |

The full graph allocation contract, including [named ordered passes](TWO-BUFFER-GRAPH.md#named-ordered-passes), lives in [Persistent render graphs](TWO-BUFFER-GRAPH.md).
See [shader authoring](../data/shaders/SHADERS.md) for declarations and
sampling primitives.

## Sight semantic region signals

Add Sight Privacy V2 before hypershader on the same source. Enable its
custom semantic detector and **Analysis only (shader signals; no concealment)**;
leave approved-frame buffering off. Enable Sight tracking in hypershader.
A missing, disabled, stale, ambiguous or incompatible producer is unavailable.
Unknown intervening filters are coordinate barriers. No second model is loaded.

| Uniform | Type / meaning |
| --- | --- |
| `hs_sight_semantic_present` | float, 1 for a valid snapshot (including empty) |
| `hs_sight_semantic_count` | int, number of regions, 0–256 |
| `hs_sight_semantic_mask` | texture2d, 512×512 R8 union of all selected rectangles |
| `hs_sight_semantic_bounds` | texture2d, 256×1 RGBA32F; normalized x0,y0,x1,y1 |
| `hs_sight_semantic_meta` | texture2d, 256×1 RGBA32F; category, confidence, low 24 bits of track ID, held flag |

Check present and count before sampling; unavailable returns zero logical
count and fallback textures. Use `Load(int3(i,0,0))` for each region index
below count. Category IDs depend on the selected model. The mask represents
rectangular coverage, not pixel segmentation; analysis-only is not censorship.
The typed ABI is versioned, caller-owned, and rejects oversized snapshots rather
than silently truncating. The source remains live, so a region can lag motion.

The shipped **Sight: Semantic Prism** effect demonstrates category selection and
three region treatments. It uses the union mask fast path for all categories.

**Sight: Semantic Materials** assigns three category IDs to independent
materials (solid, illustrated ink, hologram, brushed metal, mosaic,
iridescence, ceramic glaze, emissive circuitry, animated relief), tint
and mix. Slots A, B, C have descending priority at overlaps. Set category
to -1 or mix to zero to disable a slot. Unmapped regions pass through.
The helper `data/stdlib/semantic_regions.glsl` exposes
`hs_semantic_route(uv,a,b,c,out bounds,out metadata)` and
`hs_semantic_local(uv,bounds)` for other shaders. Declare the existing
semantic uniforms before including it. The router chooses priority first,
then fresh observations, confidence and smaller packed track ID. The
procedural patterns use region-local coordinates, not true surface geometry;
the union mask skips uncovered pixels. Rendering cost grows with region count.
These are creative effects, not a privacy guarantee.

### Surface-aware material fusion

Semantic Materials additionally accepts the existing source-local depth,
flow and foreground matte signals. Place semantic detection, Depth Camera
(Original source/share depth), Subject Cutout (Original source) and
Spatial Motion (Original source/share motion) before hypershader in the
same source's render chain. Each optional producer must be unique and
analysis-only; no coordinate-changing filter may intervene. Enable Sight
shared snapshots in hypershader. There is no additional model inside the
shader.

- **Depth Response** uses four adjacent inverse-depth taps plus the center
  to reconstruct an edge-aware height-field normal. It warps procedural
  ink/hologram/metal coordinates and blends brushed metal toward
  surface-oriented specular/Fresnel lighting. Relief, radius and edge
  tolerance are adjustable.
- **Motion Response** uses confidence-weighted backward displacement,
  normalized to a 33 ms reference interval and capped to a quarter of
  the region per axis. It deforms procedural patterns, without accumulating
  history or moving the region boundary.
- **Foreground Clipping** (off by default) multiplies material mix by
  the shared soft subject matte. The matte is not a per-category mask;
  use it for foreground subjects, not arbitrary background objects.
- Each optional input independently falls back to the existing region
  material if absent or disabled. Missing semantics still pass through.

These independent asynchronous snapshots are spatially aligned, not
frame-synchronized; the normals and texture mapping are approximate.
The shader does not reconstruct metric geometry or true mesh UVs.

### Persistent surface coordinates

The optional **Persistent Surface Coordinates** slider is off by default
to preserve existing material presets. Set it to 1 for full locking.
The new linked Sight flow procedure reports the actual predecessor
sequence of each backward motion snapshot. hypershader transports a
32×18 coordinate-offset field once per linked snapshot, not once per
render or graph pass. A missing predecessor, replacement, resize, or
skipped snapshot reseeds the field; low-confidence cells and samples
outside the previous image are rejected. Temporarily unavailable flow
hides the field, and a subsequently valid linked snapshot can resume it.
Reload the shader or disable shared Sight snapshots to clear history.

| Native uniform | Meaning |
| --- | --- |
| `hs_sight_surface_uv` | RGBA32F 32×18; RG = reference UV minus current UV, B = confidence, A reserved |
| `hs_sight_surface_present` | float; 1 if a valid linked coordinate field is available |

For custom effects, sample the field at current source UV, then add its
RG offset to recover the approximate reference coordinate. Respect B
confidence and the present flag. Older Sight versions retain ordinary
instantaneous motion but do not provide persistent coordinates. Semantic
Materials blends region-local coordinates toward scene-relative reference
coordinates, using square pattern units; fully locked pixels skip the
instantaneous motion offset. The preview can show encoded displacement
(RG = 0.5 + 4 × offset, B = confidence) or grayscale confidence.

The field is coarse, shared across the image, and not an instance-specific
surface parameterization. Occlusion, poor texture, independent snapshot
timing, missed captures and prolonged motion may cause drift or reseeding.
It does not preserve hidden geometry or guarantee physical attachment.

When source-local depth is available, the transport also caches a coarse
depth observation at each flow step. Between distinct depth publications
with matching source dimensions, it rejects correspondence if relative
inverse depth differs by more than 76/255 from the previous warped cell.
That cell is invalid for the step and can reseed on a later reliable
observation. Missing or repeated depth does not trigger the comparison.
This is a disocclusion heuristic, not synchronized geometry: normalization
changes or fast depth motion can also cause a reset.

### Additional material looks

The four appended material choices preserve the numeric IDs of existing
presets. Iridescence uses a procedural thin-film-like spectrum influenced
by estimated orientation; ceramic glaze combines grain, veins and
specular lighting; emissive circuitry uses a reference-coordinate grid
with animated pulses and pads; animated relief adds procedural lighting
and bounded source-image displacement. All four consume the same
persistent coordinate field and depth/motion controls. They are
screen-space treatments, not physical material simulations or geometric
displacement. Circuit emission is an SDR color treatment, not an HDR
light source.
No HDR, cross-platform, or real-time throughput claim is made.

The controlled OpenGL test checks opposite depth slopes, opposite flow
directions, zero confidence, foreground clipping, and exact bypass.
For model-backed testing run `scripts/verify-sight-interop.sh` with the
existing semantic fixture variables plus `SIGHT_MATERIAL_DEPTH_MODEL`,
`SIGHT_MATERIAL_MATTE_MODEL`, and optionally
`SIGHT_MATERIAL_FLOW_SHIFT_IMAGE` (same-sized translated image).
This checks the real depth and matte rendering response, disabling and
restoring producers, nonzero measured flow, linked history and a retained
coordinate offset after motion settles in the composed chain. The
controlled shader test additionally verifies coordinate-lock pattern
response, invalid-confidence fallback, and no double application of flow.
The transport unit test covers changed-depth rejection, independent
neighbor preservation, recovery, missing depth and repeated observations;
the controlled shader test also checks the four appended material IDs,
coordinate response, animation and unchanged pixels outside regions.

## Named texture inputs

Declare a texture with a same-line `@input N` annotation (N = 0 through
3), optionally with `@label`. The shader can use its own semantic name,
and OBS exposes the existing input picker under that label:

```hlsl
uniform texture2d normalMap; // @input 0 @label "Normal Map"
uniform float4 normalMap_resolution;
uniform float normalMap_time;

uniform texture2d roughnessMap; // @input 2 @label "Roughness Map"
uniform float4 roughnessMap_resolution;
```

Resolution (float4) and time (float) companions are optional read-only
runtime values, automatically recognized by the exact suffix. Resolution
contains actual texture width, height, aspect and a reserved component;
time is the existing shader clock. If a companion is absent, its
numbered runtime counterpart is still available. Do not use both
spellings of the same binding. Missing named textures use the black
fallback and zero logical width/height. Existing `hs_input_atlasN`
sprite metadata remains slot-based.

The annotation assigns the existing slot; this is not an additional
resource pool. Saved `channelN_*` settings, images, showing OBS sources,
and audio options stay in the same resource manager. Assignments and
settings remain slot-based even when switching shaders. Use unique
slots and names; do not also reference the corresponding numbered
texture, including through macros. Native `hs_` names, text/audio
metadata and defaults cannot be used for named texture declarations.
The two transition A/B slots are reserved. Invalid declarations fail
shader loading and retain the last working effect. Extra named slots,
persistent settings keyed by semantic name, named atlas companions,
and transition support have not been qualified.

Simple native effects can use:

```hlsl
#include "../../stdlib/hypershader_standard.glsl"

void hs_shade_pixel(out float4 color, in float2 pixel)
{
    float2 uv = pixel / hs_resolution.xy;
    color = image.Sample(textureSampler, uv);
}
HYPERSHADER_PIXEL_WRAPPER
```

Complete native effects can declare their own vertex/pixel entry points
and Draw technique instead.

## Migrating existing custom shaders

Bundled shaders have already been migrated. Existing custom effects
using the previous runtime names must be converted before using this
new runtime; there is no implicit dual-name binding or in-place
modification of managed shader revisions. For a custom effect:

    python3 scripts/migrate-shader-vocabulary.py old.effect new.effect

The helper replaces only known complete code identifiers, preserves
comments, strings, custom identifiers and relative include paths, and
refuses to overwrite either file or proceed with conflicting old/new
names. Migrate custom include files separately and keep relative paths
intact. Review the result and compile it before selecting the new file
in OBS. It is not a general shader importer, and does not rewrite
saved scene settings or metadata.

The separate `shadertoy_compat.glsl` adapter is for explicitly imported
foreign source, not native effects. Existing engine-independent
`shaderfilter_compat.glsl` and `isf_compat.glsl` likewise map their own
external contracts onto the native layer. The import fixture is tested
separately from first-party authoring.

## Verification

The native vocabulary contract is tested in CI. Linux/libobs/OpenGL
native regression tests verify source/filter graphs, standard library,
builder output and original instruments. The six existing original
instrument previews were re-rendered after renaming: every resulting
PNG is byte-identical to its previous preview. Shader source hashes
and preview filenames were updated accordingly. This does not claim
Windows/macOS, HDR, or arbitrary existing user-shader compatibility.
