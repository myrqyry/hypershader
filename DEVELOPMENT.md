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
loads it through an isolated **private** HyperShaderFX filter, and reports the
result returned by the **real libobs effect compiler**. It does not install a
binary, attach a filter to a scene, or update the selected OBS source. The
validation revision is deleted when the private source is released; a failed
validation attempts a separate recoverable draft snapshot.

**Apply** remains a separate explicit action for the selected active HyperShader
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

To reproduce the isolated test, clone that repository outside HyperShaderFX,
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

On the original pinned corpus, the actual HyperShaderFX native compiler
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
scripts/verify-input-ui-obs.sh /path/to/hyperShaderFX.so /path/to/data
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
scripts/verify-live-shader-catalog.sh /path/to/hyperShaderFX.so /path/to/data
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
