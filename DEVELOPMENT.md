# Development Checks

For the new native OBS audio visualizer and its actual audio-to-GPU
integration test, see [Audio Visualizer](docs/audio-visualizer.md).

## Verify settings wiring

Run the lightweight settings verification script:

```bash
npm run verify:settings
```

What it checks (from `src/hyper_ui.c`):

- source-of-truth settings keys and types from defaults + runtime get/set usage
- UI-bound keys from `obs_properties_add_*` calls
- failures for:
  - settings keys without UI (except explicit internal allowlist)
  - UI keys that do not exist in source-of-truth
  - type mismatches between settings usage and UI widgets

To allow internal non-UI keys, add them to `INTERNAL_KEYS_WITHOUT_UI` in `scripts/verify-settings.js`.

## Native Builder validation and AI repair

The Qt Builder now offers **Validate Native Shader (no live changes)**.
It writes the current draft to an atomic, UUID-named temporary managed revision,
loads it through an isolated **private** hypershader filter, and reports the
result returned by the **real libobs effect compiler**. It does not install a
binary, attach a filter to a scene, or update the selected OBS source. The
validation revision is deleted when the private source is released; a failed
validation attempts a separate recoverable draft snapshot.

**Apply** remains a separate explicit action for the selected active hypershader
instance. The runtime returns compile success or the actual error through
per-source private settings keyed by the exact managed revision path. The
Builder refuses to attribute unrelated or superseded async results to the
current request. If an Apply compilation fails and its rejected revision is
still selected, it restores only the prior shader selection (file, preset,
version schema); other source controls are not reset. Applied revision files
remain available independently.

On a failed compile, **Prepare AI Repair** fills the local AI prompt with
compiler diagnostics and the unmodified draft. **It does not call the model**;
review the prompt and click **Generate** separately. This generates another
recoverable draft, not an automatic Apply. Repairs depend on the installed
local model; compiler errors alone do not prove correct rendered output.

Checks:

```sh
scripts/verify-builder-compile-feedback.sh   # real private libobs invalid/valid/stale paths
scripts/verify-builder-repair-prompt.sh      # real Qt native-repair prompt contract
npm run verify:dock                         # Qt UI wiring and nonautomatic Apply/repair contract
```

The isolated tests don't constitute a normal OBS GUI/NVIDIA smoke test or a
successful run of the local Qwen model. The native shader compile status also
does not substitute for visual render/pixel validation.

## Optional external WebGL2 shader import corpus

