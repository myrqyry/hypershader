# Persistent render graphs — native OBS effect runtime

## Available now
Opt-in fixed three-stage sequence, executed by the production hypershader filter and standalone source:
1. Capture the unfiltered upstream image for a filter, or use the source lane’s opaque-black 1×1 placeholder.
2. Run technique BufferA, with its own two ping-pong history targets.
3. Run technique BufferB, sampling the **current** BufferA and its own previous-frame state.
4. Run technique Draw, sampling both **current** A and B.
5. Swap both history targets only after successful rendering.

Existing one-pass effects keep their existing Draw and hs_history paths.
BufferA and BufferB support independent bounded resolutions and optional
RGBA16F storage. Their existing contract is implemented by the same
scheduler as the named pass form below.

## Named ordered passes

For new effects, declare one to eight named ordered passes (persistent by default). The order
uniform is immutable metadata, not a slider. For example, the following
is one pass declaration (a complete effect also needs its vertex/pixel
functions, a matching Pass_integrate technique and final Draw technique):

```hlsl
uniform int hs_pass_integrate_order = 0;
uniform int hs_pass_integrate_divisor = 2;
uniform int hs_pass_integrate_format = 1;
uniform texture2d hs_pass_integrate;
uniform texture2d hs_pass_integrate_previous;
uniform float3 hs_pass_integrate_resolution;
```

Use lowercase identifiers starting with a letter (up to 24 characters,
letters, digits and underscores). Resource companion suffixes
`_order`, `_previous`, `_resolution`, `_divisor`, `_format`, `_transient`, `_last_use` are
reserved at the end of pass identifiers. Orders must be unique and
contiguous from zero, regardless of declaration or technique order.
For each persistent id, declare a matching `Pass_<id>` technique, the two texture2d
bindings, and optionally the float3 resolution binding. Use divisor 1,
2, 4 or 8 and format 0 (source format) or 1 (RGBA16F), as for A/B.
The final Draw technique renders at full source resolution.

For persistent passes, before each pass the unsuffixed resource represents the most recently
completed result: current-frame output for earlier stages, previous
frame for the current or later stages. The explicit `_previous`
resource always refers to the previous successful graph frame.
In Draw all unsuffixed resources refer to current-frame results.
The original image (full-sized for filters, the black placeholder for sources), controls, external texture inputs and resolution/time built-ins are rebound through the existing renderer.
This is an ordered schedule with implicit texture sampling dependencies,
not an automatically sorted DAG or shader-language dependency analysis.
Persistent stages own two surfaces. Use explicit
`_previous` for intentional temporal dependencies.

The loader rejects noncontiguous or duplicate orders, out-of-range
orders, unknown reserved resource names, missing techniques, incorrect
resource types and invalid allocation descriptors. A rejected named
schedule leaves the previous working effect in place. Do not mix the
named form with BufferA/BufferB. Named schedules work in filters and standalone sources; transitions remain unsupported.
The existing A/B contract remains supported, not a second renderer.

The native fixture
[`ordered_pass_graph.effect`](../data/shaders/tests/ordered_pass_graph.effect)
contains a complete integrate/diffuse/resolve schedule. The regression
runner also derives an eight-pass chain and six malformed schedules
inside an isolated copy of the plugin data directory.

## Transient passes and physical target reuse

For purely current-frame intermediates, declare an immutable transient
descriptor and omit the previous-frame texture:

    uniform int hs_pass_blur_order = 1;
    uniform int hs_pass_blur_divisor = 2;
    uniform int hs_pass_blur_transient = 1;
    uniform int hs_pass_blur_last_use = 2;
    uniform texture2d hs_pass_blur;

Transient values are 0 (default persistent) or 1. The last-use descriptor
is the final consumer's zero-based pass order, inclusive; the final Draw
is numbered N for an N-pass graph. Omit last-use to retain the result
through Draw. It must be between the producer's order and N. Persistent
passes must not specify last-use, and transient passes must not declare
a previous-frame texture. Malformed descriptors reject the candidate
without replacing the working shader.

The host assigns a single surface to each transient lifetime. It may
reuse a surface only after the previous owner's inclusive last-use,
and only for matching divisor and format. Persistent history is never
aliased. Before production or after expiry, a transient binding receives
the opaque-black 1x1 fallback (never the active write target). Its
resolution uniform still describes its nominal pass size; do not use
integer fetches on an unavailable binding. The author must declare a
correct lifetime: native effect sampling dependencies are not inferred
or statically checked by the scheduler. Use the default through Draw
when uncertain.

A four-stage native fixture with one persistent stage and three transient
stages in a chain reuses the first temporary for the third. At equal
descriptors it needs four pass surfaces instead of eight, with the same
arithmetic and retained history. The native probe counts distinct
texrender handles and reads the actual final pixels in source and filter
lanes (set HS_TRANSIENT_TEST_FILTER=1 for the filter). The temporary
allocation budget counts physical owners rather than logical stages.
The visual Builder continues to emit persistent graphs until it gains
explicit transient controls; full native effect authoring is supported.

