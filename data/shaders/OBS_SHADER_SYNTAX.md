# OBS-Compatible Shader Syntax (Windows + macOS)

Use this guide when authoring hypershader `.effect` files that should run cleanly across OBS on Windows and macOS.

## Core Syntax Rules

- Start from OBS effect syntax (HLSL/Cg-style), not raw GLSL.
- Native hypershader effects include `#include "../../stdlib/hypershader_standard.glsl"` when using the standard runtime uniforms/helpers.
- External-format compatibility headers are adapters only; do not use them as the base for first-party hypershader effects.
- Effects that declare their own native `ViewProj`/`VertData`/`VSDefault` (full custom passes) do not include the compat layer.
- Use either OBS-native `float2/3/4` or hypershader's native `vec2/3/4` authoring aliases.
- Use `frac`, not `fract`.
- Use `lerp` or hypershader's native `mix` alias.
- Use `texture(tex, uv)` with the native hypershader standard, or OBS-native texture sampling in full custom effects.

## Avoid These Cross-Platform Pitfalls

- Avoid `mat2` (`float2x2`) in OBS effect shaders.
- Avoid dynamic array-heavy logic for OpenGL-style backends.
- Keep loop bounds constant when possible.
- For simple `hs_shade_pixel` effects, finish with `HYPERSHADER_PIXEL_WRAPPER`. Full native effects may declare their own techniques/passes.

## Standard Cross-Platform Template

```hlsl
#include "../../stdlib/hypershader_standard.glsl"

// @min 0.0 @max 2.0 @step 0.01 @label "Speed"
uniform float speed = 1.0;

void hs_shade_pixel(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / hs_resolution.xy;
    vec3 col = 0.5 + 0.5 * cos(hs_time * speed + uv.xyx + vec3(0, 2, 4));
    fragColor = vec4(col, 1.0);
}

HYPERSHADER_PIXEL_WRAPPER
```

## Generating Windows/macOS Variants

Run:

```bash
python3 tools/shader_tools/generate_platform_variants.py
```

This creates:

- `data/shaders/platform/windows/*.effect`
- `data/shaders/platform/macos/*.effect`

These generated files are ready to ship as platform-specific presets while preserving the original source shaders.
