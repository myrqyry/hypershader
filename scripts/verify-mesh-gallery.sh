#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hypershader.so}"
data="${2:-$repo/data}"
work="$(mktemp -d /tmp/hyper-mesh-gallery-XXXXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -w -I"$inc" "$repo/scripts/verify-stdlib-loader.cpp" \
  -o "$work/probe" $(pkg-config --libs libobs)
mkdir -p "$work/home"
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hypershader/bin/64bit:${LD_LIBRARY_PATH:-}"
export MESA_SHADER_CACHE_DISABLE=true
(cd "$work" && HOME="$work/home" XDG_CONFIG_HOME="$work/home" \
 xvfb-run -a timeout 40 "$work/probe" "$module" "$data" \
 "$data/shaders/root/3d_mesh_gallery.effect" audit) > "$work/output.log" 2>&1 || {
 tail -35 "$work/output.log"
 exit 1
}
if grep -Eq 'Shader load failed|Error creating shader|set to invalid size' "$work/output.log"; then
 grep -A8 -B2 -E 'Shader load failed|Error creating shader|set to invalid size' "$work/output.log" | head -40
 exit 1
fi
grep -q 'Loaded shader: shaders/root/3d_mesh_gallery.effect' "$work/output.log" || {
 echo 'FAIL: shader not loaded'; tail -30 "$work/output.log"; exit 1;
}
python3 - "$work/output.log" <<'PY'
import re, sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
m=re.search(r'SHADER_AUDIT samples=(\d+) mean_rgb=([\d.]+) delta_rgb=([\d.]+) min_rgb=(\d+) max_rgb=(\d+) modified_frames=(\d+)',s)
assert m,'No GPU readback'
n,mean,delta,lo,hi,modified=map(float,m.groups())
assert n>=8 and modified>=8 and 8<mean<244 and hi-lo>25,(n,mean,lo,hi,modified)
print(f'MESH_GALLERY_GPU frames={int(n)} mean_rgb={mean:.2f} delta={delta:.2f} dynamic_range={int(hi-lo)} PASS=1')
PY
# Same real OBS filter tested under four camera/mesh/light configurations.
g++ -std=c++17 -O2 -w -I"$inc" "$repo/scripts/verify-mesh-gallery-controls.cpp" \
  -o "$work/controls" $(pkg-config --libs libobs)
(cd "$work" && HOME="$work/home" XDG_CONFIG_HOME="$work/home" \
 xvfb-run -a timeout 35 "$work/controls" "$module" "$data" \
 "$data/shaders/root/3d_mesh_gallery.effect") > "$work/controls.log" 2>&1 || {
 echo 'FAIL: mesh gallery options are not changing rendered pixels'
 grep -E 'MESH_GALLERY_CONTROLS|Shader load failed|error:' "$work/controls.log" | tail -30
 exit 1
}
grep 'MESH_GALLERY_CONTROLS' "$work/controls.log"
