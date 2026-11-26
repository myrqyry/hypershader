#!/usr/bin/env python3
"""Real libobs/Mesa image regressions for visually deficient hypershader demos.

Uses the existing effect thumbnail renderer, not mocked shader math. Temporary
metadata variants exercise actual UI uniforms without touching user OBS files.
"""
from __future__ import annotations
import os
import re
import subprocess
import tempfile
from pathlib import Path
from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parent.parent
FILES = ("numbers_tunnel", "blurred_gradient")
TARGETS = {
    "numbers_tunnel": {"std": 25, "bright": 0.02, "colorful": 0.07, "temporal": 3},
    "blurred_gradient": {"std": 18, "bright": 0.12, "colorful": 0.25, "temporal": 3},
}
VARIANTS = {
    "numbers_tiny": ("numbers_tunnel", "number_size", "0.2", 4.0),
    "numbers_dark": ("numbers_tunnel", "glow_intensity", "0.0", 4.0),
    "gradient_static": ("blurred_gradient", "distortion_amount", "0.0", 2.0),
    "gradient_shift": ("blurred_gradient", "color_shift", "1.0", 2.0),
    "gradient_blur_off": ("blurred_gradient", "blur_mix", "0.0", 0.75),
    "gradient_blur_max": ("blurred_gradient", "blur_radius", "20.0", 1.0),
}

def mean_difference(a: Image.Image, b: Image.Image) -> float:
    return sum(ImageStat.Stat(ImageChops.difference(a.convert("RGB"), b.convert("RGB"))).mean)/3

def evaluate(before: Image.Image, after: Image.Image, name: str) -> None:
    rgb = before.convert("RGB")
    px = list(rgb.getdata())
    stats = ImageStat.Stat(rgb)
    variance = sum(stats.stddev)/3
    bright = sum(max(c)>115 for c in px)/len(px)
    colorful = sum(max(c)-min(c)>35 for c in px)/len(px)
    temporal = mean_difference(rgb, after)
    opaque = before.getchannel("A").getextrema() == (255,255)
    measures = dict(std=variance,bright=bright,colorful=colorful,temporal=temporal)
    print(f"{name}: "+", ".join(f"{k}={v:.3f}" for k,v in measures.items())+f", opaque={opaque}", flush=True)
    assert opaque, name+" output contains transparency"
    for key, expected in TARGETS[name].items():
        assert measures[key] > expected, f"{name}: {key}={measures[key]:.3f}, expected > {expected}"

def variant_source(shader: str, param: str, value: str) -> str:
    pattern = re.compile(r"(?m)(//[^\n]*@default\s+)([-+]?(?:[0-9]*\.)?[0-9]+)([^\n]*\n)(uniform (?:float|int) "+re.escape(param)+r";)")
    src = (ROOT/"data/shaders/root"/(shader+".effect")).read_text()
    result,n = pattern.subn(lambda m: m.group(1)+value+m.group(3)+m.group(4), src)
    if n != 1: raise AssertionError(f"Expected one default for {shader}:{param}; found {n}")
    return result

def capture(exe: Path, shader_paths: list[Path], output: Path, time: str) -> None:
    output.mkdir(parents=True,exist_ok=True)
    subprocess.run([
        "xvfb-run","-a","-s","-screen 0 1280x720x24","timeout","45",
        str(exe),*(str(p) for p in shader_paths),str(output)
    ],check=True,stdout=subprocess.DEVNULL,env={**os.environ,"HS_PREVIEW_TIME":time})

def main() -> None:
    with tempfile.TemporaryDirectory(prefix="hyper-polish-shader-") as td:
        tmp=Path(td); exe=tmp/"capture"
        inc=subprocess.check_output(["pkg-config","--variable=includedir","libobs"],text=True).strip()+"/obs"
        args=subprocess.check_output(["pkg-config","--libs","libobs","libpng"],text=True).strip().split()
        subprocess.run(["g++","-std=c++17","-O2",f"-I{inc}",str(ROOT/"scripts/verify-shader-orientation.cpp"),"-o",str(exe),*args],check=True)
        sources=[ROOT/"data/shaders/root"/(name+".effect") for name in FILES]
        first=tmp/"first"; later=tmp/"later"
        capture(exe,sources,first,"1.25")
        capture(exe,sources,later,"3.25")
        for name in FILES:
            with Image.open(first/(name+".png")) as a, Image.open(later/(name+".png")) as b:
                evaluate(a,b,name)
        # Build isolated copies under the same relative include topology.
        sandbox=tmp/"fixture"; shader_dir=sandbox/"shaders/root"
        shader_dir.mkdir(parents=True)
        (sandbox/"stdlib").symlink_to(ROOT/"data/stdlib",target_is_directory=True)
        names=[]
        for stem,(shader,param,value,threshold) in VARIANTS.items():
            path=shader_dir/(stem+".effect")
            path.write_text(variant_source(shader,param,value))
            names.append(path)
        variant_output=tmp/"variants"
        capture(exe,names,variant_output,"1.25")
        for stem,(shader,param,value,threshold) in VARIANTS.items():
            with Image.open(first/(shader+".png")) as base, Image.open(variant_output/(stem+".png")) as changed:
                delta=mean_difference(base,changed)
                print(f"{shader} {param}={value}: pixel delta={delta:.3f}",flush=True)
                assert delta > threshold, f"{shader}: {param} control has no meaningful effect"
    print("PASS: native shader color, spatial structure, time and six visible controls")

if __name__ == "__main__":
    main()
