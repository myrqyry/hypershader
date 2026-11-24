# Two-buffer graph — native OBS effect runtime

## Available now
Opt-in fixed three-stage sequence, executed by the production HyperShaderFX filter:
1. Capture the unfiltered upstream image.
2. Run technique BufferA, with its own two ping-pong history targets.
3. Run technique BufferB, sampling the **current** BufferA and its own previous-frame state.
4. Run technique Draw, sampling both **current** A and B.
5. Swap both history targets only after successful rendering.

Existing one-pass effects keep their existing Draw and iPreviousFrame paths.
There is no generic graph JSON, configurable pass order, or independent per-pass resolution yet.

## Contract
A graph effect must declare all four texture uniforms iBufferA,
iBufferB, iBufferAPrevious, iBufferBPrevious, and techniques BufferA,
BufferB, Draw. A normal effect with only Draw is unchanged.

In BufferA, iBufferA and iBufferB refer to prior-frame buffers.
In BufferB, iBufferA is current frame A and iBufferB is prior-frame B.
In Draw, iBufferA and iBufferB refer to current frame buffers.
In all stages, iBufferAPrevious and iBufferBPrevious refer to the prior frame.

On the first frame after a shader change or resize, prior textures are
known black, never uninitialized GPU contents. Optional int iGraphFrame
starts at 0, resets on shader replacement or render-target resize,
and increases only after a successful graph frame. This is separate from OBS iFrame.

The same original input image is bound into all three stages. Per-shader
metadata controls are rebound before each stage because effect parameters
can be reset by OBS technique loops. A real libobs test checks that
a saved output-gain control actually changes the final rendered pixels.

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

Existing shader picker, P-frame, stdlib loader, source feedback and
Graph mode is intentionally opt-in.

## Current limits
- Not wired to the standalone source or transition lanes.
- The legacy iPreviousFrame feedback path is separate; graph effects
  use iBufferAPrevious / iBufferBPrevious.
- Real-time full-resolution performance, HDR correctness on other
  GPU drivers, and Windows/macOS were not established by Mesa tests.
- No arbitrary pass graph, adaptive per-pass resolution, general
  compute shaders, or spatial ML input integration yet.
- Graph-enabled effects with external channels or per-pass audio
  control need dedicated hardware/integration regression coverage.
