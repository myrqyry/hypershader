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

The full graph allocation contract lives in [Two-buffer graph](TWO-BUFFER-GRAPH.md).
See [shader authoring](../data/shaders/SHADERS.md) for declarations and
sampling primitives. Named author-defined texture resources are still
planned; the four slots remain for now.

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
