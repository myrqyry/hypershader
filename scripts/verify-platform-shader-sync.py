#!/usr/bin/env python3
"""Check that repaired root effects match shipped generated platform variants.

Checks only the explicitly repaired shaders; regeneration of the entire
historical platform tree would silently rewrite unrelated assets.
"""
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

# Keep a source-check invocation from creating untracked Python cache files.
sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
GENERATOR_PATH = ROOT / "tools/shader_tools/generate_platform_variants.py"
CHECKED = ("crt_retro.effect", "ps1_filter.effect",
           "gravity_melt.effect", "pixelsort_glitch.effect")


def main() -> int:
    spec = importlib.util.spec_from_file_location("platform_shader_generator", GENERATOR_PATH)
    assert spec and spec.loader
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    errors = []
    for filename in CHECKED:
        text = (ROOT / "data/shaders/root" / filename).read_text(encoding="utf-8")
        for platform in ("windows", "macos"):
            expected = generator.build_variant(
                text, generator.WINDOWS_INCLUDE_PATH if platform == "windows"
                else generator.MACOS_INCLUDE_PATH,
                "Windows" if platform == "windows" else "macOS",
            )
            path = ROOT / "data/shaders/platform" / platform / filename
            if not path.exists() or path.read_text(encoding="utf-8") != expected:
                errors.append(f"{platform}/{filename}")
    if errors:
        print("PLATFORM_VARIANTS_STALE=" + ",".join(errors))
        return 1
    print(f"PLATFORM_VARIANTS_SYNC_PASS files={len(CHECKED) * 2}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
