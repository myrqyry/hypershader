#!/usr/bin/env bash
# Exercises the real native HyperShader graph, moving footage, and shader controls.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hypershader.so}"
data="${2:-$repo/data}"
shader="$data/shaders/root/filter.motion_mosh.effect"
test -f "$shader"
python3 - "$shader" <<'PYQ'
from pathlib import Path
import sys
text = Path(sys.argv[1]).read_text()
assert "float motionNeutral() { return 128.0/255.0; }" in text
assert "decodeMotion(" in text and "encodeMotion(" in text
assert "(field.rg-0.5)" not in text
neutral = 128.0 / 255.0
stored = round(neutral * 255.0) / 255.0
decoded = (stored - neutral) * 64.0
assert abs(decoded) < 1e-12, decoded
PYQ
work="$(mktemp -d /tmp/hyper-motion-mosh-XXXXXX)"
trap 'rm -rf "$work"' EXIT
g++ -std=c++17 -O2 -I"$(pkg-config --variable=includedir libobs)/obs" \
  "$repo/scripts/verify-creative-graph-shaders.cpp" -o "$work/test" $(pkg-config --libs libobs)
export MESA_SHADER_CACHE_DISABLE=true
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hypershader/bin/64bit:${LD_LIBRARY_PATH:-}"
mkdir -p "$work/home"
(cd "$work" && HOME="$work/home" XDG_CONFIG_HOME="$work/home" \
 xvfb-run -a timeout 30 "$work/test" "$module" "$data" "$shader" clean) > "$work/out.log" 2>&1 || {
  tail -65 "$work/out.log"; exit 1;
}
grep -q 'Two-buffer graph enabled: A -> B -> Draw' "$work/out.log"
grep -q 'Loaded shader: shaders/root/filter.motion_mosh.effect' "$work/out.log"
grep -q 'NEW_GRAPH_SHADER=.*PASS=1' "$work/out.log"
grep -q 'MODE_VERIFY=clean .*PASS=1' "$work/out.log"
grep -q 'MOTION_UI_SETTINGS=.*expected=12 present=12 PASS=1' "$work/out.log"
grep '^MOTION_UI_SETTINGS=' "$work/out.log"
grep '^MODE_VERIFY=' "$work/out.log"
if grep -Eq 'Shader load failed|set to invalid size|missing pixel shader' "$work/out.log"; then
  tail -65 "$work/out.log"; exit 1
fi
grep 'NEW_GRAPH_SHADER=' "$work/out.log"
(cd "$work" && HOME="$work/home" XDG_CONFIG_HOME="$work/home" \
 xvfb-run -a timeout 30 "$work/test" "$module" "$data" "$shader" vectors) > "$work/vectors.log" 2>&1 || {
  tail -65 "$work/vectors.log"; exit 1;
}
grep -q 'MOTION_UI_SETTINGS=.*expected=12 present=12 PASS=1' "$work/vectors.log"
grep -q 'MODE_VERIFY=vectors .*PASS=1' "$work/vectors.log"
grep -q 'NEW_GRAPH_SHADER=.*PASS=1' "$work/vectors.log"
grep '^MODE_VERIFY=' "$work/vectors.log"
echo 'MOTION_MOSH_NATIVE_GPU_PASS=1'
