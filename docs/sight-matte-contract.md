# Source-local Sight subject mattes

HyperShaderFX consumes the existing Sight mask without running a second
segmentation model. Place one enabled Sight Subject Cutout on the same
source, upstream of HyperShaderFX, select a compatible U2Net model and
use Original source in Sight's Diagnostic view. Enable HyperShaderFX's
Sight shared-snapshot switch. The landmark channel does not select a
matte. Keep unrelated geometry-changing filters out of the analysis
chain; mismatched, ambiguous, disabled, detached, misordered or expired
producer data is rejected.

Native shaders receive an inference-resolution, temporally stabilized
mask in iSegmentation. Check iSightMattePresent > 0.5 before sampling
the red channel at normalized source UV; otherwise preserve the original
image. An all-zero matte is valid when no foreground is detected.
Sight's GPU edge refinement and the native HyperShader face-region mask
are separate from this shared mask. The original bridge uses a 750 ms
freshness limit. Independent analysis snapshots are not frame-synchronized.

See [shader authoring](../data/shaders/SHADERS.md),
[Original Instruments](original-instruments.md), and
[depth relighting](sight-relighting.md) for related input contracts.
The renderer and model-backed checks need compatible built modules and
local model fixtures; those assets are not bundled in the curated
distribution. See the README verification section for scope and caveats.
