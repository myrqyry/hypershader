#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]

checks = []

def check(label, condition):
    checks.append((label, bool(condition)))

native_header = root / "src/hypershader_data.h"
legacy_header = root / "src/shadertoy_data.h"
check("native runtime header exists", native_header.exists())
check("legacy runtime header is gone", not legacy_header.exists())

native_std = (root / "data/stdlib/hypershader_standard.glsl").read_text()
compat = (root / "data/stdlib/shadertoy_compat.glsl").read_text()
check("native stdlib does not depend on Shadertoy compatibility", "shadertoy" not in native_std.lower())
check("Shadertoy adapter depends on native stdlib", '#include "hypershader_standard.glsl"' in compat)
check("legacy wrapper alias exists only in adapter", "#define SHADERTOY_MAINIMAGE_WRAPPER HYPERSHADER_MAINIMAGE_WRAPPER" in compat)

forbidden_runtime_tokens = [
    "shadertoy_data",
    "shadertoy_filter_info",
    "shadertoy_source_info",
    "shadertoy_transition_info",
    "DEBUG_ACTIVE_SHADERTOY",
]
runtime_text = "\n".join(
    p.read_text()
    for p in (root / "src").rglob("*")
    if p.is_file() and p.suffix in {".c", ".h", ".cpp", ".hpp"}
)
for token in forbidden_runtime_tokens:
    check(f"runtime does not use legacy token {token}", token not in runtime_text)

native_authoring_paths = [
    root / "src/llm/llm_context.h",
    root / "src/ui/shader_builder_dock.cpp",
    root / "src/ui/shader_builder_dock.hpp",
    root / "data/shaders/OBS_SHADER_SYNTAX.md",
    root / "data/shaders/SHADERS.md",
    root / "tools/shader_tools/generate_platform_variants.py",
]
native_authoring_paths.extend(sorted((root / "data/shaders/root").glob("*.effect")))
native_authoring_paths.extend(sorted((root / "data/shaders/platform/windows").glob("*.effect")))
native_authoring_paths.extend(sorted((root / "data/shaders/platform/macos").glob("*.effect")))

for p in native_authoring_paths:
    text = p.read_text()
    leaked = any(token in text for token in ("Shadertoy", "shadertoy", "SHADERTOY_"))
    check(f"native authoring surface has no Shadertoy dependency: {p.relative_to(root)}", not leaked)

failed = 0
for label, ok in checks:
    print(("PASS" if ok else "FAIL") + ": " + label)
    if not ok:
        failed += 1

if failed:
    raise SystemExit(f"{failed} native-boundary checks failed")
