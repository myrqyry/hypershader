#!/usr/bin/env python3
"""Prevent synchronous PNG encoding while libobs's GPU lock is held."""
from pathlib import Path

src = (Path(__file__).resolve().parent / "render-rikiya-thumbnails.cpp").read_text()
main = src.index("int main(")
capture = src[src.index("bool thumbnail("):main]
dispatch = src[main:]
assert "png_image_write_to_file" not in capture, "GPU readback must not write PNGs"
assert "png_image_write_to_file" in dispatch, "still need real PNG serialization"
assert dispatch.index("obs_leave_graphics();") < dispatch.index("png_image_write_to_file")
print("PREVIEW_PNG_OUTSIDE_GRAPHICS_LOCK_PASS")
