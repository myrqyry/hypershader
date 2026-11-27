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

    STDLIB_TEST_MODES=sampling bash scripts/verify-stdlib-loader.sh /path/to/hypershader.so

The test loads a real hypershader filter, renders a moving color fixture
with distinct left/right edge values, and compares all RGBA pixels for
27 frames. It exercises negative modulo, wrapping, clamping, mirror
addressing, texel centers and interpolation across the repeat seam.
Verified on Linux libobs/OpenGL; Windows/macOS, explicit mip levels and
mipmapped resource sampling are not yet qualified. No change to existing
native effects or the old translation of mod; use hsFloorMod explicitly.

## Qualified second slice: independent native render targets

The existing two-buffer filter graph supports immutable shader-declared
divisors (1, 2, 4, 8) and explicit RGBA16F intermediate data formats.
The full input remains available to each pass; current-pass, per-buffer
and output resolutions have separate built-in uniforms. Histories are
initialized at the actual sizes and reset on resize or shader replacement.
Existing effects that omit declarations use the original full-size/source
format defaults. Invalid allocation descriptors bypass the effect.

Linux libobs/OpenGL tests qualified default and half/quarter graphs,
signed and above-one history values, last-texel addressing, per-instance
isolation, user controls and compositing, odd-dimension resizing and invalid descriptors.
See [the graph contract](TWO-BUFFER-GRAPH.md). HDR input/output behavior,
other platforms, dynamic scaling, and end-to-end performance are not
yet qualified. Named ordered passes now extend the same scheduler.

## Qualified third slice: resource admission

The native graph has a default 512 MiB logical color-target payload cap
per instance, with a shader-declared lower limit. It counts the capture,
persistent buffers and conservative compositor reserve using overflow-safe
arithmetic. Oversized graphs release their render targets and bypass
until the source fits again; recovery starts with clean history.
Verified in the real Linux graph with repeated reject/recover cycles.
This is not an aggregate GPU memory manager or driver allocation estimate.

## Qualified fourth slice: external texture binding across passes

Existing four channel resources, their true dimensions and their shader
clock are rebound for every graph stage. Missing resources have an
opaque-black fallback and zero logical dimensions. A real image channel
pixel test failed before the fix and now passes for present, removed
and restored states. This does not yet expand the number of texture
slots or implement arbitrary named bindings. See [the graph contract](TWO-BUFFER-GRAPH.md).

## Qualified fifth slice: live OBS source textures

Existing channel slots can capture already-showing independent OBS
sources and scenes using the native renderer, in the filter graph
and standalone source. Weak references, source-removal checks,
self/parent/containing-scene rejection and reentry guards prevent the
tested stale-resource and recursive-render paths. Each slot is capped
at 128 MiB logical color payload. This is separate from the graph
budget. Tests cover image pixels, true capture sizes, hide/show, source
identity changes and removal. Hidden-source activation, transition
capture, arbitrary cross-shader cycles and HDR/platform parity are
not qualified.

## Qualified sixth slice: named texture bindings

Native `@input N` texture declarations map author-defined uniform names
and optional labels onto the four existing resource slots. Optional
`_resolution` and `_time` companions expose read-only runtime data.
The Linux graph test verifies two independent named image inputs,
actual dimensions/timing, labels, clearing/restoring, rejected duplicate
assignments and switching back to numbered inputs. A named standalone
source also passes the existing live capture lifecycle checks.
See [native shader vocabulary](NATIVE-SHADER-API.md#named-texture-inputs).
An expanded slot pool and name-keyed saved settings remain future work.

## Qualified seventh slice: ordered persistent pass schedules

One to eight shader-named passes run through the existing graph
renderer and per-instance admission budget. Compiled uniform defaults
define explicit order, dimensions and formats. Each stage sees the
latest completed texture plus an explicit previous-frame texture.
The three-stage native fixture checks signed and above-one histories,
resource dimensions, same-frame dependencies, forward history reads,
controls, compositing, isolation and resize. Malformed plans reject
before replacement. This is a bounded ordered scheduler, not an
automatic dependency sorter or transient-resource allocator.

## Capability matrix

| Primitive | Current boundary | Next |
| --- | --- | --- |
| Scalar/vector math, timing, UV, controls | Existing; portable sampling qualified | Audit actual library functions and portable intrinsics, verify numerical parity |
| Texture inputs | Four named or numbered input slots, source image, auxiliary signals; image and showing-source capture verified | Expand resource pool, type/size/format/color-space declarations |
| Sampling | Native linear clamp plus opt-in addressing helpers | Mipmap and explicit LOD qualification, per-resource sampler capability probes |
| Texture storage | Native 2D inputs and selected float data textures | Cubemap, volume, arrays/atlas, linear data formats and limits |
| Render passes | One to eight named ordered persistent passes or existing A/B, Draw; independent size and RGBA16F; filter and source lanes | Explicit dependency DAG, transient resources, transition lane and aggregate admission |
| Temporal state | Previous output and two persistent buffers with sized initial histories and resize reset | Named history, lifetime and aggregate memory budgets |
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
2. Independent pass dimensions/formats, resource-resolution uniforms
   and resize tests are implemented for the two-buffer filter.
   Per-instance logical graph allocation budget is implemented.
   Named ordered schedules use the same renderer and admission budget.
   Automatic dependency scheduling, transient resource reuse and
   aggregate GPU admission remain planned.
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
