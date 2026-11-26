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

## Data Source driver

The optional provider ABI comes from the local URL/API Source fork
(`obs-urlsource`, commit `8a014f3`). The copied public C ABI is in
`src/interop/urlsource_snapshot_v1.h`; no build-time provider dependency.

1. In URL Source, configure a request with a JSON Pointer selecting a single
   numeric or boolean value, and enable **Share parsed data with OBS plugins**.
   For a data-only provider remove output mappings. If needed, explicitly
   enable background operation so it continues polling while inactive.
2. In Play's Modulation group, choose a bounded float parameter, driver
   **Data Source · value**, provider and maximum age, then Bind.
   Refresh the panel if a provider was just created. The saved UUID survives
   renaming; a missing provider does not silently select another source.
3. The selected result is applied directly (true/false become 1/0), clamped
   to the declared range. Depth and frequency are not used for this driver.
   Only one modulation per instance is supported. Shader-owned audio
   mappings and host-owned uniforms are not targets for the new driver.

A lazily started worker reads at up to 10 Hz, with a fixed 128-byte buffer
for scalar JSON. The draw path uses a nonblocking cached read and a
monotonic expiry deadline. Unavailable, expired, nonnumeric, oversized,
nonfinite or contended reads use the saved base control. Unbinding stops
polling; the idle worker is joined at destruction. Bound instances continue
to poll their cached provider while hidden; the consumer never activates
the provider or performs networking. The default freshness limit is 5 s
(configurable 100–60000 ms); removal and opt-out are seen on the next
consumer poll, or earlier if the cached value expires. No claim of hard
real-time delivery. JSON strings, arrays, and objects are not coerced into
numbers; select the exact scalar on the provider. Other plugins can reuse
the same provider without duplicating the HTTP request. No provider
installation or configuration is performed by HyperShader.

## Verification

```sh
scripts/verify-performance-panel.sh /path/to/hypershader.so /path/to/data
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


### Data Source interoperability

```sh
scripts/verify-data-binding.sh /path/to/hypershader.so /path/to/hypershader/data /path/to/obs-urlsource.so /path/to/obs-urlsource/data
```

This starts a loopback HTTP server and loads both actual modules into
isolated libobs/OpenGL. It exercises the real Qt Play binding controls and
checks shader pixels through provider updates, clamp, wrong type, rename,
sharing opt-out, bool, stale and fresh values, removal and clear. The worker
fixture also tests inactive polling, mismatched UUID/age and scalar parsing.
The previous oscillator/PCM/Play tests remain separately runnable.
Linux Qt/no-ML only; other platforms and full production OBS sessions are
not qualified by these tests.