## Contract
A graph effect must declare all four texture uniforms hs_buffer_a,
hs_buffer_b, hs_buffer_a_previous, hs_buffer_b_previous, and techniques BufferA,
BufferB, Draw. A normal effect with only Draw is unchanged.

In BufferA, hs_buffer_a and hs_buffer_b refer to prior-frame buffers.
In BufferB, hs_buffer_a is current frame A and hs_buffer_b is prior-frame B.
In Draw, hs_buffer_a and hs_buffer_b refer to current frame buffers.
In all stages, hs_buffer_a_previous and hs_buffer_b_previous refer to the prior frame.

On the first frame after a shader change or resize, prior textures are
initialized to opaque black at their actual configured sizes and formats, never uninitialized GPU contents or a one-pixel substitute. Optional int hs_graph_frame
starts at 0, resets on shader replacement or render-target resize,
and increases only after a successful graph frame. This is separate from OBS hs_frame_index.

The same original input image (or source placeholder) is bound into all three stages. Per-shader
metadata controls are rebound before each stage because effect parameters
can be reset by OBS technique loops. A real libobs test checks that
a saved output-gain control actually changes the final rendered pixels.

## Reset history while live

For a source or filter, Global Overrides > Reset History requests a fresh
simulation frame without recompiling or resetting the shader clock, controls,
or any other effect instance. The next valid draw consumes the request;
requests before that draw coalesce. A named or A/B graph starts again at
hs_graph_frame = 0 with opaque-black, full-sized prior textures. Single-pass
feedback uses the normal initial seed (transparent for a source, incoming
video for a filter). Compatible physical surfaces remain allocated.

OBS hotkey bindings are per instance: Reset effect history, and Cancel
pending history reset. Bind distinct keys if using both. The cancel action
only cancels a request not yet consumed; it cannot restore discarded history.

A native OBS integration may call the source's proc handler with the void
procedure hypershader_reset_history (no arguments). A successful procedure
call indicates submission, not completion. Hidden/disabled effects consume
the request when they next draw. Transitions do not implement this action.

## Independent buffer size and precision

The following optional shader declarations are allocation descriptors, not
user controls. They are read once at shader load, before allocating graph
resources, and never from saved slider values.

```hlsl
uniform int hs_buffer_a_divisor = 2; // half width and height
uniform int hs_buffer_b_divisor = 4; // quarter width and height
uniform int hs_buffer_a_format = 1;  // RGBA16F signed/HDR data
uniform int hs_buffer_b_format = 1;
uniform float3 hs_resolution;
uniform float3 hs_buffer_a_resolution;
uniform float3 hs_buffer_b_resolution;
uniform float3 hs_graph_output_resolution;
```

Divisors must be 1, 2, 4 or 8. The default is 1; each dimension is
rounded up independently. Format 0 (default) follows the upstream
capture format; 1 explicitly uses RGBA16F. Invalid descriptor types,
missing initializers or values cause the graph to pass the upstream
source through with a logged diagnostic. The final Draw stage remains
full resolution and uses the source/output format. The original image
remains full resolution in every stage.

All resolution vectors contain width, height and aspect ratio. Within
BufferA/B, hs_resolution is that pass's actual target size; within Draw it
is the full output size. The other three resolution uniforms retain
their respective resource dimensions across all stages, including the
first initialized history. Use those sizes for integer texel fetches
and texel offsets rather than assuming the viewport and all textures
are equal. Shader replacement and source resize reset the history.

Example: on a 1920 x 1080 input, half-size BufferA and quarter-size
BufferB contain 648,000 pixels in total per history pair, versus
4,147,200 at full resolution. This is 84.375% fewer buffer pixels;
memory depends on format and full-resolution capture/output allocations.
It is not an end-to-end performance benchmark.

## External texture inputs in every stage

The graph rebinds existing `hs_input0` through `hs_input3` textures,
their `hs_input_resolution0` through `hs_input_resolution3` dimensions
(float4: width, height, aspect, reserved), and their
`hs_input_time0` through `hs_input_time3` values before each technique.
All stages use the same current resource snapshot; rebinding does not
decode or advance an animated image again. Missing textures sample
opaque black and report zero width and height. Check logical dimensions
before sampling a missing resource. Channel time follows the existing
shader clock rather than a media decoder clock.

The native test loads a generated 3x2 image, verifies every output pixel
after sampling the channel in all three passes, clears the input, and
restores it. This qualifies static file input and live replacement in
the graph on Linux/OpenGL. Live OBS source and independent scene capture are now also verified
in the graph and standalone source lanes on Linux/OpenGL. Audio textures,
GIF animation and other backends still need graph-specific verification.

### Live source channels

Select OBS Source in an existing channel slot. The source must already be
showing and enabled, with a nonzero size. The native capture renders the
selected source (including its normal OBS render path) into an on-demand
texture, preserving its actual dimensions. It does not activate hidden
media sources, route audio, or retain a strong reference to another scene.
Self/parent selection and scenes containing the consumer are rejected;
reentrant capture is guarded. Arbitrary nested cross-shader cycles and
capture while an effect technique is already active are not supported.
Missing or rejected resources report zero logical dimensions and use
the black fallback. Captures are limited to 128 MiB logical color
payload per slot, separate from the graph's 512 MiB allocation limit.
This is not a global VRAM limit. Transition channels retain their distinct
A/B contract; live capture in transitions is not qualified.

