# Sight: Depth Relighting

Native HyperShaderFX filter using the existing source-local Sight depth bridge.
No second inference model, per-frame file IO, or extra feedback texture.

## Setup

1. Add **Sight: Depth Camera (Depth Anything)** to the camera or image.
   Select a compatible ONNX depth model and set Visualization to
   **Original source (share depth)**.
2. After the depth analysis filter in actual render order, add HyperShaderFX.
   Choose **Sight: Depth Relighting** (`root/filter.sight_relight.effect`).
   Enable **Use Sight shared snapshots (tracking, matte, text, flow, depth)**.
   Do not put an unrelated image treatment between the analysis filters
   and HyperShaderFX; it breaks the alignment contract.
3. Move Light X/Y (0–1 is inside the frame, extended to -1–2), select
   directional or point light, and adjust elevation, intensity and color.
   Point lights also have a radius. Lower ambient for dramatic lighting;
   opposing fill, rim and specular are independently controllable.
4. Set the Preview dropdown to Surface normals to inspect the inferred
   shape; increase Normal Sample Radius to reduce quantization noise or
   decrease Depth Relief for subtler gradients.

Optional **Subject only** and **Background only** require a source-local
Sight Subject Cutout producer in analysis-only Original source mode, on
the same source and upstream. Entire image needs depth only. A soft depth
band limits illumination by relative near/far depth.

## Contact shadows and ambient occlusion

Both are optional, default to zero, and use the existing height field.
The controls are grouped under **Occlusion**.

- **Contact Shadow Strength** attenuates the key diffuse and specular light,
  not the opposing fill or ambient. Twelve bounded depth samples trace
  toward the key; point lights stop at their projected position.
  **Shadow Reach** bounds the search in source pixels. **Shadow Bias**
  suppresses quantization/self-shadow artifacts; **Shadow Softness**
  softens the height-intersection threshold (not true area-light penumbra).
- **Ambient Occlusion Strength** attenuates ambient and opposing fill.
  Eight directions with two radii each compare nearby height samples to
  the local tangent plane, reducing self-occlusion on flat sloped surfaces.
  **Occlusion Radius** is in source pixels; **Occlusion Bias** suppresses
  small deviations from the plane.
- **Key visibility** and **Ambient visibility** preview the respective
  estimated visibility in grayscale. White is unoccluded, black is
  maximally occluded. The previews ignore the strength sliders but obey
  the depth band/region selection and overall bypass rules.

A starting point for a portrait is shadow strength 0.35, reach 48 px,
ambient occlusion strength 0.25, radius 24 px. Tune to the input size
and depth resolution. Disabled effects skip their additional sampling;
with both enabled the shader uses at most 28 additional depth taps per
pixel, in the same rendering pass. Actual frame-rate cost depends on
hardware and source resolution; no throughput benchmark is claimed.

## Suggested looks

- Soft portrait: point key, warm key color, ambient 0.7, opposing cool fill
  0.25, low relief and modest mix.
- Dramatic side light: directional, Light X at 0 or 1, lower elevation and
  ambient, opposing fill near zero, slightly raised relief.
- Colored accent: point key, saturated color, smaller radius, target a depth
  band or the subject. Keep ambient around 1 to retain existing illumination.

## Geometry and limitations

Sight supplies per-inference normalized inverse depth, higher is nearer.
The shader interprets it as an orthographic height field. Four depth taps
estimate slopes; discontinuity-aware one-sided differences reject samples
across strong edges, and low-confidence slopes taper toward a flat normal.
Normals are calculated in aspect-correct screen space. Directional and
soft point lighting use those normals; opposing fill, grazing-angle rim
and optional Blinn-style specular add artistic control. All color changes
are applied to the existing photographed RGB, not to recovered albedo.

This is depth-driven relighting, not a metric 3-D reconstruction, a full
scene shadow raytracer, an intrinsic image decomposition, or a way to
remove the original lighting. Screen-space shadow rays cannot see outside
the frame or behind visible surfaces, may miss thin occluders at coarse
sampling distances, and use relative depth rather than metric geometry. It cannot recover occluded surfaces. Model noise, depth
normalization drift and inference latency can affect visual stability.
The matte and depth are independent snapshots, not synchronized inference.

Missing depth or a required missing matte returns the original image.
Zero effect mix also bypasses exactly. Source alpha is preserved.

## Verification

- `scripts/verify-sight-relight.sh`: isolated OBS/OpenGL GPU fixture verifies
  slope orientation, light movement, point falloff, alpha, absent signal,
  zero mix, normal preview, silhouette discontinuities, subject/background,
  missing matte, colored key, fill, rim, specular and depth band. Additional
  occlusion cases verify directional shadows, receiver isolation, point
  light termination, local AO, both previews, alpha, flat/slope safety.
- `scripts/verify-sight-relight-model.sh SIGHT_MODULE SIGHT_DATA HYPER_MODULE HYPER_DATA DEPTH_MODEL`:
  loads the actual modules and local Depth Anything V2 model, checks rendered
  changes, light movement, conditional controls, independent shadow/AO
  activation, depth loss/recovery and zero mix.
  Requires compatible module runtime dependencies in the loader path.
  The verified fixture is a synthetic input image, not live camera footage.

The native GPU tests and model-backed filter-chain test ran on OBS 32.
No separate OBS 33 or interactive live-camera verification is claimed.
