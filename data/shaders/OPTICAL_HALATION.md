# Optical Halation (native HyperShader filter)

`root/optical_halation.effect` diffuses actual bright pixels into localized
warm halos, inspired by optical diffusion and film halation rather than
procedural flare overlays. It uses 13 Karis-weighted bright taps at each of
two scales (26 samples per source pixel), a continuous luminance soft knee,
and a warm extended fringe. Existing shadows and the source alpha remain
untouched. It is a **single-pass approximation**, not a true multilevel
downsample/upsample bloom pyramid.

Add HyperShaderFX to a video source and choose **Optical Halation** from
the shader picker. The six settings are:

- **Bloom Intensity:** 0 bypasses the effect; 0.85 is the starting point
- **Luma Threshold / Soft Knee:** define which source highlights spread
- **Bloom Radius / Halo Spread:** determine local and wide glow footprint
- **Halation Warmth:** adds red-orange spectral emphasis to the wide halo

For a restrained natural-camera look, start with the defaults, lower
Intensity if whites become too hot, and raise Luma Threshold to restrict
the bloom to practical light sources. No synthetic chromatic channel split
or random noise is used.

## Real verification

```bash
scripts/verify-optical-halation-gpu.sh
scripts/verify-optical-halation-filter.sh
```

The first test compiles the real effect using libobs/OpenGL and verifies
bright-pixel spread, warm fringe, no effect when disabled or when no
highlight meets the threshold, and unmodified alpha.

The second runs the **actual HyperShaderFX plugin** in a disposable
libobs video graph with moving high-contrast video, verifies the async
shader loader and both exposed settings, and compares actual rendered
pixels with the input at nonzero and zero intensity. It uses the source
repository as the isolated plugin's data root so the plugin's existing
shader path security checks remain intact. It never changes the user's
installed plugin, OBS scenes, or filter settings.

Neither test establishes real-camera appearance, hardware cost at
1920x1080, HDR correctness, or interaction with every existing filter.
The GPU tests execute under Mesa/llvmpipe in an isolated Xvfb session;
a live camera and hardware GPU check is still worthwhile.
