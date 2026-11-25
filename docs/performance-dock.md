# HyperShader Play dock

## Design

One shared UUID-selected instance across Play, Library and Builder. Never
guess a live target. Read compiled native property schema only after the
selected logical path has completed compilation. Every edit checks the
current UUID, path and compile revision. A pending/failed compile disables
the editor. The normal OBS property panel remains available.

- Play: bypass, global time freeze/resume, typed controls and requested
  input routing; the status strip reflects actual recent render and PCM
  timestamps plus available Sight/texture data, not merely settings.
- Variations: bounded float sliders only, declared ranges and quantized
  random steps, per-control locks; no source routing, integer modes, or
  unbounded fields changed. Captured A/B and one-step undo are session-local
  and cleared on target or compile revision changes. Morph commits on
  release (keyboard changes commit directly) rather than causing expensive
  source updates on every mouse event.
- Modulation: one mapping per instance stored in scene settings and bound
  to the exact shader path. Oscillator is bipolar; measured signals add a
  nonnegative excursion as a fraction of the declared range. All outputs
  clamp to the uniform's declared bounds. Inactive signal returns to
  saved base; audio PCM freshness expires after 250ms. No new inference
  or capture path, no per-frame settings writes. Source bindings stay
  explicit. Sight motion reads the existing source-local producer and
  pinch reads the chosen channel; missing input remains at base.
- Library: compatible role-matched installed native effects load without
  copying into a managed revision. Existing exact-path async compiler
  tracking and rollback on failure are shared with Builder. Other files
  remain editable through Builder. No new assets or models shipped.

## Verification

```sh
scripts/verify-performance-panel.sh /path/to/hyperShaderFX.so /path/to/data
```

Real Qt widget + isolated libobs/OpenGL checks:

- typed native controls, bounded variations, locks and selected instance
  safety; A/B numeric interpolation and undo; bypass and freeze/resume;
- native status procedure; modulation UI bind and clear;
- actual shader pixel readback: saved base .25 (red=64), oscillator
  .75 (red=191), real selected PCM increases output (red=93 in this run),
  stale PCM and wrong shader return exactly to base;
- modulation-driven audio input picker visibility.

Full shipped shader corpus (34), live catalog, input visibility,
real PCM kick/snare, named multipass graph and existing dock/settings/builder
checks also passed on the staged Linux Qt/no-ML build. Direct Library load
uses the existing compiler/rollback infrastructure and was compiled and
statically checked; the full OBS dock GUI and live Sight modulation
producer have not been interactively exercised. Windows/macOS unqualified.

Not implemented: authored look collections, persistent A/B snapshots,
multiple simultaneous mappings, attack/release controls, animated library
previews, favorites/recent history, MIDI/hotkey mappings or a live GPU
preview surface. These are separate follow-up capabilities.
