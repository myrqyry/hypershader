# P-Frame Drift — lost-reference stream effect

**Shader:** `root/pframe_drift.effect`

A live, codec-inspired effect focused on the failure mode of **predicted
video frames**. Old decoded image blocks are reused and moved using a cheap
macroblock motion-search approximation while missing residuals prevent clean
pixels from correcting the picture. Error compounds from one output frame to
the next. A simulated clean **I-frame** resets the feedback immediately.

This is *not* a real H.264/H.265 parser, an encoded P-frame bitstream editor,
or access to actual codec motion vectors. It uses HyperShader's existing
`hs_history` output-frame feedback. That is crucial: without a working
previous-output texture the effect is not temporal moshing.

## Try it in OBS

Add **HyperShader** to a camera or moving video source. Select
`P-Frame Drift — Lost Reference Stream` in the shader list, or choose
`pframe_drift.effect` from the plugin's installed shader folder.

Controls:

- **P-Frame Loss** controls how much new image information is lost.
- **Missing Residual** controls how little of the real source repairs it.
- **Macroblock Size** groups errors into contiguous codec-like regions.
- **Motion Vector Carry** draws old image content into moving regions.
- **Residual Recovery** is the minimum clean input per frame.
- **Clean I-Frame Every** is the automatic recovery interval; zero disables.
- **Force Clean Keyframe** (1) forces a full clean frame. Return to 0 afterward.
- **Loss Burst Period**: zero means always damaged; greater values cycle.
- **Burst Duty Cycle**: fraction of each cycle that remains damaged.
- **Chroma Residual Loss**: keeps erroneous old color without RGB splits.

For a long melt, set Clean I-Frame Every to 0, P-Frame Loss near 1,
Missing Residual near 1, and Residual Recovery near 0. Move something
through the frame, then toggle Force Clean Keyframe to 1 to snap back.

## Validation

`scripts/verify-pframe-drift-gpu.sh` runs a real isolated OBS/OpenGL shader
render using moving synthetic video and alternating GPU textures.
It tests: carrying prior damaged frames; changes as motion continues;
clean forced keyframe; bypass at zero loss; and alpha preservation.

**Full runtime verification:** `scripts/verify-pframe-filter-runtime.sh`
loads the actual HyperShader module into a headless libobs video graph,
creates a real filter and synthetic moving video source, waits for its
asynchronous shader load, inspects the runtime parameter controls, and
renders 75 frames with both high-loss and zero-loss settings. It asserts
persisting distortions over late frames, clean forced I-frame recovery,
and zero-loss passthrough. The shader itself is standalone native OBS
effect syntax, deliberately avoiding the legacy include preprocessor's
unverified wrapper expansion behavior.

For testing an installed plugin without running the user's OBS session:

```bash
scripts/verify-pframe-filter-runtime.sh \
  "$HOME/.config/obs-studio/plugins/hypershader/bin/64bit/hypershader.so" \
  "$HOME/.config/obs-studio/plugins/hypershader/data"
```

**Remaining boundaries:** neither test accesses actual H.264/H.265
motion vectors or encoded bitstreams, and the isolated test at 240×136
does not establish live 1080p performance, all older shader compatibility,
or the appearance on a real webcam. Existing scenes and filter settings
are outside the test and are preserved during a binary/shader-only update.
