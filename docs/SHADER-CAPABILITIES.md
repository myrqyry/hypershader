# Native shader capability roadmap

Goal: provide functional equivalents of common game-engine shader
authoring primitives (Godot, Unreal and related environments) without
embedding those engines or silently substituting incompatible inputs.
External importers are adapters to the native runtime, not dependencies
of first-party effects. This is a capability roadmap, not a claim of
engine parity.

## Qualified first slice: portable sampling

Native `data/stdlib/sampling.glsl` is opt-in. It provides GLSL floor
remainder and texel-center, point clamp, point repeat, linear repeat and
linear mirror addressing with explicit texture dimensions. The repeat
seam uses four manually wrapped texel taps. This is deliberately
independent of hardware sampler-state switching on the same texture:
the native OBS/OpenGL probe reproduced sampler aliasing, and overloaded
helper functions also failed to resolve reliably.

Run:

    STDLIB_TEST_MODES=sampling bash scripts/verify-stdlib-loader.sh /path/to/hyperShaderFX.so

The test loads a real HyperShader filter, renders a moving color fixture
with distinct left/right edge values, and compares all RGBA pixels for
27 frames. It exercises negative modulo, wrapping, clamping, mirror
addressing, texel centers and interpolation across the repeat seam.
Verified on Linux libobs/OpenGL; Windows/macOS, explicit mip levels and
mipmapped resource sampling are not yet qualified. No change to existing
native effects or the old translation of mod; use hsFloorMod explicitly.

## Capability matrix

| Primitive | Current boundary | Next |
| --- | --- | --- |
| Scalar/vector math, timing, UV, controls | Existing; portable sampling qualified | Audit actual library functions and portable intrinsics, verify numerical parity |
| Texture inputs | Four input channels, source image, auxiliary signals | Named resource descriptors, type/size/format/color-space declarations |
| Sampling | Native linear clamp plus opt-in addressing helpers | Mipmap and explicit LOD qualification, per-resource sampler capability probes |
| Texture storage | Native 2D inputs and selected float data textures | Cubemap, volume, arrays/atlas, linear data formats and limits |
| Render passes | Fixed BufferA, BufferB, Draw; filter only | Independent resolution/format, then named dependency graph for filter/source/transition |
| Temporal state | Previous output and two persistent buffers | Named history, reset/resize semantics, lifetime and memory budgets |
| Scene buffers | Sight depth, flow, matte and tracking; estimated, not engine G-buffer | Unified sampling contract with valid/fresh/provenance and camera-space metadata |
| Geometry | Fullscreen draw and shader-side helpers | Subdivided planes, mesh vertex attributes, transforms, depth and instance drawing |
| Materials | Lighting/material helper files, not a general pipeline | Common surface record, light evaluation, normal/tangent conventions |
| Simulation | Fragment feedback; particle helper files | Bounded float state passes, particles and spatial effects |
| Compute | No general compute dispatch | Evaluate only after native raster/feedback limits are established |

## Implementation order

1. Resource contracts and native pixel tests. Prefer existing libobs
   facilities where actual backend behavior permits it. Keep shader
   authoring controls, texture declarations and builder capability
   descriptions aligned.
2. Extend the existing two-buffer renderer with independent pass sizes,
   formats, input/output dimension uniforms, and budget/resize tests.
   Generalize the scheduler only once that contract is established.
3. Expose scene inputs through one provider-independent API. Never label
   monocular estimated depth, optical flow or derived normals as genuine
   game-engine geometry. Missing/stale inputs must be explicit.
4. Add geometry, material and simulation primitives incrementally with
   actual isolated native render tests. Consider compute only for
   workloads that cannot be served by native render passes.

Do not claim platform parity based on a header, generated shader or
successful compilation alone. Require rendered fixtures, resize/reload
and teardown checks where applicable, and a capability report for each
supported graphics backend. Existing shader IDs and settings remain
stable. All new distributable runtime assets need explicit provenance
and inventory entries.
