# Sight: Baked PBR

A separate native filter, `root/filter.sight_baked_pbr.effect`, applies a
verified local HypBR material to Sight's semantic categories. It does not
change the existing nine-style Semantic Materials effect or its saved settings.

## Use

1. Configure Sight semantic analysis on the original camera/image source.
   Optional analysis-only depth, subject matte and linked optical flow
   producers must operate on the same source before HyperShader.
2. Select **Sight: Baked PBR**, enable the shared Sight snapshot consumer,
   and set **Texture Input 0** to Image File. Select the bundled
   `materials/classic_red_brick/atlas.png` in HyperShader's data directory,
   or a packed atlas generated as below.
3. Set category IDs (A, B, C in priority order, -1 disables a slot), tint
   (white retains baked color) and mix. All three categories share the same
   atlas in this filter instance. Tile Repeat controls screen-space repeat;
   Normal Detail and Relight Strength control the texture-light response.
4. Persistent Surface Coordinates defaults to full lock when a linked
   flow-derived coordinate field exists. Coordinate offset/confidence views
   help diagnose tracking; fallback uses region-local coordinates. Depth
   and optional matte clipping reuse the same semantic helper contracts.

## Atlas

OBS/libobs supports eight simultaneous texture bindings. The existing
Semantic Materials shader already consumes eight, so attaching three new
map textures was rejected after a native probe logged GL_INVALID_ENUM.
This dedicated variant uses exactly eight: image, semantic mask/bounds/meta,
depth, matte, persistent UV and one atlas. Instantaneous flow deformation
is not an additional sampler; the host still transports persistent UVs
with linked flow. The original effect stays byte-identical.

The bundled atlas packs 512px albedo (sRGB), normal (OpenGL +Y) and linear
ORM (R=AO/G=roughness/B=metallic) horizontally, each with a one-pixel
periodic gutter on every side. Dimensions are 3*(N+2) by (N+2). The
shader repeats inside each subimage instead of sampling adjacent maps.
The host's actual image dimensions are used to reject missing or
wrong-shaped atlases. This is an offline generated layout, not an
additional image-decoding path or CPU processing on the render thread.

Generate a verified material bundle using the Sand3D Studio baker and then
pack it with Python + Pillow (e.g. the installed HypBR environment):

```sh
python scripts/pack-hypbr-atlas.py /path/to/bundle /path/to/new-atlas.png
```

The packer checks the pinned engine, receipt QA, source dimensions and
SHA-256 for all three maps; refuses overwrite. Supported resolutions are
512/1024/2048. The latter two have layout support but only 512 has native
render qualification. Bundled source maps and material receipt are from
HypBR commit 83a0cb859ad03324185632019402ba01ba31f242 (MIT; license included).
No external generation service or runtime Python required.

## Boundaries

This is screen-space approximate material relighting, not metric 3D
reconstruction or a complete physically based BRDF/IBL renderer. Region
routing follows the existing Sight semantic bounds and mask, not precise
per-instance geometry. Missing semantic analysis or wrong-shaped atlas
passes through exactly (except explicit coordinate diagnostic view).
Source alpha is preserved. Depth and matte are optional. One atlas per
filter; no live catalog or multi-atlas material slots, displacement,
per-frame synthesis or timing benchmark.

## Verification

- `python scripts/test-pack-hypbr-atlas.py` (Pillow environment): byte-identical
  actual source interiors, periodic gutters, atlas reproducibility,
  overwrite and corrupt input rejection.
- `scripts/verify-sight-interop.sh` includes the direct libobs shader probe
  with the actual baked PNG and controlled semantic, depth, matte and
  persistent coordinate textures. Dedicated probe passed all six checks,
  with zero GL errors on Mesa llvmpipe.
- `scripts/verify-shipped-shaders.sh /path/to/hypershader.so /path/to/data`:
  37/37 actual native loader checks passed, including new filter, single
  texture input visibility and missing-Sight pass-through; zero GL_INVALID_ENUM.
  This is not a live-camera model inference qualification or proof of
  frame-rate performance on other graphics backends.
