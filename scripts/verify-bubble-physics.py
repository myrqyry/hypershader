#!/usr/bin/env python3
"""Bubble Physics GPU regression: actual libobs shader loading and pixel readback.

Use the existing real GPU preview helper (not an image synthesized in Python).
Isolated temporary effect copies test the public numeric controls. No OBS install.
"""
from __future__ import annotations
import importlib.util
import os
import re
import subprocess
import tempfile
from pathlib import Path
from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "data/shaders/root/bubble_physics.effect"
VARIANTS = (
    ("few", "bubble_count", "5", 2.5),
    ("fast", "bubble_speed", "1.8", 3.0),
    ("gravity", "gravity", "2.0", 1.0),
    ("friction", "friction", "1.0", 1.0),
    ("bounce", "bounce", "0.0", 1.0),
    ("transparent", "transparency", "0.0", 0.75),
)

def set_default(src: str, name: str, value: str) -> str:
    pattern = re.compile(r"(?m)(//[^\n]*@default\s+)([-+]?(?:\d*\.)?\d+)([^\n]*\n)(uniform (?:float|int) "+re.escape(name)+r"\s*;)")
    updated,n=pattern.subn(lambda m:m[1]+value+m[3]+m[4],src)
    if n != 1:raise AssertionError(f"{name} @default count {n}")
    return updated

def capture(exe: Path, files: list[Path], out: Path, time: float) -> None:
    out.mkdir(parents=True,exist_ok=True)
    result=subprocess.run(["xvfb-run","-a","-s","-screen 0 1280x720x24","timeout","45",
             str(exe),*(str(p) for p in files),str(out)],
        env={**os.environ,"HS_PREVIEW_TIME":str(time)},capture_output=True,text=True,timeout=65)
    assert result.returncode == 0, result.stdout[-1000:]+result.stderr[-2000:]

def delta(a:Image.Image,b:Image.Image)->float:
    return sum(ImageStat.Stat(ImageChops.difference(a.convert("RGB"),b.convert("RGB"))).mean)/3.0

def main()->None:
    with tempfile.TemporaryDirectory(prefix="hyper-bubble-test-") as td:
        tmp=Path(td); exe=tmp/"capture"
        inc=subprocess.check_output(["pkg-config","--variable=includedir","libobs"],text=True).strip()+"/obs"
        libs=subprocess.check_output(["pkg-config","--libs","libobs","libpng"],text=True).split()
        subprocess.run(["g++","-std=c++17","-O2",f"-I{inc}",str(ROOT/"scripts/verify-shader-orientation.cpp"),
                        "-o",str(exe),*libs],check=True)
        first=tmp/"default"; later=tmp/"later"
        capture(exe,[SOURCE],first,1.25)
        capture(exe,[SOURCE],later,3.25)
        with Image.open(first/SOURCE.with_suffix(".png").name) as before, Image.open(later/SOURCE.with_suffix(".png").name) as after:
            assert before.getchannel("A").getextrema()==(255,255),"Opaque source must not render transparent"
            std=sum(ImageStat.Stat(before.convert("RGB")).stddev)/3
            highlights=sum(max(p[:3])>190 for p in before.getdata())/(before.width*before.height)
            moving=delta(before,after)
            print(f"bubble default: spatial_std={std:.2f} highlights={highlights:.3f} temporal_delta={moving:.2f}",flush=True)
            assert std>20 and highlights>0.001 and moving>3, "Bubble output lacks shape/lighting/animation"

            # Preserve existing UI controls: prove changes affect visible pixels.
            sandbox=tmp/"data"; effect_dir=sandbox/"shaders/root"; effect_dir.mkdir(parents=True)
            (sandbox/"stdlib").symlink_to(ROOT/"data/stdlib",target_is_directory=True)
            sources=[]
            for label,param,value,_ in VARIANTS:
                file=effect_dir/(label+".effect")
                file.write_text(set_default(SOURCE.read_text(),param,value))
                sources.append(file)
            variant_dir=tmp/"variants"
            capture(exe,sources,variant_dir,1.25)
            for label,param,value,threshold in VARIANTS:
                with Image.open(variant_dir/(label+".png")) as changed:
                    assert changed.getchannel("A").getextrema()==(255,255),label+" lost opaque output"
                    difference=delta(before,changed)
                    print(f"{param}={value}: RGB pixel delta={difference:.2f}",flush=True)
                    assert difference>threshold, f"{param} control ineffective: {difference:.2f}"
    print("PASS: Bubble Physics native GPU output, animation, and six control responses")

if __name__=="__main__":
    main()
