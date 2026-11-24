#!/usr/bin/env python3
"""Generate Windows/macOS OBS shader variants from base .effect files."""

from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC_DIR = ROOT / "data" / "shaders" / "root"
OUTPUT_WINDOWS = ROOT / "data" / "shaders" / "platform" / "windows"
OUTPUT_MACOS = ROOT / "data" / "shaders" / "platform" / "macos"

SKIP_DIRS = {"platform", "transitions"}
SKIP_FILES = {"README.md", "SHADERS.md", "OBS_SHADER_SYNTAX.md"}

WINDOWS_INCLUDE_PATH = "../../stdlib/hypershader_standard.glsl"
MACOS_INCLUDE_PATH = "../../stdlib/hypershader_standard.glsl"

# Files that declare their own native boilerplate (ViewProj/VertData/VSDefault)
# are self-contained OBS effects and must not get a compat layer prepended.
NATIVE_BOILERPLATE_MARKER = "struct VertData"


def iter_source_shaders() -> list[Path]:
    shaders: list[Path] = []
    for path in SRC_DIR.iterdir():
        if path.name in SKIP_FILES:
            continue
        if path.is_dir():
            if path.name in SKIP_DIRS:
                continue
            continue
        if path.suffix != ".effect":
            continue
        shaders.append(path)
    return sorted(shaders)


def build_variant(source_text: str, include_path: str, platform_name: str) -> str:
    updated = source_text
    replacement_line = f'#include "{include_path}"'

    include_variants = [
        '#include "../stdlib/hypershader_standard.glsl"',
        '#include "../../stdlib/hypershader_standard.glsl"',
    ]

    replaced = False
    for line in include_variants:
        if line in updated:
            updated = updated.replace(line, replacement_line)
            replaced = True

    if not replaced:
        updated = replacement_line + "\n\n" + updated

    banner = (
        f"// Auto-generated {platform_name} variant.\n"
        "// Do not edit directly; update the source shader and re-run\n"
        "// tools/shader_tools/generate_platform_variants.py\n\n"
    )

    return banner + updated


def write_variant(source: Path, output_root: Path, text: str) -> None:
    output_root.mkdir(parents=True, exist_ok=True)
    out_path = output_root / source.name
    out_path.write_text(text, encoding="utf-8")


def main() -> None:
    shaders = iter_source_shaders()
    generated = 0
    for shader in shaders:
        source_text = shader.read_text(encoding="utf-8")
        if NATIVE_BOILERPLATE_MARKER in source_text:
            continue
        windows_text = build_variant(source_text, WINDOWS_INCLUDE_PATH, "Windows")
        macos_text = build_variant(source_text, MACOS_INCLUDE_PATH, "macOS")

        write_variant(shader, OUTPUT_WINDOWS, windows_text)
        write_variant(shader, OUTPUT_MACOS, macos_text)
        generated += 1

    print(f"Generated variants for {generated} shaders")


if __name__ == "__main__":
    main()
