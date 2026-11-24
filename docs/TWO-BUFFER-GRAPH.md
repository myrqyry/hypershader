# Two-buffer graph — native OBS effect runtime

## Available now
Opt-in fixed three-stage sequence, executed by the production HyperShaderFX filter:
1. Capture the unfiltered upstream image.
2. Run technique BufferA, with its own two ping-pong history targets.
3. Run technique BufferB, sampling the **current** BufferA and its own previous-frame state.
4. Run technique Draw, sampling both **current** A and B.
5. Swap both history targets only after successful rendering.

Existing one-pass effects keep their existing Draw and iPreviousFrame paths.
BufferA and BufferB support independent bounded resolutions and optional RGBA16F storage. There is no generic graph JSON or configurable pass order yet.

## Contract
A graph effect must declare all four texture uniforms iBufferA,
iBufferB, iBufferAPrevious, iBufferBPrevious, and techniques BufferA,
BufferB, Draw. A normal effect with only Draw is unchanged.

In BufferA, iBufferA and iBufferB refer to prior-frame buffers.
In BufferB, iBufferA is current frame A and iBufferB is prior-frame B.
In Draw, iBufferA and iBufferB refer to current frame buffers.
In all stages, iBufferAPrevious and iBufferBPrevious refer to the prior frame.

On the first frame after a shader change or resize, prior textures are
initialized to opaque black at their actual configured sizes and formats, never uninitialized GPU contents or a one-pixel substitute. Optional int iGraphFrame
starts at 0, resets on shader replacement or render-target resize,
and increases only after a successful graph frame. This is separate from OBS iFrame.

The same original input image is bound into all three stages. Per-shader
metadata controls are rebound before each stage because effect parameters
can be reset by OBS technique loops. A real libobs test checks that
a saved output-gain control actually changes the final rendered pixels.

## Independent buffer size and precision

The following optional shader declarations are allocation descriptors, not
user controls. They are read once at shader load, before allocating graph
resources, and never from saved slider values.

```hlsl
uniform int iBufferADivisor = 2; // half width and height
uniform int iBufferBDivisor = 4; // quarter width and height
uniform int iBufferAFormat = 1;  // RGBA16F signed/HDR data
uniform int iBufferBFormat = 1;
uniform float3 iResolution;
uniform float3 iBufferAResolution;
uniform float3 iBufferBResolution;
uniform float3 iGraphOutputResolution;
```

Divisors must be 1, 2, 4 or 8. The default is 1; each dimension is
rounded up independently. Format 0 (default) follows the upstream
capture format; 1 explicitly uses RGBA16F. Invalid descriptor types,
missing initializers or values cause the graph to pass the upstream
source through with a logged diagnostic. The final Draw stage remains
full resolution and uses the source/output format. The original image
remains full resolution in every stage.

All resolution vectors contain width, height and aspect ratio. Within
BufferA/B, iResolution is that pass's actual target size; within Draw it
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

## Resource allocation budget

Graph color-target payload is capped per filter instance at 512 MiB.
The optional shader declaration `uniform int iGraphBudgetMiB = 64;`
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
- The legacy iPreviousFrame feedback path is separate; graph effects
  use iBufferAPrevious / iBufferBPrevious.
- Real-time full-resolution performance, HDR correctness on other
  GPU drivers, and Windows/macOS were not established by Mesa tests.
- No arbitrary pass graph, dynamic/adaptive per-pass resolution, general
  compute shaders, or spatial ML input integration yet.
- Graph-enabled effects with external channels or per-pass audio
  control need dedicated hardware/integration regression coverage.

- The budget is per-filter logical graph color payload only; aggregate VRAM admission and backend-specific overhead are not accounted for.
