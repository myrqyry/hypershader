# Shared CI qualification

The reusable workflow at obs-plugin-ci 70ce37b1015df80478f8ed559f5c88bf4007233f
builds native packages against OBS 32.2.0 and 33.0.3 on Linux x86-64,
Windows x64, macOS arm64 and macOS x86-64. The pre-existing 31.1.1
baseline remains separate, including its metadata, installer, shader
settings, image ABI and source-export checks. No release or visibility
change is performed.

The caller reuses the SDK bootstrap and archive handoff qualified by
emulens b72651e, retains native MSVC atomics verification, and disables
optional ONNX, LiteRT, frontend and Qt paths to match the baseline build
configuration. Those optional components, actual native Windows/macOS
rendering, full OBS GUI behavior and live provider data need separate
qualification. No forced module ABI is used.

Linux must render all 37 shipped root effects in real isolated
libobs/OpenGL under Xvfb, using the pinned SDK and the packaged module
and data. Each effect must report successful compilation, pixel
readback, expected inputs and fallback behavior; the final audit count
and per-effect results are checked. This is not a performance or live
ML benchmark. Prior intermittent teardown failures are not waived.

The runtime inventory is scripts/release-assets.txt, pinned to Git
blob IDs. The staged binary tree is checked against the included
inventory; unknown or missing assets fail. Packages include LICENSE
and upstream data notices, without duplicate modules or PDBs.
The source archive uses the existing committed candidate exporter and
excludes agent guidance and private state; its payload manifest refers
to the installed Linux32 binary tree, not the source tree. Test archives
are not public releases or automatic licensing clearance.

Local: fresh Release-like build with installed OBS 32.2.0 SDK and
optional backends disabled. All 37 native render contracts passed on
isolated OpenGL; package-layout fixture tests passed. The private
repository stays private. Hosted qualification is pending.

## PR #1 follow-up

Run 45914621730 was blocked before the planning job started by the
GitHub Actions budget; there is no hosted build or artifact evidence.
Codex correctly found that the Linux installer rejected the generated
modern-only OBS33 package. The installer now accepts that layout without
creating an older-ABI module copy, preserves dependencies and license,
and refuses to overwrite an installation containing a legacy module.
The package-to-installer regression uses the real adapter and installer.
