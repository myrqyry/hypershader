#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
data = (root / "src/hypershader_data.h").read_text()
filter_src = (root / "src/filter-hypershader.c").read_text()
plugin = (root / "src/plugin-main.c").read_text()
cmake = (root / "CMakeLists.txt").read_text()

assert "channel_image_compat.h" in data
assert "hs_channel_image_t channel_images[4]" in data
for forbidden in (
    "gs_image_file_t channel_images",
    "gs_image_file_free(&s->channel_images",
    "gs_image_file_init(&s->channel_images",
    "gs_image_file_init_texture(&s->channel_images",
):
    assert forbidden not in data + filter_src, f"direct OBS image ABI remains: {forbidden}"

assert "hs_channel_image_runtime_init()" in plugin
assert "src/channel_image_compat.c" in cmake
compat = (root / "src/channel_image_compat.c").read_text()
for symbol in (
    '"gs_image_file_init"',
    '"gs_image_file_free"',
    '"gs_image_file_init_texture"',
    '"gs_image_file_ex_init"',
    '"gs_image_file_ex_free"',
    '"gs_image_file_ex_init_texture"',
):
    assert symbol in compat, f"runtime symbol missing: {symbol}"
assert "obs_get_version()" in compat
assert "MAKE_SEMANTIC_VERSION(33, 0, 0)" in compat
print("PASS: channel images use runtime OBS <=32/33 ABI selection")

header = (root / "src/channel_image_compat.h").read_text()
assert "max_align_t" not in header
assert "void *storage;" in header
compat = (root / "src/channel_image_compat.c").read_text()
assert "bzalloc(HS_CHANNEL_IMAGE_STORAGE_SIZE)" in compat
assert "bfree(image->storage)" in compat
# GLVND dependencies now come from the immutable shared caller workflow.
workflow = (root / ".github/workflows/obs33-compatibility.yml").read_text()
assert "myrqyry/obs-plugin-ci/.github/workflows/build.yml@70ce37b1015df80478f8ed559f5c88bf4007233f" in workflow
assert "driver: caller" in workflow
builder = (root / ".github/scripts/prepare-obs32-sdk.sh").read_text()
assert "--target obs-frontend-api libobs-opengl" in builder, (
    "OBS33 SDK install requires both frontend API and graphics backend"
)
