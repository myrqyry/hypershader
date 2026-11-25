# Shipped shader correctness audit — 2026-11-24

## Scope and reproduction

All 34 first-party/current root effects from the standalone repository, not archived imports.
Target: Linux OBS 32/libobs OpenGL (software renderer), Qt-enabled build without native ML backend.
The corpus regression checks actual asynchronous loader success, framebuffer readback, advertised
role, audio/Sight/texture control visibility, selected no-signal pixel contracts, and template defaults.
It additionally exercises Playground mode changes with actual libobs pixel comparisons.

```sh
scripts/verify-shipped-shaders.sh /path/to/hyperShaderFX.so /path/to/data
```

All 34 passed the final corpus test. The following table distinguishes per-effect supplemental
coverage from the corpus test; it is not a claim that every control value or every hardware/backend
combination is verified. Sight-specific GPU fixtures supply controlled snapshots; they do not
constitute live camera/ML inference testing.

| Shipped effect | OBS role | External input controls | Supplemental evidence / limit |
|---|---|---|---|
| `audio_demo.effect` | source | Audio | Corrected inverted smoothstep bounds; annotated custom audio controls; host PCM/audio binding regression. |
| `filter.afterimage_press.effect` | filter | Audio | Native previous-output feedback and moving-image response; dedicated GPU stimulus/control tests pass. |
| `filter.contact_filaments.effect` | filter | Audio, Sight / shared analysis | Exact passthrough without tracking; synthetic hand snapshot and control response pass; live hand-producer behavior not verified. |
| `filter.face_mesh_wireframe.effect` | filter | Sight / shared analysis | Native compile; synthetic barycentric surface wireframe response and exact no-model/zero-strength passthrough. Actual face inference not verified. |
| `filter.ink_currents.effect` | filter | None | Native two-buffer temporal output, stimulus response, controls and bypass. |
| `filter.liquid_light.effect` | filter | Audio | Native two-buffer temporal output, controls and bypass; per-band audio contribution not isolated. |
| `filter.reactive_sprite.effect` | filter | Audio, Texture 0, 1, 2 | PCM-driven state selection, atlas cells and stale-input fallback with images. |
| `filter.ripple_glass.effect` | filter | None | Native two-buffer temporal output, stimulus response, controls and bypass. |
| `filter.sight_depth_glass.effect` | filter | Audio, Sight / shared analysis | GPU depth fixture, boundaries, matte fallback, alpha, debug and dry behavior; corrected native kick binding. |
| `filter.sight_flow_echo.effect` | filter | Sight / shared analysis | GPU flow fixture, advection, decay, missing-flow clear and bypass. |
| `filter.sight_flow_sparks.effect` | filter | Audio, Sight / shared analysis | GPU flow fixture, advection, decay, missing-flow clear and bypass; corrected native kick binding. |
| `filter.sight_flow_warp.effect` | filter | Sight / shared analysis | Synthetic flow displacement and zero-strength/absent passthrough; live producer not verified. |
| `filter.sight_hand_highlights.effect` | filter | Sight / shared analysis | GPU snapshot fixture and exact missing-tracking passthrough. |
| `filter.sight_hand_ripples.effect` | filter | Sight / shared analysis | GPU expanding/decaying wave fixture and exact missing-tracking passthrough; real gesture model test not run. |
| `filter.sight_motion_pulse.effect` | filter | Audio, Sight / shared analysis | Synthetic foreground matte/motion energy response, background preservation, absent and dry passthrough. |
| `filter.sight_relight.effect` | filter | Sight / shared analysis | GPU depth/normal, light direction, shadow, AO, matte, alpha, bypass and region tests. |
| `filter.sight_subject_frame.effect` | filter | Sight / shared analysis | Synthetic bounds/centroid markers, unchanged exterior and absent/dry passthrough; subject-statistics helper tests. |
| `filter.sight_text_scramble.effect` | filter | Sight / shared analysis | Synthetic mask: opaque black/noise inside, unchanged exterior and absent passthrough; text-mask helper tests. |
| `filter.topographic_etch.effect` | filter | Audio, Sight / shared analysis | Exact missing-depth passthrough and synthetic depth GPU response; live depth-producer behavior not verified. |
| `filter.vidicon_afterglow.effect` | filter | None | Native two-buffer temporal output, stimulus response, controls and bypass. |
| `ink_reaction_buffers.effect` | filter | None | Native two-buffer shader pixel readback and temporal change. |
| `optical_halation.effect` | filter | None | Native halo, warm fringe, intensity and zero-strength bypass. |
| `pframe_drift.effect` | filter | None | Native corruption, keyframe reset and zero-loss passthrough. |
| `playground.effect` | filter | None | Native solid background/channel separation, zero-strength pixelation bypass and active pixelation. |
| `shaderfilter_demo.effect` | filter | None | Compatibility example compiles and renders; corrected inverted smoothstep bounds. |
| `source.audio_spectrum_orbit.effect` | source | Audio | Real PCM, source defaults, frequency/gain response and transparency. |
| `source.brass_register.effect` | source | Audio | Native visible/evolving output and GPU audio/control response; individual band response not isolated. |
| `source.paper_tide.effect` | source | Audio | Native visible/evolving output and GPU audio/control response; individual band response not isolated. |
| `source.phosphor_scope.effect` | source | Audio, Texture 0 | Real PCM, frequency/gain response, disconnect/reconnect, graticule and transparency. |
| `source.reactive_sprite.effect` | source | Audio, Texture 0, 1, 2, 3 | PCM-driven states, image atlas cells, stale-input fallback and transparency. |
| `source.spectral_garden.effect` | source | Audio | Native visible/evolving output and GPU audio/control response; individual band response not isolated. |
| `source.spectral_strata.effect` | source | Audio, Texture 0 | Real PCM, frequency mapping, hold/erase/recovery, transparency; fixed transparent history seed. |
| `template.effect` | filter | None | Native nonzero speed/decay defaults and render. |
| `warp_tape.effect` | filter | None | GPU write, retain, hold, erase and reverse tests. |

