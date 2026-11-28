# Architecture audit implementation ledger

2026-11-28 — original 100-item user-supplied architecture audit.
This is an implementation status index, not a claim that unreviewed items
are absent from the current code. Preserve original numbering; recheck
current code before each change. All items not explicitly marked verified
or partial below remain open for inspection and implementation.

## Rendering and graphs
- [x] 1. Multipass source graphs — existing 5bfb590; reverified 14-case native graph suite.
- [ ] 2. Multipass transitions.
- [ ] 3. Transient passes and lifetime-aware allocations.
- [ ] 4. Optional mipmaps.
- [ ] 5. Host render scale.
- [ ] 6. Universal spatial masking.
- [ ] 7. Expanded blend modes and alpha handling.
- [ ] 8. HDR and linear-light composition.
- [ ] 9. Reset History action, hotkey and proc.
- [ ] 10. Subdivided mesh / instanced geometry.
## Performance and concurrency
- [x] 11. Update-time typed parameter snapshots — 46ba064 + bd524b1: cache numeric/color controls, modulation, reaction and animation/atlas config at update or compiled-effect swap; live signals evaluated once per render. Native interposition probe: 60/60 correct draws, settings retrieval 120 -> 0, value API calls 780 -> 0. This establishes the settings hot-path contract, not a whole-render zero-allocation or frame-time claim.
- [x] 12. Cache listed per-frame effect handles — 46ba064, graph and compositor tests.
- [ ] 13. Backend-safe MediaPipe readback — partial: c29f99e bounds capture/inference/upload to one outstanding slot and deduplicates video timestamps. 4f0242e uses a real Linux OpenGL fence, zero-timeout polling, map only after signal and 250 ms expiry; no double-buffering claim. Actual ONNX + llvmpipe native regular/composited filter probes passed, including forced readiness delays and held upload results. Other backends retain bounded synchronous staging, not qualified as nonblocking.
- [x] 14. SPSC audio race — af2940a, producer/consumer queue and worker-owned history, concurrent and real PCM probes.
- [x] 15. Audio source lifetime and teardown — 86fb457: worker detaches removed sources; strong ref retained only while callback registered for safe libobs teardown. Native deletion/replacement regression passes; intentionally not an unsynchronized weak-reference conversion.
- [x] 16. Lazy EnTT allocation — 2d91f24: allocate on compatible effect load, release on switch/teardown, GPU texture creation outside render. Native entity animation and allocation lifecycle regression passed; full catalog 39/39.
- [ ] 17. Hidden/disabled workload suspension.
- [ ] 18. Single-pass GLSL translator.
- [ ] 19. Stdlib include cache.
- [ ] 20. Aggregate VRAM admission budget.
## Play dock
- [ ] 21. Persistent named Looks.
- [ ] 22. Timed/beat-quantized morphing.
- [ ] 23. Multi-route modulation.
- [x] 24. In-place Qt value updates — 4d76384; widget identity regression passed. JSON polling remains, form rebuild limited to schema/identity changes.
- [ ] 25. Live modulation slider overlay.
- [ ] 26. OBS websocket vendor API.
- [ ] 27. MIDI and OSC bridge.
- [ ] 28. GPU/VRAM telemetry.
- [ ] 29. Before/after wipe.
- [ ] 30. Per-instance hotkeys.
## Audio
- [ ] 31. Tap tempo, BPM lock, phase nudge and half/double.
- [ ] 32. Trigger-locked scope.
- [ ] 33. Stereo analysis and Lissajous.
- [ ] 34. AGC and noise gate.
- [ ] 35. Flash-rate reduction (not a safety guarantee).
- [x] 36. All native music metadata tags — 64fc25b, all tag mappings and real snare/kick/flux probes.
- [ ] 37. Dual-resolution FFT.
- [ ] 38. Spectral centroid, rolloff, pitch.
- [ ] 39. Audio update-rate and dead settings.
- [ ] 40. Sprite threshold preview.
## Sight and computer vision
- [ ] 41. Gesture triggers.
- [ ] 42. Standardized blendshape drivers.
- [ ] 43. Subject velocity and scale delta.
- [ ] 44. Canny integration or prune.
- [ ] 45. GPU persistent surface UV advection.
- [ ] 46. HypBR browser and manifest loader.
- [ ] 47. Multi-material atlas.
- [ ] 48. Depth-PNG channel decoding.
- [ ] 49. Metadata-driven ECS physics.
- [ ] 50. Sight diagnostic doctor.
## Shader API
- [ ] 51. Preprocessor line mapping.
- [ ] 52. Real font rendering.
- [ ] 53. Compound show-if expressions.
- [ ] 54. Opt-in stdlib controls.
- [ ] 55. 3D/volumetric/screenspace stdlib qualification.
- [ ] 56. Semantic-name texture bindings.
- [ ] 57. Expanded texture pool; validate backend limits.
- [ ] 58. 3D LUT input.
- [ ] 59. Rich range/vector/color metadata.
- [ ] 60. Authored factory presets.
## Native Builder
- [ ] 61. Syntax highlighting and gutter.
- [ ] 62. Clickable diagnostics.
- [ ] 63. Safe external live reload.
- [ ] 64. Local LLM — partial 60dabb8: bounded JSON and tokenizer initialization; actual Qwen BPE and model context contract still open.
- [ ] 65. Optional OpenAI-compatible provider.
- [ ] 66. Uniform metadata wizard.
- [ ] 67. Library favorites/recents/chips.
- [ ] 68. AI repair diff.
- [ ] 69. Live preview.
- [ ] 70. Recovered drafts.
## Visual Builder
- [x] 71. Existing toolbar/canvas bindings — inspected current setupEventListeners; earlier four browser smoke cases passed. No duplicate implementation.
- [ ] 72. Undo/redo.
- [ ] 73. Minimap.
- [ ] 74. WebGL2 preview adapters.
- [ ] 75. Simulated input controls.
- [ ] 76. Multipass/feedback graph nodes — partial: df62e68 persistent stages and previous/latest nodes; separate hs_history authoring not qualified.
- [ ] 77. Compound subgraphs.
- [ ] 78. Rich node properties.
- [ ] 79. Opt-in live OBS bridge.
- [ ] 80. Deterministic .hs export.
## Packaging and interoperability
- [ ] 81. In-dock .hs import/export.
- [ ] 82. Thumbnail generator.
- [ ] 83. URL source range mapping and smoothing.
- [ ] 84. URL source text/color.
- [ ] 85. Multi-channel JSON Pointer.
- [ ] 86. Foreign import wizard.
- [ ] 87. Collection manifest.
- [ ] 88. Scene dependency export.
- [ ] 89. Native transition pack.
- [ ] 90. Offline portability linter.
## Reliability and maintenance
- [ ] 91. Crash-resilient shader watchdog.
- [ ] 92. Cross-platform global cursor.
- [ ] 93. Windows/macOS real render tests.
- [ ] 94. Split monolithic runtime/UI units along verified seams.
- [ ] 95. Dead and duplicate utility audit.
- [ ] 96. Localization.
- [ ] 97. Structured logging and diagnostics.
- [ ] 98. Optional hidden-scene channel activation with cycle prevention.
- [ ] 99. Parser fuzzing — bounded deterministic JSON cases added; broad fuzz/property coverage still open.
- [ ] 100. Versioned settings migration.

