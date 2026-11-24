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
  are procedural. Live OBS inputs, multi-pass effects, audio uniforms and
  precise rendering parity are not provided by this browser surface.
- The Qt dock's editor is separate and unchanged. The browser-based graph
  builder is not automatically embedded in that dock.

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