## Defects found and addressed

- Native audio ownership: redundant metadata could zero the real kick uniform. Reproduced with 94 Hz PCM
  in the existing native audio onset probe, then prevented runtime-owned uniforms and non-float uniforms
  from receiving custom audio metadata writes. Removed the two redundant annotations.
- Template speed and decay were implicitly zero; explicit useful defaults now exercise the intended feedback.
- Audio demo and compatibility demo used reversed smoothstep bounds; now use defined ascending bounds.
- Playground chromatic aberration read the camera even when a procedural/solid background was selected.
  It now samples the selected background. Zero distortion strength is an exact bypass, including pixelation.
- Spectral Strata initially inherited opaque black from an unbound previous-frame sampler. The source
  feedback lane explicitly clears its initial/resized read history to transparent. The corpus now asserts
  empty alpha during initial silence; the real PCM history/hold/erase test still passes.

## Additional verification

- Current global compositor suite: three shipped filters, two shipped sources, a frame-counter
  fixture and transition use all passed. Updated the runner to use shipped effects rather than
  removed imported assets; no compositor assertions were relaxed.
- Original instrument GPU stimulus/control tests: six passed.
- Sight simple effects: all five previously unisolated active-input shader paths now pass
  with deterministic GPU textures/values; the shared Sight verification script includes them.

## Remaining qualification

No Windows/D3D11, macOS/Metal, HDR, live GUI acceptance, exhaustive parameter sweep, or native ML
model execution was performed. Sight inventory/client procedure, helper and GPU fixture tests passed;
the real gesture-model test was explicitly skipped because model/fixture paths were not supplied.
Sight-only filters with no valid producer are expected to pass through, not appear broken.
Audio source activation remains opt-in except existing audio preset defaults. Some source designs can
animate without PCM; this is intentional, but their individual band contributions were not independently
isolated in this audit. No new imported effects or models were added.