## Evidence and continuation

Local branch feature/source-render-graphs, previous baseline 049ffff.
Runtime commits 60dabb8, af2940a, 46ba064, 4d76384, 64fc25b.
Fresh Qt-enabled /tmp/hs-audit-qt-build and isolated staged payload.
Final catalog: 39/39 native Linux libobs/OpenGL cases, exit 0.
Graph suite: 14/14; actual Qt performance editor identity and PCM tests;
three filter, three source and one transition compositor regression;
musical onset and all-tag C tests; persistent Builder JS codegen,
native boundary and README links.
Evidence: /tmp/hs-audit-shipped-full.log, /tmp/hs-audit-cache-graph.log,
/tmp/hs-audit-compositor.log, /tmp/hs-audit-panel-final.log,
/tmp/hs-audit-meta-green.log, /tmp/hs-audit-final-*.log.

Next: qualify additional graphics backends for actual GPU readiness; Linux OpenGL
fence and bounded worker admission now verified with actual ONNX. D3D11/Metal
readiness and production hardware frame-time measurements remain open. Native
model initialization/mode changes still need separate lifecycle and thread review.
Graph lifetime/transient admission before expanding render scale and
transition scheduler. Universal spatial mask should reuse the existing
Sight path and preserve temporal history. No duplicate inference engine.
Keep output defaults compatible and qualify native pixels before commit.
No throughput measurements, TSAN qualification, live installation, model
inference, Windows/macOS validation, merge or push in this pass.

2026-11-28 second batch: 86fb457 and 2d91f24; evidence /tmp/hs-lifetime-red.log, /tmp/hs-lifetime-green.log, /tmp/hs-entity-red.log, /tmp/hs-entity-green.log, /tmp/hs-entity-catalog-full.log. Isolated Qt build, no install/push. Weak-only audio ownership intentionally not implemented; removed-source detach depends on running audio worker.

2026-11-28 third batch: bd524b1 update-time settings cache. Native Qt build; 39/39 catalog, 14/14 graph, 7/7 compositor, performance Qt/PCM/LFO/live edits, URL Source integration, entity lifecycle, audio oscilloscope/lifetime passed. All 21 reactive-sprite behavior assertions passed; script exit 1 from temporary-directory cleanup race after final PASS (not a render failure). New native call-count probe red 120 settings/780 value calls, green zero/zero, identical expected pixels 60/60 each. Evidence /tmp/hs-settings-{red,build,panel-green,data,two-buffer-graph,reactive-sprite,catalog,global-compositor,entity-lifecycle,audio-oscilloscope,call-count}.log. No production GPU benchmark, model-enabled inference, install, push or cross-platform qualification. Settings changes publish at libobs video update callback, not immediate synchronous obs_source_update.

2026-11-28 fourth batch: c29f99e and 4f0242e. Native ONNX 1.17.1 with existing local paired face models (staged only) + existing local face fixture. Baseline same timestamp six stages; new one or zero if busy. Deliberate upload hold: 15 results and 16 extra stages before, one and zero after. Linux OpenGL forced 16 not-ready polls, zero early maps/positive-timeout waits, two expired fences, recovered normalized face x=.4980 then blank=0. Regular and composited input pass, optional no-fence ONNX build passes, final Qt no-model build 39/39 catalog. Mesa llvmpipe rather than hardware GPU; no frame-time or Windows/macOS qualification. Optional GLVND link when OpenGL::OpenGL available; unsupported builds keep logged bounded synchronous path. Evidence /tmp/hs-readback-{red,green,pressure-red,pressure-green}.log, /tmp/hs-fence-{red,green,expiry,composite,fallback,catalog-full}.log. No install/push or model asset publication.
