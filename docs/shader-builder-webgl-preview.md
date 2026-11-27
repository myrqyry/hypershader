# Standalone visual node builder: WebGL2 preview

The experimental browser-based node builder at `tools/shader-builder/index.html`
now renders an actual live preview. It uses WebGL2 and a generated GLSL
shader for a **supported subset** of the graph nodes, derived from the same graph
used for the native `.effect` export.

The main OBS plugin remains native. No React runtime, `react-shadertoy` package,
or JavaScript engine has been added to the OBS graphics path. This standalone
preview uses a **test texture** rather than an actual OBS video source;
pixel identity and timing are not guaranteed to match OBS.

## Boundaries

- Native export uses `../stdlib/hypershader_standard.glsl` from its export
  directory (`data/shaders`), with `VSDefault` as its vertex entry point.
  The native foundation already defines `textureSampler`.
- The WebGL2 preview supports built-in inputs, texture sampling, standard GLSL
  arithmetic functions and the explicit `uvSwirl` adapter. Other native
  standard-library functions and MediaPipe nodes produce **visible unsupported
  warnings** when they feed the active output; disconnected native-only nodes
  do not block the preview. Export remains available.
- Preview shader compilation reports actual browser GLSL compiler errors.
  Successful preview compilation **does not establish** native OBS compatibility.
- The test texture is deliberately labeled in the UI, and its sampled colors
  are procedural. Live OBS inputs, multi-pass simulation preview, audio uniforms and
  precise rendering parity are not provided by this browser surface.
- The Qt dock's editor is separate and unchanged. The browser-based graph
  builder is not automatically embedded in that dock.

## Persistent render-pass authoring

The standalone visual builder now has a **Render passes** section. Add one to
eight named stages, select a stage to edit its own node graph, move it earlier
or later, and choose a resolution divisor (1, 2, 4, 8) and storage format
(source format or RGBA16F). The final Draw graph is always last and full-sized.
Each stage must have a connected Final Output.

The Input palette gains Pass: Latest Result, Pass: Previous Frame, Pass:
Resolution, and Graph Frame nodes. Select the referenced buffer in the node
properties. Optional connected UV coordinates can offset sampling; otherwise
the current pixel UV is used. Latest Result means the current result of an
earlier pass and the previous-frame result of this or a later pass. Previous
Frame is explicit history regardless of order. A referenced pass cannot be
removed until the references in other stages have been removed.

Export generates the native uniform declarations and ordered techniques for
the entire project. The saved .hsgraph format uses version 2 for persistent
projects and preserves each stage's node graph and descriptors; ordinary
single-pass projects still use version 1. Incomplete projects remain saveable,
but invalid native export is blocked. WebGL2 does not simulate persistent
passes; the editor explicitly directs the user to native OBS for preview.
The native Qt code editor remains separate.

For a quick feedback loop, add a pass called accumulate, wire its Previous
Frame node into a Math Add node (B = 0.01), then into Final Output. In Draw,
wire a Latest Result node referring to accumulate into Final Output. Export
the .effect to the plugin's data/shaders directory so the standard library
include resolves. Select it in a hypershader Source to see accumulation.
See [native graph contract](TWO-BUFFER-GRAPH.md).

Verify the actual generated effect with
`scripts/verify-persistent-builder-obs.sh /path/to/hypershader.so`. The test
builds a two-stage graph with the real generator and checks temporal output
pixels in an isolated native OBS source.

## Traceable node code

The existing visual graph generates both native OBS `.effect` files and an
explicitly limited WebGL2 preview. Each emitted node code section is now
bracketed with `// @node <id> <type>` and `// @endnode`. These breadcrumbs
survive export and identify the graph node responsible for the surrounding
shader statements without introducing a second runtime or modifying the
saved graph format.

When the WebGL2 fragment compiler reports a line number, the preview maps
that line to its enclosing generated node section and appends the node ID and
type to the compiler error. Errors in header/boilerplate, shader linkage,
missing driver line numbers, and unsupported native-only nodes do not receive
fabricated node attribution. Native OBS output carries the comments for
inspection, but native compiler diagnostics are **not** automatically mapped.

This attribution is inspired by the provenance comments in
[satoruhiga/TouchDesigner-ShaderBuilder](https://github.com/satoruhiga/TouchDesigner-ShaderBuilder),
not a port of its TouchDesigner context/generator or GLSL code.

## Verification

```sh
npm run verify:shader-builder
npm run verify:shader-builder:obs
bash scripts/verify-shader-builder-browser.sh
python3 scripts/verify-native-boundary.py
```

The first test covers native effect output and browser GLSL conversion.
The second compiles **three generated native effects** in an isolated real libobs
OpenGL context under Xvfb, without installing or opening the user's OBS.
The third checks rendered WebGL2 pixel values, live changes, compile errors,
and the default builder graph in headless Chrome.

To explore manually, serve `tools/shader-builder` with a local static HTTP
server and open `index.html` in a WebGL2-capable browser.
