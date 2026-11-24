# Portable native HyperShaderFX effect bundles

This is an **offline pack/unpack tool**, not an OBS plugin importer or a new
shader dialect. It packages an existing native `.effect`, its **quoted**
`#include` dependencies under a bounded library root, and any explicitly
supplied texture/LUT assets, into a deterministic `.hsfx` ZIP archive.

The manifest records a format version, native entry path, and SHA-256 per file.
Unpack verifies the declared files and hashes before writing anything. It
rejects absolute/traversal paths, symlinks, duplicate entries, oversized data,
and pre-existing destinations. It creates a *new* directory, not an installation.

## Example

From the repository root:

```bash
python3 tools/effect-packager/pack.py pack \
  data/shaders/root/hologram.effect /tmp/hologram.hsfx --root data

python3 tools/effect-packager/pack.py unpack \
  /tmp/hologram.hsfx /tmp/hologram-unpacked
```

The unpacked tree contains the relative paths used by the shader:

```text
/tmp/hologram-unpacked/
  shaders/root/hologram.effect
  stdlib/hypershader_standard.glsl
  stdlib/hologram.glsl
```

If a shader uses an image, LUT, or other asset without a quoted shader
`#include`, add `--asset path/to/asset.png` for each such file. The tool
does **not** infer paths from shader string literals or attempt to bundle
external models, ONNX runtimes, binary plugins, or arbitrary dependencies.

Test it with `python3 tools/effect-packager/test_pack.py -q`.

## Boundaries

- No archives are loaded by OBS automatically. You can inspect the unpacked
  native effect and assets before placing them in a HyperShader library.
- Shader includes must remain under the provided `--root` tree.
- Existing folders are never overwritten; extracting does not modify OBS
  configuration or user shader catalogs.
- Packaging an effect doesn't prove it compiles or renders. Test the unpacked
  shader in the native HyperShader runtime.
- This format is HyperShaderFX-specific; it is not a `.shadertastic` importer.
- The package tool does not include any third-party shader licenses unless you
  explicitly add the relevant license file via `--asset`. Preserve licenses and
  attribution when distributing third-party effects.
