#!/usr/bin/env bash
# This exercises the actual installed-style example through the HyperShader
# filter, not direct shader compilation or a substitute renderer.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hypershader.so}"
data="${2:-$repo/data}"
work="$(mktemp -d /tmp/hyper-ink-example-XXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -w -I"$inc" "$repo/scripts/verify-stdlib-loader.cpp" \
  -o "$work/test" $(pkg-config --libs libobs)
mkdir -p "$work/home" "$work/config"
export MESA_SHADER_CACHE_DISABLE=true
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hypershader/bin/64bit:${LD_LIBRARY_PATH:-}"
(cd "$work" && HOME="$work/home" XDG_CONFIG_HOME="$work/config" \
 xvfb-run -a timeout 23 "$work/test" "$module" "$data" \
 "$data/shaders/root/ink_reaction_buffers.effect" audit) >"$work/output.log" 2>&1 || {
  tail -35 "$work/output.log"; exit 1;
}
grep -q 'Two-buffer graph enabled: A -> B -> Draw' "$work/output.log"
grep -q 'Loaded shader: shaders/root/ink_reaction_buffers.effect' "$work/output.log"
if grep -Eq 'Shader load failed|set to invalid size' "$work/output.log"; then
 tail -35 "$work/output.log"; exit 1;
fi
python3 - "$work/output.log" <<'PY'
import re,sys
from pathlib import Path
out=Path(sys.argv[1]).read_text()
m=re.search(r'SHADER_AUDIT samples=(\d+) mean_rgb=([\d.]+) delta_rgb=([\d.]+) min_rgb=(\d+) max_rgb=(\d+) modified_frames=(\d+)',out)
assert m, 'missing native pixel readback'
frames,mean,delta,lo,hi,changed=map(float,m.groups())
assert frames>=8 and changed>=8,(frames,changed)
assert 10<mean<180 and delta>6,(mean,delta)
print(f'INK_GRAPH_REAL_FILTER samples={int(frames)} mean_rgb={mean:.2f} delta_rgb={delta:.2f} PASS=1')
PY
