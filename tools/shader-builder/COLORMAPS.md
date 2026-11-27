# Named colormaps for the hypershader visual builder

Four selectively adapted scalar → RGBA colormaps from [kbinani/colormap-shaders](https://github.com/kbinani/colormap-shaders), upstream commit `3ba464481acfdd30bb33ac00503190d99398979f` (MIT, © 2015 kbinani). Full upstream notice is preserved at [`data/stdlib/COLORMAPS-LICENSE.txt`](../../data/stdlib/COLORMAPS-LICENSE.txt).

## Available nodes

In the standalone visual builder's **Colors** category:

| Node ID | Label | Source |
| --- | --- | --- |
| `colormap_jet` | Jet (Scientific) | `shaders/glsl/MATLAB_jet.frag` |
| `colormap_spectral` | Spectral | `shaders/glsl/IDL_CB-Spectral.frag` |
| `colormap_lava_waves` | Lava Waves | `shaders/glsl/transform_lava_waves.frag` |
| `colormap_space` | Space | `shaders/glsl/transform_space.frag` |

Connect a scalar value to the **Value** input (expected 0–1) and use the RGBA **Color** output. The scalar may be a static value or come from another node. These mappings are not image-wide grading passes; callers must supply an intensity/depth/noise or other normalized scalar.

This extension does **not** replace the existing `rainbow`, `heatmap`, and IQ `palette` functions.

## One canonical shader source

- `tools/shader-builder/nodes/colormap-kernels.js` contains the upstream GLSL formulas, with only function names renamed to be collision-safe. It is the canonical checked-in formula source.
- `scripts/generate-colormaps.js` deterministically emits `data/stdlib/colormaps.glsl`. Both the native and browser kernels come from the same formulas, not independently hand-maintained algorithms.
- `data/stdlib/hypershader_standard.glsl` provides the OBS-compatible `vec4 → float4` macro, and generated native effects include `../stdlib/colormaps.glsl`.
- The browser preview loads `nodes/colormap-kernels.js` and includes only the colormaps actually reachable from Final Output. No silent preview fallbacks.
- The native function names are `colormapJet`, `colormapSpectral`, `colormapLavaWaves`, and `colormapSpace`.

To regenerate or check parity:

```bash
node scripts/generate-colormaps.js
node scripts/generate-colormaps.js --check
node scripts/verify-colormaps.js
npm run verify:shader-builder
npm run verify:shader-builder:obs
bash scripts/verify-shader-builder-browser.sh
python3 scripts/verify-native-boundary.py
```

Native verification runs isolated libobs/Xvfb/Mesa compile checks and **actual stage-surface pixel readbacks** for each colormap; browser smoke compiles real WebGL2 and reads pixels before/after changes in the input. Default generated effects use a static midpoint scalar (0.5); the preview smoke also varies the scalar (0.2 and 0.8).

Native midpoint readback on 2026-10-29 (RGBA8):
- Jet: 128,255,128,255
- Spectral: 255,255,191,255
- Lava Waves: 124,72,131,255
- Space: 137,0,104,255

## Constraints

These four source formulas use direct shader math, not textures or external services. Each exported function expects an input in 0–1; behavior outside that range follows the upstream formula and should not be assumed consistent across every palette. Jet and Spectral are not perceptually uniform; choose an appropriate color scheme for accessible scientific interpretation.

Verified on Linux Mint with the installed libobs OpenGL backend (Mesa llvmpipe) and Chrome WebGL2. **Not verified** on Windows/D3D11, macOS/Metal, the user's everyday running OBS instance, mobile browsers, or long-running shader workloads. This local source integration does not install/replace plugins, scenes, or presets.
