#!/usr/bin/env python3
"""Pack a verified portable HypBR bundle into one repeat-safe OBS texture."""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image

PIN = "83a0cb859ad03324185632019402ba01ba31f242"
MAPS = ("albedo_delit.png", "normal.png", "orm.png")

def pack(source: Path, output: Path):
    if output.exists():
        raise ValueError("Output already exists")
    receipt = json.loads((source / "material.json").read_text())
    size = receipt["recipe"]["size"]
    if (receipt["schema"] != 1 or receipt["engine"] != "hypbr" or
        receipt["engine_commit"] != PIN or receipt["verification"]["passed"] is not True or
        size not in (512, 1024, 2048)):
        raise ValueError("Expected verified pinned HypBR bundle")
    # One-pixel periodic gutters make bilinear filtering safe across repeat
    # seams and prevent any interpolation into the adjacent map.
    pitch = size + 2
    atlas = Image.new("RGB", (pitch * 3, pitch))
    for i, filename in enumerate(MAPS):
        path = source / filename
        if hashlib.sha256(path.read_bytes()).hexdigest() != receipt["maps"][filename]:
            raise ValueError("Map hash mismatch: " + filename)
        with Image.open(path) as decoded:
            if decoded.size != (size, size):
                raise ValueError("Invalid map dimensions: " + filename)
            img = decoded.convert("RGB")
        x = i * pitch
        atlas.paste(img, (x+1, 1))
        atlas.paste(img.crop((size-1,0,size,size)), (x,1))
        atlas.paste(img.crop((0,0,1,size)), (x+size+1,1))
        atlas.paste(img.crop((0,size-1,size,size)), (x+1,0))
        atlas.paste(img.crop((0,0,size,1)), (x+1,size+1))
        for xx, yy, sx, sy in ((0,0,size-1,size-1),(size+1,0,0,size-1),
                                 (0,size+1,size-1,0),(size+1,size+1,0,0)):
            atlas.putpixel((x+xx,yy),img.getpixel((sx,sy)))
    with output.open("xb") as stream:
        atlas.save(stream, format="PNG")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    pack(args.bundle,args.output)
