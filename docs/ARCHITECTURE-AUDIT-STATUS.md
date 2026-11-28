# Architecture audit implementation ledger

2026-11-28 — original 100-item user-supplied architecture audit.
This is an implementation status index, not a claim that unreviewed items
are absent from the current code. Preserve original numbering; recheck
current code before each change. All items not explicitly marked verified
or partial below remain open for inspection and implementation.

## Rendering and graphs
- [x] 1. Multipass source graphs — existing 5bfb590; reverified 14-case native graph suite.
- [ ] 2. Multipass transitions.
- [x] 3. Transient passes and lifetime-aware allocations — fd3c56c: opt-in immutable transient and inclusive last-use descriptors; matching non-overlapping intervals share physical owners. Persistent history unchanged; budget counts physical owners. Actual native source/filter test: 4 vs 8 half-size pass targets, expected pixels and retained history. Invalid lifetimes reject without swapping; dead bindings use black. Explicit lifetimes, not automatic shader dependency analysis; visual Builder still persistent.
- [ ] 4. Optional mipmaps.
- [x] 5. Host render scale — 2a01400: opt-in 25/50/75/100% source/filter internal extent; full-size upstream/base/output, graph pass divisors relative to internal size, scale change resets history, physical budget accounts for scaled surfaces. Native 36/36 scale cases and 1 MiB admission recovery, full detail preserved at effect opacity zero. Transitions remain full resolution; no performance benchmark.
- [ ] 6. Universal spatial masking.
- [ ] 7. Expanded blend modes and alpha handling.
- [ ] 8. HDR and linear-light composition.
- [x] 9. Reset History action, hotkey and proc — 71df5aa: atomic per-instance request consumed before rendering; graph counters/read seeds reset, compatible surfaces retained. Filter/source normal feedback and named graph tested through property, hotkey and native proc (12/12), preserving edited gain and independent peer. Complementary cancel hotkey uses OBS source-pair API for private filters. Transitions excluded.
- [ ] 10. Subdivided mesh / instanced geometry.
## Performance and concurrency
- [x] 11. Update-time typed parameter snapshots — 46ba064 + bd524b1: cache numeric/color controls, modulation, reaction and animation/atlas config at update or compiled-effect swap; live signals evaluated once per render. Native interposition probe: 60/60 correct draws, settings retrieval 120 -> 0, value API calls 780 -> 0. This establishes the settings hot-path contract, not a whole-render zero-allocation or frame-time claim.
- [x] 12. Cache listed per-frame effect handles — 46ba064, graph and compositor tests.
- [ ] 13. Backend-safe MediaPipe readback — partial: c29f99e bounds capture/inference/upload to one outstanding slot and deduplicates video timestamps. 4f0242e uses a real Linux OpenGL fence, zero-timeout polling, map only after signal and 250 ms expiry; no double-buffering claim. Actual ONNX + llvmpipe native regular/composited filter probes passed, including forced readiness delays and held upload results. Other backends retain bounded synchronous staging, not qualified as nonblocking.
- [x] 14. SPSC audio race — af2940a, producer/consumer queue and worker-owned history, concurrent and real PCM probes.
- [x] 15. Audio source lifetime and teardown — 86fb457: worker detaches removed sources; strong ref retained only while callback registered for safe libobs teardown. Native deletion/replacement regression passes; intentionally not an unsynchronized weak-reference conversion.
- [x] 16. Lazy EnTT allocation — 2d91f24: allocate on compatible effect load, release on switch/teardown, GPU texture creation outside render. Native entity animation and allocation lifecycle regression passed; full catalog 39/39.
- [ ] 17. Hidden/disabled workload suspension — partial b365cde: showing/preview and direct-draw lease; audio callback admission, worker reset/backoff, Sight polling and ECS ticks gated. Native source/filter lifecycle, actual PCM pixels, fixture Sight endpoint, preview and direct draw, late filter attachment verified. 9ae0549 adds native inference capture/result generations, invalidation across suspension and graph/provider changes, worker-owned graph switch and safe binding fallback. Real ONNX regular/composited held-result probes passed four lifecycle changes, fresh detection recovered. Running model calls are not forcibly canceled; retained resources and initial synchronous model load remain.
- [ ] 18. Single-pass GLSL translator.
- [ ] 19. Stdlib include cache — POSIX implementation ee3553c: bounded shared immutable per-file text and parsed include plans, canonical path + high-resolution stat identity, 64 entries/2 MiB retained-buffer LRU. Native cold opens 2→1, warm 2→0 per library; edit/delete/recovery/metadata/order/managed revision/eviction verified. Windows conservatively bypasses cache pending native file identity implementation; automatic dependency watching is separate.
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
- [ ] 97. Structured logging and diagnostics — partial 70c96fc: teardown messages use OBS logger exclusively, no duplicate per-step config-file open/write/flush/close. Native source/filter teardown probe confirms start/end messages retained and sidecar absent; broader diagnostics work remains.
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

