#!/usr/bin/env python3
"""Verify targeted platform mirrors without regenerating unrelated shaders.

The playground effect is native and self-contained; do not add a compatibility
header to it. Other entries use the established platform variant generator.
"""
import importlib.util
import sys
from pathlib import Path
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
path=ROOT/"tools/shader_tools/generate_platform_variants.py"
spec=importlib.util.spec_from_file_location("shader_variants",path)
assert spec and spec.loader
generator=importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)
NAMES=("playground", "audio_pulse", "creative_coding", "numbers_tunnel", "blurred_gradient", "bubble_physics")
def expected(name, platform):
    source=(ROOT/"data/shaders/root"/(name+".effect")).read_text()
    banner=f"// Auto-generated {platform} variant.\n// Source: data/shaders/root/{name}.effect\n// Do not edit directly.\n\n"
    if name=="playground":
        return banner+source
    return generator.build_variant(source,generator.WINDOWS_INCLUDE_PATH if platform=="Windows"
                        else generator.MACOS_INCLUDE_PATH,platform)
if __name__=="__main__":
    if len(sys.argv)>1 and sys.argv[1]=="--write":
        for name in NAMES:
            for plat,dir in (("Windows","windows"),("macOS","macos")):
                (ROOT/"data/shaders/platform"/dir/(name+".effect")).write_text(expected(name,plat))
    for name in NAMES:
        for plat,dir in (("Windows","windows"),("macOS","macos")):
            file=ROOT/"data/shaders/platform"/dir/(name+".effect")
            assert file.read_text()==expected(name,plat),str(file)
    print(f"PASS: {len(NAMES)*2} repaired platform variants match canonical shader sources")
