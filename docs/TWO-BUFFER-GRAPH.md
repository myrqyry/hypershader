# Two-buffer graph — native OBS effect runtime

## Available now
Opt-in fixed three-stage sequence, executed by the production HyperShaderFX filter:
1. Capture the unfiltered upstream image.
2. Run technique BufferA, with its own two ping-pong history targets.
3. Run technique BufferB, sampling the **current** BufferA and its own previous-frame state.
4. Run technique Draw, sampling both **current** A and B.
5. Swap both history targets only after successful rendering.

Existing one-pass effects keep their existing Draw and hs_history paths.
BufferA and BufferB support independent bounded resolutions and optional RGBA16F storage. There is no generic graph JSON or configurable pass order yet.

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

The same original input image is bound into all three stages. Per-shader
metadata controls are rebound before each stage because effect parameters
can be reset by OBS technique loops. A real libobs test checks that
a saved output-gain control actually changes the final rendered pixels.

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
There are still only four user-configurable input slots; independent
named resources are a future extension, not provided by this change.

## Resource allocation budget

Graph color-target payload is capped per filter instance at 512 MiB.
The optional shader declaration `uniform int hs_graph_budget_mib = 64;`
can request a smaller cap (integer 1 through 512). This is immutable
metadata, not a live control. The calculation reserves the full-size
input capture, both full-size compositor surfaces, a transparent pixel,
and both ping-pong textures for each intermediate buffer using its
actual format and rounded dimensions. Integer arithmetic rejects overflow.

The cap is a conservative logical texture-payload estimate, not a GPU
VRAM measurement or global limit. Driver allocation overhead, input
channels, ML resources, other filters and OBS's output target are not
included. An over-budget graph releases its graph targets and passes the
upstream image through without silently changing its resolution or
precision. Logging occurs once per rejection episode. When the source
size fits again, fresh histories are allocated; old state is not reused.
Existing full-resolution 4K SDR and RGBA16F graphs fit the default
budget; some 8K graphs now require smaller intermediate buffers.

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

    cmake --build build_x86_64 --target hyperShaderFX --parallel 4
    scripts/verify-two-buffer-graph.sh
    scripts/verify-two-buffer-example.sh

The graph test opens the actual HyperShaderFX module, renders moving
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
- Not wired to the standalone source or transition lanes.
- The legacy hs_history feedback path is separate; graph effects
  use hs_buffer_a_previous / hs_buffer_b_previous.
- Real-time full-resolution performance, HDR correctness on other
  GPU drivers, and Windows/macOS were not established by Mesa tests.
- No arbitrary pass graph, dynamic/adaptive per-pass resolution, general
  compute shaders, or spatial ML input integration yet.
- Graph-enabled audio/animated channels and per-pass audio
  controls need dedicated integration regression coverage.

- The budget is per-filter logical graph color payload only; aggregate VRAM admission and backend-specific overhead are not accounted for.