The live-source tests cover a changing synthetic OBS source, source resize,
hide/show, self/parent and containing-scene rejection, independent scene
capture, explicit UUID selection, restoration and removal. They verify
every output pixel in all three graph passes and in a standalone source.
Hardware camera/media behavior, HDR sampling, arbitrary graph cycles and
hidden-source activation remain to be qualified.
The four existing slots also accept shader-defined texture names and
read-only resolution/time companions using the
[native named input contract](NATIVE-SHADER-API.md#named-texture-inputs).
Names and labels are resolved before shader replacement, and each pass
uses the same existing image or live capture. The native test covers
two named image inputs in all three passes, removal/restoration,
duplicate-slot rejection without losing the old shader, switching back
to numbered inputs, and a named live standalone source.

## Resource allocation budget

Graph color-target payload is capped per filter instance at 512 MiB.
The optional shader declaration `uniform int hs_graph_budget_mib = 64;`
can request a smaller cap (integer 1 through 512). This is immutable
metadata, not a live control. The calculation reserves the full-size
input capture, both full-size compositor surfaces, a transparent pixel,
and two textures per persistent buffer or one per distinct transient
physical owner, using its actual format and rounded dimensions. Integer arithmetic rejects overflow.

The cap is a conservative logical texture-payload estimate, not a GPU
VRAM measurement or global limit. Driver allocation overhead, input
channels, ML resources, other filters and OBS's output target are not
included. An over-budget graph releases its graph targets and passes the
upstream image through for filters (standalone sources draw nothing) without silently changing its resolution or
precision. Logging occurs once per rejection episode. When the source
size fits again, fresh histories are allocated; old state is not reused.
Existing two-buffer full-resolution 4K SDR and RGBA16F graphs fit the
default budget. Larger schedules and some 8K graphs require smaller
intermediate targets.

The budget calculator also has a dependency-free C test:

    cc -std=c17 -Wall -Wextra -Werror -Isrc scripts/test-graph-budget.c -o /tmp/hs-graph-budget-test
    /tmp/hs-graph-budget-test

The native graph runner checks rejection, recovery, repeat rejection,
and clean reset after resizing with a deliberately small 1 MiB budget.

## Included native effect
data/shaders/root/ink_reaction_buffers.effect demonstrates a
Gray-Scott-inspired reaction and diffusion state in BufferA and a
separate persistent phosphor trail in BufferB, composited with input
image using tunable defaults. It is a visual effect, not a
scientifically validated physical simulation.

## Native verification
Run from repository root:

    cmake --build build_x86_64 --target hypershader --parallel 4
    scripts/verify-two-buffer-graph.sh
    scripts/verify-two-buffer-example.sh

The graph test opens the actual hypershader module, renders moving
frames through libobs and Mesa OpenGL, and checks both independently
evolving histories, per-instance isolation, and observable final
output from a user parameter. The example test separately verifies
that the real artist-facing shader compiles, loads, binds parameters,
and produces varying pixels.

The default graph test runner now also exercises the half/quarter
RGBA16F graph, previous-history access in Draw, exact last-texel access,
negative and greater-than-one data across frames, opacity changes without
losing history, odd-dimension resize and invalid descriptor pass-through. All use the real native renderer.
The runtime checks actual texture dimensions and format, not only
the requested allocation. Graph mode remains intentionally opt-in.

## Current limits
- Standalone sources use the same scheduler and budget; transitions are not wired.
- Standalone sources bind a 1×1 opaque-black image, not a full-sized input; use pass textures for integer texel reads. Opacity composites against transparency.
- The single-output hs_history feedback path is separate; graph effects
  use explicit per-pass previous textures.
- Real-time full-resolution performance, HDR correctness on other
  GPU drivers, and Windows/macOS were not established by Mesa tests.
- No automatic DAG sorting, transient-target reuse, dynamic/adaptive
  per-pass resolution, general compute shaders, or spatial ML input
  integration yet.
- Graph-enabled audio/animated channels and per-pass audio
  controls need dedicated integration regression coverage.

- The budget is per-filter logical graph color payload only; aggregate VRAM admission and backend-specific overhead are not accounted for.

## Standalone source instruments

Wave Chamber uses a signed height/velocity field and a second persistent light field; Chemical Garden uses Gray–Scott chemistry and a separate pigment memory. Both store their state at half resolution in RGBA16F, require no external image, and draw at the selected source dimensions. Simulation Step = 0 stops the simulation updates (the GPU output is visually stationary, not guaranteed bit-exact); the runtime graph frame counter continues to advance. The exciter phase can therefore change on resume. The simulation advances per graph render, not at a guaranteed fixed wall-clock rate.

Run the two native pixel regressions with `scripts/verify-source-instruments.sh /path/to/hypershader.so`. They check spatially varied opaque output, temporal change, visible controls, pause (at most eight 8-bit output levels at any channel and mean absolute channel change at most 0.01 over 40 samples), and resumed evolution.
