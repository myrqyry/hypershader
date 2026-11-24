#!/usr/bin/env python3
"""Probe-only conversion of simple WebGL2 fragment shaders to native .effect.

This is deliberately NOT a general GLSL importer. It only stages temporary
fixtures for real libobs compile/readback analysis of a pinned external corpus.
It is not used by the HyperShaderFX runtime or editor.
"""
import argparse
import re
from pathlib import Path


def wrap_shader(source: str) -> str:
    browser_main = r'\bvoid\s+main\s*\(\s*\)'
    if not re.search(browser_main, source):
        raise ValueError('No WebGL2 void main() entry point')
    source = re.sub(r'^\s*#version\s+300\s+es\s*\n', '', source, flags=re.M)
    source = re.sub(r'^\s*precision\s+\w+\s+\w+\s*;\s*\n', '', source, flags=re.M)
    source = re.sub(r'^\s*uniform\s+vec2\s+u_resolution\s*;\s*\n', '', source, flags=re.M)
    source = re.sub(r'^\s*uniform\s+float\s+u_time\s*;\s*\n', '', source, flags=re.M)
    source = re.sub(r'^\s*out\s+vec4\s+out_color\s*;\s*\n', '', source, flags=re.M)
    source = re.sub(browser_main, 'void hs_shade_pixel(out vec4 fragColor, in vec2 fragCoord)', source)
    source = re.sub(r'\bgl_FragCoord\.xy\b', 'fragCoord', source)
    source = re.sub(r'\bgl_FragCoord\b', 'vec4(fragCoord,0.0,1.0)', source)
    source = re.sub(r'\bout_color\b', 'fragColor', source)
    source = re.sub(r'\bu_resolution\b', 'hs_resolution.xy', source)
    source = re.sub(r'\bu_time\b', 'hs_time', source)
    # Native stdlib defines one-argument atan(v); GLSL allows atan(y, x).
    # This intentionally only recognizes the simple two-argument call shape.
    source = re.sub(r'\batan\s*\(([^()\n]*,[^()\n]*)\)', lambda m: 'atan2(' + m.group(1) + ')', source)
    return '#include "../../stdlib/hypershader_standard.glsl"\n' + source + '\nHYPERSHADER_PIXEL_WRAPPER\n'


def stage_directory(source_dir: Path, dest_dir: Path, mode: str) -> int:
    files = sorted(source_dir.glob('*.glsl'))
    if not files:
        raise ValueError(f'No *.glsl files in {source_dir}')
    if dest_dir.resolve() == source_dir.resolve():
        raise ValueError('Refusing in-place staging')
    dest_dir.mkdir(parents=True, exist_ok=True)
    for f in files:
        target = dest_dir / (f.stem + '.effect')
        if target.exists():
            raise FileExistsError(f'Will not overwrite {target}')
        original = f.read_text(encoding='utf-8')
        target.write_text(wrap_shader(original) if mode == 'wrapped' else original, encoding='utf-8')
    return len(files)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source_dir', type=Path)
    parser.add_argument('dest_dir', type=Path)
    parser.add_argument('--mode', choices=('raw', 'wrapped'), required=True)
    args = parser.parse_args()
    print(f'STAGED={stage_directory(args.source_dir, args.dest_dir, args.mode)} mode={args.mode}')
