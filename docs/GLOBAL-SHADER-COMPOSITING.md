# Global shader time and source compositing

Native HyperShaderFX render controls, available to filters, standalone sources,
and transitions. These are **host-level** controls; do not add another copy of
the controls to every shader, or replace/rename existing shader resources.

## Controls and defaults

- **Global Animation Speed** (0–10, default 1): changes iTime, iTimeDelta, and
  virtual iFrame. Reset Timer resets both iTime and virtual iFrame.
  It does **not** freeze physical scene frame ingestion or iGraphFrame:
  those counters must still initialize/reset stateful shader buffers. A shader
  using wall-clock time, physical feedback, or custom speed uniforms may
  exhibit activity at host speed zero.
- **Base Opacity** (0–1, default 1): multiplies the alpha of the incoming
  unmodified video for filters, or source A for a transition. In standalone
  generated sources, there is no incoming base layer, and this property
  intentionally cannot alter the output. Scene-item opacity/blending in OBS
  can operate on the resulting source.
- **Effect Opacity** (0–1, default 1): multiplies the shader-output alpha,
  independently of Base Opacity.
- **Effect Blend Mode**: Normal (0), Add (1), Screen (2), Multiply (3),
  Overlay (4), Difference (5). Blends the rendered effect **against** the
  base, then alpha-composites the two weighted layers using Source Over.
  This is one relationship between the two layers, not two separately
  meaningful blend modes against an unknown third framebuffer layer.

The default Normal, Base=1, Effect=1 configuration preserves the previous
unmodified fast path and saved-scene behavior. Only when compositing settings
change does the engine run a second offscreen draw and compositor.
All new compositing happens **after** shader simulation/history writes, so
opacity cannot accidentally change the accumulated temporal feedback.

Real GPU tests exercise source, normal filter, two-buffer graph, prior-frame
feedback, and manual OBS transition rendering; no mock compositor.
The MediaPipe capture-and-dispatch path was refactored into a shared helper:
the legacy default path continues to capture the implicit OBS input after
process_filter_begin; the new compositing path explicitly samples the
independently captured source texture. This path compiles but actual
MediaPipe inference with a live model remains unverified.

## OBS settings layout and optional shader inputs

Animation speed, timer reset, base/effect opacity, and effect blend mode now
sit together under **Global Overrides**, after the shader's own controls.
They keep their existing saved setting names (\`speed\`, \`reset_time\`,
\`compose_base_opacity\`, \`compose_effect_opacity\`, \`compose_blend_mode\`).
Shaders may also define local speed controls; global speed multiplies their
host time, rather than replacing their own per-shader settings.

The host-owned uniforms \`iProgress\` and \`iScale\` are never user-facing
shader parameters. The transition renderer still sets progress, and the
rendering engine still supplies scale as before.

**Texture Input 0–3** are *optional shader sampler slots*, not the incoming
base layer or a source list. A shader that references one can receive an
image file, another OBS source, or an audio spectrum; **None** leaves the
sampler unconfigured. The group only appears when executable shader code
references the corresponding \`iChannelN\`. Metadata scanning disregards
comments, unused uniform declarations from the included standard library,
and preprocessor directives.
The old misleading "Active" label has been removed.

The OBS source selectors for Texture Input 0–3 and Audio Source show scene
and group paths, including nested scenes. Unplaced sources are still listed,
and a source shared across multiple scenes is listed once: these inputs bind
the underlying source rather than a particular scene item. Audio and texture
pickers filter by the relevant OBS output flags.

New selections store a versioned source UUID in the existing setting; a
rename does not change the target, and an unavailable UUID does not resolve
to a different source. Historical name-based settings remain readable and
are not rewritten when opening properties. Missing saved selections stay
visible. Do not downgrade a scene with newly selected UUID-backed inputs to
older plugin builds that only understand source names.

Native regression: \`scripts/verify-shader-ui-boundaries.sh\` opens actual
HyperShaderFX Source instances after asynchronous shader loading and
inspects the real OBS nested properties, input picker switching, and
preserved default values. This is a UI contract test, not a screenshot
from the actual user's Qt theme.

## Shader Information

The read-only **Shader Information** panel has separate **Notes**
(description and any author-supplied technical details) and **Credits**
(author, copyright, upstream link, license) sections. It reads the selected
shader's actual metadata header; it does not create a personal notes editor.
Previously stored shader_notes_* OBS settings remain untouched.
See docs/SHADER-NOTES-CREDITS.md for metadata tags and tests.

## Reviewed role labels

The shader browser uses a small explicit set of reviewed procedural generators
whose proper display prefix is source..
These are catalog *labels*, not automatic conversion into new plugin types.
Shader paths and preset IDs are unchanged. The role list is evidence-backed but advisory; shaders
with resource errors are not thereby considered usable.

## Verification

- scripts/verify-global-compositor.sh
- scripts/verify-boolean-defaults.sh
- scripts/verify-filter-ui-obs.sh
- scripts/verify-two-buffer-graph.sh
- scripts/verify-creative-graph-shaders.sh
- scripts/verify-source-feedback-cycle.sh
- scripts/verify-shader-manager-catalog.sh

Source example: Blurred Gradient is a *working* standalone animated procedural
source (real libobs OpenGL). An iFrame-only shader also responds to global
speed, proving the global control does not depend on iTime.

## Cautions

These tests use real libobs but headless Mesa at 240x136, and they do not
establish 1080p/4K GPU costs, complete HDR pipeline correctness, GUI visual
taste, or Mac/Windows parity. Animation speed and transparency controls are
persisted with each filter/source/transition instance, not synchronized across
all instances. A source's alpha is preserved at its legacy full-opacity
default. Do not replace the running plugin binary while OBS is open.