The small third-party [llm-shader-toy](https://github.com/johnPertoft/llm-shader-toy)
collection is a useful **adversarial compatibility input**, not an installed
shader pack. Tested upstream revision `be4440b7553275f3bec0a407ac70b65fde203efb`
(MIT; no upstream GLSL is vendored here): **25 shaders**, comprising 22
numbered AI-generated examples and three demos.

To reproduce the isolated test, clone that repository outside hypershader,
check out the pinned commit, and run:

```sh
python3 scripts/test-webgl2-import-probe.py
scripts/probe-webgl2-corpus.sh /path/to/llm-shader-toy/shaders
```

Set `HYPER_CORPUS_REPORT_DIR=/path/to/reports` to retain the tab-separated
per-file result tables. The opt-in runner uses its own temporary HOME and
Xvfb/libobs instance. It never copies examples into the plugin's installed
assets, touches scenes, or runs an LLM. `scripts/probe-webgl2-import.py` is a
*test-only* best-effort conversion of WebGL2 `main`, uniforms, fragment
coordinates, simple two-argument `atan`, and native wrapper boilerplate. It
is **not a production GLSL importer**, does not support arbitrary inputs or
helper-level `gl_FragCoord`, and cannot guarantee visual semantics.

On the original pinned corpus, the actual hypershader native compiler
reported **0/25** for raw WebGL2 files (`#version 300 es` is not a native
OBS effect directive). The test-only wrapper yielded **22/25** compilations;
the other three were `gpt-created-13` (matrix/rotation syntax),
`gpt-created-18` (fragment-coordinate scope in a helper), and
`gpt-created-3` (global `const` handling). These are observed against the
local Mesa llvmpipe OpenGL backend, not cross-platform promises.

For real GPU readback on one wrapped file:

```sh
scripts/probe-webgl2-pixels.sh /path/to/wrapped/gpt-created-1.effect
```

In isolated readback, `gpt-created-1` and `gpt-created-8` compiled and
produced nonuniform, nonblack pixels distinguishable from the test input.
`gpt-created-22` **compiled but output an all-black frame** in this fixture.
The pixel probe checks compiler success and measurable pixel output, **not**
visual equivalence to WebGL, temporal behavior or real OBS GUI/NVIDIA behavior.
Do not classify 22 compiled shaders as 22 correct visual effects.

## Shipped shader regression

Run the full current native catalog through the actual module and compare its
declared input requirements and fallback contracts:

```sh
scripts/verify-shipped-shaders.sh /path/to/hypershader.so /path/to/data
```

See [the per-shader audit](docs/SHIPPED-SHADER-AUDIT.md) for coverage and limitations.

## Shader-driven input controls

The first controls after Category, Shader and Custom File are the external
inputs requested by the successfully loaded effect. Audio, Sight and native
analysis, then numbered or named texture slots precede shader parameters and
global overrides. Unused controls are hidden. Texture slots selecting Audio
Spectrum also reveal the audio-source selector immediately.

The existing recursive metadata scanner recognizes executable native audio
and Sight identifiers and the audio_band helper, as well as explicit audio
annotations and the existing named/numbered texture contract. Shared face
and segmentation resources can use Sight or native analysis; the user's
provider selection is not changed automatically. Standard-library uniform
declarations, comments and shader-owned audio_gain parameters are not input
requests. This is conservative lexical discovery, not shader call-graph
analysis. The existing Sight channel inventory and audio opt-in remain intact.

```sh
scripts/verify-input-ui-obs.sh /path/to/hypershader.so /path/to/data
```

The isolated regression compiles real effects and switches a real filter
between different input contracts, checks actual libobs property order and
visibility, named labels, and audio-spectrum selection. It also verifies the
Face Mesh Wireframe shader's native sampler syntax. No models are required
to exercise the UI; inference quality and ordinary OBS GUI operation are
separate checks.

## Live shader catalog

The native preset/category dropdown and Shader Manager discover the current
installed shader folder, not a fixed inventory. In Qt-enabled OBS, the dock's
filesystem watcher coalesces changes and refreshes native source properties
on the UI thread, even when the dock is hidden. New files, removals, renames,
metadata edits and atomic editor saves are covered. Reopening native properties
or changing categories also rescans the folder, including in non-Qt builds.
The watcher does not choose or apply a different shader. Existing saved
relative-path preset IDs stay stable; a removed active shader is not replaced
automatically. The runtime picker intentionally offers native .effect files,
while Shader Manager can also browse its separate draft/import categories.
Preview, test and platform collections are excluded from the installed library.

Run the regression against a staged build (the harness copies data into a
disposable directory and does not touch the user's OBS installation):

```sh
scripts/verify-live-shader-catalog.sh /path/to/hypershader.so /path/to/data
```

This exercises real libobs properties, update-properties signals, and the Qt
watcher/catalog on Linux. Native GUI restart and other OS backends still
require their own acceptance checks.

## Verify Shader Manager discovery

The Qt dock browses shader files in `data/shaders/` (loose),
`data/shaders/root/` (library), `data/shaders/builder-applied/` (revision
files), and the existing quarantined directories. Browsing is **not**
a statement that a shader compiles. The verified-only runtime preset picker
has a separate contract. Refresh keeps the selected shader instance and
file when they still exist.

```bash
scripts/verify-shader-manager-catalog.sh
```

This compiles the actual Qt catalog helper, tests all directory categories
in a temporary hierarchy and verifies that both source and installed shader
libraries are discoverable. It does not alter OBS state.

## ONNX Runtime MediaPipe Build

When `USE_ONNX_RUNTIME=ON` is enabled with valid `-DONNXRUNTIME_INCLUDE` and
`-DONNXRUNTIME_LIB` paths, the plugin compiles with `USE_ONNX_MEDIAPIPE`
defined, linking against libonnxruntime for real MediaPipe inference.

Deploy the ONNX Runtime shared libraries **matching the version and ABI linked
by the module**; do not hard-code `libonnxruntime.so.1.17.1` for every build.
For an explicit absolute `ONNXRUNTIME_LIB` path, the CMake install rules
collect `libonnxruntime.so*` and any `libonnxruntime_providers_*.so` files
from that library's directory. Package-discovered dependencies may require
separate runtime staging. Set `-DCMAKE_INSTALL_RPATH=\$ORIGIN` in a shell
command for a local bundled runtime, then inspect the installed module's
`RUNPATH`/`RPATH` and dependencies (for example, `readelf -d` and `ldd` on
Linux). Build-time success alone does not establish deploy-time loading.
See README.md for the local install steps.


### Native inference readback

Native tracking admits one capture/result at a time and drops repeated draws
of the same OBS video timestamp before scaling or staging. The slot is not
released until the result upload is consumed (or the capture/inference fails).

When Linux's OpenGL development package provides OpenGL::OpenGL, the
module links the GLVND OpenGL dispatch library and uses a fence in the
current OBS graphics context. The staging buffer is mapped only after a
zero-timeout readiness poll signals completion. A pending capture expires
after 250 ms rather than delivering an old frame. Unsupported backends and
builds without that optional dependency retain the bounded synchronous
staging path and log it explicitly. This is not a guarantee of zero driver
overhead or nonblocking behavior on D3D11/Metal.

For actual ONNX face tracking verification, use
scripts/verify-mediapipe-filter-obs.sh with the staged module, a public
256x256 RGBA face fixture, and the probe effect from staged data. Set
HS_MP_TEST_DATA to that data directory (including the paired model graphs
and external data) and HS_MP_TEST_EXPECT_FENCE=1 to require GL readiness
polling. Set HS_MP_TEST_COMPOSITE=1 to exercise explicit composited input.
The Linux probe exercises real pixels and deliberately delays fence
readiness and result publication; it does not measure production frame time.

### Inactive workload scheduling

Visible auxiliary/preview views count as demand just like program output.
Filters use OBS parent show/hide and attach/detach notifications (private
filter show_refs are not equivalent to the parent's). A valid direct draw
renews a 250 ms demand lease. Disabled effects cannot renew it.
The video tick leaves animation time and persistent shader history alone,
but skips Sight reads and entity physics while inactive. Cached Sight
presence is invalidated until a fresh snapshot is read on resume.

Audio callbacks remain registered for safe source lifetime and cheap resume,
but reject PCM before conversion while inactive. The analysis worker sleeps
at a 100 ms maintenance cadence instead of 21 ms; it still detects removed
audio sources. An activity epoch and callback-inflight count allow the
worker to discard queued PCM and clear feature, spectrum and waveform
state before reopening admission. No thread join or worker mutex wait is
introduced on the visibility or render path. FFT reconfiguration can be
deferred until demand returns. Sound-test preview is independent.

Native MediaPipe capture is draw-driven. A generation accompanies the
capture, staged readback, inference task and queued graphics publication.
Suspending the workload or changing its requested graph/provider invalidates
old generations; the next draw discards an old fence and binds no old
landmark/mask/face-surface data. The single capture reservation stays with
its current owner until release, even when its generation expires. Graph
changes and graph restart happen on the inference worker, not in the
settings callback. An already running model invocation can finish, but its
stale result is dropped. Model resources and compatible GPU allocations are
retained; an explicitly captured reference texture is not transient live
tracking data. Initial model loading remains synchronous.

With the real ONNX face model staged as described above, set
`HS_MP_TEST_EPOCH=1` on `verify-mediapipe-filter-obs.sh` to hold a completed
face result across disable/re-enable, provider changes, graph changes and
hide/show. The probe checks both pre-upload and post-upload pixels on blank
input and verifies fresh inference after all four changes.

Run the isolated native lifecycle/PCM/Sight-provider fixture:

    bash scripts/verify-inactive-workload.sh MODULE DATA

This test uses real libobs audio capture and native rendered pixels; the
Sight provider is a deterministic test endpoint (not real inference).

### Shared shader library cache

The native preprocessor keeps an immutable per-file include plan and original
text for canonical .glsl files beneath the installed stdlib directory. The
metadata scanner reads the same cached text, but retains its own metadata
parsing semantics. Include names remain unresolved in each plan; every
compilation resolves them against current files and its own include guard.
Managed Builder revisions and root effects are not cached.

On POSIX, file identity, size, mode and nanosecond modification/change times
are checked on each lookup. Missing, malformed, unstable and oversized
entries are not retained. A shared 64-entry / 2 MiB retained-buffer LRU
releases its references on module unload. Concurrent cold misses may read
independently; cache I/O and parsing occur outside the cache mutex. Windows
uses the uncached path until a high-resolution Windows identity check is
implemented. The cache does not introduce a file watcher or force an
existing effect to recompile; changes become visible on the next compile
(including Builder's revisioned Validate/Apply). Metadata and compilation
are not a transactional snapshot across concurrent file edits.

Run the isolated native regression on Linux:

    bash scripts/verify-stdlib-cache.sh MODULE DATA

The test interposes real file opens, checks rendered pixels and metadata
defaults, same-size edits with restored mtime, missing/restored dependencies,
changed include order, managed revisions, and LRU eviction.
