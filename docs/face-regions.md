# Reusable facial regions

Available from the existing native stdlib/mediapipe.glsl include, using the existing landmark texture and iFacePresent. No additional inference or ABI.

- hs_face_point(id, resolution): pixel-space coordinate.
- hs_eye_open_ratio(left, resolution): aperture relative to eye width.
- hs_eye_visibility(left, resolution): blink/narrow far-eye fade.
- hs_eye_mask(uv, left, resolution): feathered open-eye interior from eight contour points.
- hs_face_tint_luma(color, luminance): luminance-preserving tint with gamut compression.

Left means the person's own left, not screen left. Supply source dimensions (iResolution.xy). Geometry works with base 468-point topology, not dependent on iris points. Eye masks are geometric visibility estimates, not true occlusion segmentation. Mask helpers return zero on missing tracking. Use on local regions rather than repeated full-screen passes where possible.
Independent implementation inspired by Lashline's eye geometry and color approach. Native libobs/OpenGL pixel probe: mask, blink, far-eye attenuation, tracking absence, rotation and tint luminance all passed. First fixture failed shader compilation due to mismatched vertex argument; then blank draw fixed by explicit render state. Existing face-surface and six Fun Face effect tests and native boundary passed. Run scripts/verify-face-regions-obs.sh, scripts/verify-face-surface-obs.sh, python3 scripts/verify-fun-face.py. The probe flattens actual stdlib includes and compiles them with libobs; full HyperShader dynamic include/runtime integration and real camera cosmetics still require qualification. No existing effect defaults changed, no install/push.
