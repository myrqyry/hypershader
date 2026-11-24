#!/usr/bin/env python3
"""Native libobs/GPU regressions for landmark-dependent Funny Face effects."""
import os
import subprocess
import tempfile
from pathlib import Path
from PIL import Image,ImageChops,ImageStat

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/"data/shaders/root/fun_face.effect"
MODES=("base","bypass","enhanced","blank","mouth","laser","eyes","blush","sparkles","fallback")

def rgb_delta(left,right):
    return sum(ImageStat.Stat(ImageChops.difference(left.convert("RGB"),right.convert("RGB"))).mean)/3

def run():
    with tempfile.TemporaryDirectory(prefix="hyper-fun-face-") as t:
        tmp=Path(t); exe=tmp/"probe"
        inc=subprocess.check_output(["pkg-config","--variable=includedir","libobs"],text=True).strip()+"/obs"
        libs=subprocess.check_output(["pkg-config","--libs","libobs","libpng"],text=True).split()
        subprocess.run(["g++","-std=c++17","-O2",f"-I{inc}",str(ROOT/"scripts/verify-fun-face-gpu.cpp"),"-o",str(exe),*libs],check=True)
        for mode in MODES:
            dest=tmp/mode;dest.mkdir()
            p=subprocess.run(["xvfb-run","-a","-s","-screen 0 1280x720x24","timeout","25",
                    str(exe),str(SOURCE),str(dest),mode],
                    env={**os.environ,"MESA_SHADER_CACHE_DISABLE":"true"},
                    capture_output=True,text=True,timeout=35)
            assert p.returncode==0, mode+" GPU compile/render error: "+p.stderr[-1700:]+p.stdout[-600:]
            assert "FACE_CAPTURE_PASS" in p.stdout,mode
        images={}
        for mode in MODES:
            with Image.open(tmp/mode/(mode+"-0.png")) as im:images[mode]=im.copy()
        baseline=images["bypass"]
        for mode,im in images.items():
            assert im.getchannel("A").getextrema()==(255,255),mode+" unexpected alpha"
        for mode,threshold in (("base",0.1),("mouth",0.2),("laser",0.2),
                               ("eyes",0.2),("blush",0.2),("sparkles",0.05),("enhanced",1.5)):
            change=rgb_delta(baseline,images[mode])
            print(f"{mode}: delta against native camera-only image = {change:.3f}",flush=True)
            assert change>threshold,mode+" effect has no visible response"
        source_fallback=rgb_delta(images["base"],images["fallback"])
        print(f"unconfigured iChannel0: difference from channel input = {source_fallback:.3f}",flush=True)
        assert source_fallback<0.5, "unconfigured channel should fall back to OBS image"
        missing=rgb_delta(baseline,images["blank"])
        print(f"missing landmarks: bypass RGB difference {missing:.3f}",flush=True)
        assert missing<0.3, "missing landmarks must not draw fake effects in frame corner"
        with Image.open(tmp/"sparkles"/"sparkles-2.png") as later:
            tdelta=rgb_delta(images["sparkles"],later)
            changed=ImageChops.difference(images["sparkles"].convert("RGB"),later.convert("RGB"))
            changed_pixels=sum(max(v)>9 for v in changed.getdata())
            print(f"sparkle animation mean RGB delta={tdelta:.3f}, changed pixels={changed_pixels}",flush=True)
            assert tdelta>0.009 and changed_pixels>20,"sparkles fail to animate with time"
        print("PASS: face shader GPU rendering, bypass, landmark absence, 6 effects and animation")
if __name__=="__main__":
    run()
