# Native shader vocabulary

HyperShader owns its shader runtime contract. First-party shaders, builder
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

Add Sight Privacy V2 before HyperShader on the same source. Enable its
custom semantic detector and **Analysis only (shader signals; no concealment)**;
leave approved-frame buffering off. Enable Sight tracking in HyperShader.
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
