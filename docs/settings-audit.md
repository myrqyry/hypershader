# Settings label/help truth audit

This pass verifies that UI text reflects **actual runtime behavior** for static controls in `hyper_ui.c`.

## Verification method

Commands used:
- `rg -n 'obs_properties_add_|obs_property_set_long_description|obs_data_set_default_' src/hyper_ui.c`
- `rg -n 'obs_data_get_(bool|int|double|string)\(settings, "' src/filter-hypershader.c src/hyper_audio.c`
- `rg -n 'audio_update_rate|audio_smoothing|time_scale|global_scale|showcase_|input_mode|enable_audio_meter|sound_mix|enable_sound' src/filter-hypershader.c src/audio/audio_processor.c src/hyper_ui.c`

## Control behavior + text alignment

| Control | Key | Behavior verified | Text update made |
|---|---|---|---|
| Animation Speed | `speed` | Multiplies `iTime`/`iTimeDelta` accumulation; unitless range `0.0..10.0`; `0` freezes animation; immediate apply. | Added tooltip with exact semantics, range, and edge behavior. |
| Global Scale | `scale` | Value is read into `s->global_scale` but not consumed by render path (currently no visual effect). | Label updated to mark unused; tooltip explicitly says currently no runtime effect. |
| Enable Showcase Comparison | `showcase_enabled` | Enables side-by-side original vs filtered render split; immediate apply. | Added locale key and tooltip to match behavior. |
| Showcase Wipe Position | `showcase_wipe` | Normalized split position `0.0..1.0` (unitless); immediate apply. | Added locale key and tooltip with units/range. |
| Pulse toggle | `enable_sound` | Read into `s->sound_enabled`, but not used to gate audio worker or render paths (legacy no-op today). | Label changed to legacy/no-op wording; tooltip explains compatibility status. |
| Output Audio Mix (legacy) | `sound_mix` | Stored/read and used in compatibility/preview paths; primary analysis path uses `audio_mix`. | Label changed to legacy wording; tooltip clarifies scope. |
| Pulse Input Source | `input_mode` | Switches mouse/global cursor/audio behavior; entering audio mode starts audio connection + worker, leaving audio mode disconnects/stops. | Added tooltip describing immediate switching behavior. |
| Show Visual Level Meter | `enable_audio_meter` | Toggles property-pane meter generation; refresh throttled (~12.5 Hz); immediate apply. | Added tooltip with throttling detail. |
| Mapping analysis feature | `mapping_<n>_feature` | Selects mapped driver value (RMS/Peak/Band0-7) for corresponding uniform mapping; immediate apply. | Added tooltip on each feature dropdown. |
| MediaPipe Graph | `mp_controlnet_model` | Runtime graph selection value is read and applied during updates; immediate apply. | Added tooltip describing role. |
| Audio Mix (Analysis) | `audio_mix` | Mix index used by OBS analysis callback; clamped to valid range; immediate apply. | Added tooltip clarifying this is the active analysis mix. |
| Audio source name override | `audio_source` | String is still read into runtime state; primary live capture still comes from mix callback path. | Removed “Deprecated” wording; replaced with legacy override wording + tooltip. |
| Analysis Rate | `audio_update_rate` | Setting is read, but current worker loop is fixed ~21 ms tick (setting currently informational/no-op). | Label and tooltip updated to avoid implying active rate control. |
| Analysis Smoothing | `audio_smoothing` | Exponential smoother coefficient used for level/peak/bands; range `0..1`; `0` immediate, `1` hold previous; immediate apply. | Label and tooltip updated with exact units and edge behavior. |
| Debug Test Tone | `audio_test_tone` | Generates fallback 440 Hz tone only when no OBS audio samples are available. | Added tooltip for exact trigger behavior. |
| Debug Logging | `audio_debug` | Emits throttled (~1 Hz) logs when analyzed values change significantly. | Added tooltip clarifying cadence/condition. |
| FFT Size | `audio_fft_size` | Triggers deferred async FFT reconfiguration (512/1024/2048/4096); affects latency/detail. | Added tooltip for effect and async apply behavior. |
| Window Type | `audio_window_type` | Triggers deferred async FFT/window reconfiguration; affects spectral leakage/shape. | Added tooltip for effect and async apply behavior. |

## Ambiguous or inconsistent settings (not behavior-changed)

- `scale` (`global_scale`) is currently **stored but unused** in rendering.
- `enable_sound` is currently **legacy/no-op** for core runtime processing.
- `audio_update_rate` is currently **informational** because worker cadence is fixed.

Smallest follow-up fixes (not implemented here):
1. Either wire these keys to runtime behavior, or hide them behind an “Experimental/Legacy” group.
2. If retained visible, keep explicit no-op wording to prevent user confusion.