2026-11-28 fifth batch: fd3c56c transient pass planning and physical target reuse. Native source comparison persistent 8 vs transient 4 render target handles, 68 valid draws each, first red=3 last=204 green=153; filter 69 valid draws and 4 targets. Expired binding probe sampled black rather than active write target. Three malformed descriptors rejected, retained 20/20 frames each in both lanes. Unit budget/slot test under UBSan, 14/14 legacy native graph cases, 39/39 catalog, persistent Builder codegen passed. Evidence /tmp/hs-transient-{red,green,reject,final,filter,legacy,catalog-full}.log. No automatic dependency analysis, visual Builder UI, GPU memory telemetry, install/push, or cross-platform verification. Next graph history reset (#9), render scale and hidden workload gating; preserve persistent graph semantics.

2026-11-28 sixth batch: 71df5aa history reset. Baseline missing all three entry points, mature red=204; final every entry point resets first two draws red=10/20, peer stays 204, edited gain green=153, graph counter encoded blue=0/26. Source/filter × single-pass feedback/named graph all pass; hotkey release ignored, binding saved, pending cancel verified. Existing filter enable/disable hotkey, source feedback cycle, transient filter/reject and catalog 39/39 pass. Qt build Linux libobs/OpenGL. Single source hotkey API rejects private filters; paired reset/cancel registration verified. Locale pin updated. No install/push, Windows/macOS, Play dock button, or transition history support. Evidence /tmp/hs-history-{red,green,pair,final,hotkeys-final,feedback-final,transient-final,catalog-full}.log. Next render scale (#5) and hidden workload gating (#17).

2026-11-28 seventh batch: 2a01400 Render Scale, c8aa4b1 test-fixture teardown race. Native six lanes × six updates (100/75/50/25/100/invalid) all pass actual internal render target sizes and output pixels; histories seed blue=5 and mature=204, invalid setting keeps 204; full-res alternating 16/240 pixels intact under muted filter. Unit rounding and allocation arithmetic UBSan; actual graph budget-scale bypass=19 seed=3 mature=88 valid=34 bypass_again=14 restart=3. History reset 12/12; compositor 7/7 after fixing fixture pointer under graphics lock (failed delta 1193561 exactly old-vs-new fixture). Legacy graph 14/14 behavioral checks; wrapper exit 2 because script was modified while executing, current bash -n passes and new budget-scale case exits 0. Catalog 39/39. Evidence /tmp/hs-scale-*.log. Linux OpenGL software rendering, no real GPU timing, scaled MediaPipe backend qualification, transitions or push/install. Next #17 inactive workload suspension.

2026-11-28 eighth batch: b365cde inactive work gating. 250ms valid-draw lease; filter parent show/hide and attach/detach; no time/history semantic changes. Audio worker 21ms active/100ms maintenance while inactive; lock-free callback epoch/inflight guard and worker-owned reset; retained routes continue removal maintenance. Native red hidden Sight reads 32/33 and audio fresh=1; final source/filter reads=0 and audio fresh=0, resume silent red<=2, active PCM/Sight red=26 green=255; preview no draws 11/10 reads, direct render and late attach pass. Sight endpoint is deterministic fixture, actual libobs PCM/graphics. Final scope and lifetime, entity lifecycle, Qt performance/PCM/modulation pass. Music/beat and 39/39 shipped catalog passed pre-final guard refinement. No native MP in-flight cancellation, hardware CPU/GPU benchmark, TSAN, cross-platform, install or push. Evidence /tmp/hs-inactive-*.log. Next stale native inference result across activity/graph mode changes, then eliminate per-instance sync teardown file logging (#97) and other remaining audit items.

2026-11-28 ninth batch: 9ae0549 native inference generation ownership, 70c96fc teardown logging. Real ONNX baseline (prior fence build) held face across disable/provider switch leaked x=.4980 before/after upload; new module both x=0. Expanded regular and composited tests cover disable, provider, graph and hidden cycles (8/8), fresh face x=.4980 and blank=0, one held result/zero extra captures, zero early maps or blocking waits. Graph start/switch moved to worker; immutable capability query in settings. No-model Qt build and 39/39 catalog passed after unused-variable fix. Native teardown test before starts=2 ends=2 sidecar=1, after 2/2/0; final inactive source/filter and entity lifecycle pass. No forced cancellation of running model call, resource eviction, initial model-loading migration, hardware timing, cross-platform, live installation or push. Evidence /tmp/hs-epoch-*.log and /tmp/hs-teardown-*.log. Next #19 stdlib include cache (dependency-aware invalidation) or #18 translator inspection.

2026-11-28 tenth batch: ee3553c shared stdlib include cache. Preserve caller-specific guard and fresh nested include resolution; metadata reads cached text without caching mutable metadata. Native test baseline cold/warm parent/leaf each 2 opens; final 1/0, actual pixel and metadata changes on same-size edit with restored mtime, peer unchanged, missing hard failure and recreation, changed nested dependency/order, managed revision and eviction after 70 unique keys. Final Qt build, 39/39 shipped catalog, 12/12 stdlib loader modes including isolated Apply, private Builder compile feedback valid/invalid/stale passed. No shader asset changes. Windows uses uncached path, macOS stat branch unqualified, no automatic watcher, transactional multi-file snapshot, cold-miss coalescing, hardware or wall-clock benchmark, install/push. Evidence /tmp/hs-stdlib-cache-*.log. Next #18 single-pass GLSL translator, preserving comments/strings and native token behavior, or #20 aggregate VRAM admission.
