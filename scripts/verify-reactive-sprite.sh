#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_qt_reload/hypershader.so}"
data="${2:-$repo/data}"
work="$(mktemp -d /tmp/hyper-reactive-sprite-XXXXXX)"
trap 'rm -rf "$work"' EXIT
for kind in source filter; do
  test -f "$data/shaders/root/$kind.reactive_sprite.effect" || { echo "Missing $kind.reactive_sprite.effect"; exit 1; }
done
python3 - "$work" <<'PY'
import struct, zlib, sys
from pathlib import Path
def chunk(n,p):
    return struct.pack(">I",len(p))+n+p+struct.pack(">I",zlib.crc32(n+p))
for name,rgb in [('red',(255,0,0)),('green',(0,255,0))]:
    pixels=b''.join(b'\0'+b''.join(bytes((*rgb,255)) if 8<=x<56 and 8<=y<56 else b'\0'*4 for x in range(64)) for y in range(64))
    Path(sys.argv[1],name+'.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',64,64,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(pixels))+chunk(b'IEND',b''))
# Atlas with non-square cells and conspicuous neighbors.
colors=[(255,0,255),(0,255,0),(0,0,255),(255,255,0)]
w,h=128,64
pixels=b''.join(b'\0'+b''.join(bytes((*colors[(y//32)*2+x//64],255)) for x in range(w)) for y in range(h))
Path(sys.argv[1],'atlas.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(pixels))+chunk(b'IEND',b''))
PY
printf %s R0lGODlhQABAAIEAAAD/AAAAAAAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQAKAAAACwAAAAAQABAAAAIaQABCBxIsKDBgwgTKlzIsKHDhxAjSpxIsaLFixgzatzIsaPHjyBDihxJsqTJkyhTqlzJsqXLlzBjypxJs6bNmzhz6tzJs6fPn0CDCh1KtKjRo0iTKl3KtKnTp1CjSp1KtarVq1izagUQEAAh+QQBKAABACwAAAAAQABAAIEAAP8AAAAAAAAAAAAIaQABCBxIsKDBgwgTKlzIsKHDhxAjSpxIsaLFixgzatzIsaPHjyBDihxJsqTJkyhTqlzJsqXLlzBjypxJs6bNmzhz6tzJs6fPn0CDCh1KtKjRo0iTKl3KtKnTp1CjSp1KtarVq1izagUQEAAh+QQBKAABACwAAAAAQABAAIEA//8AAAAAAAAAAAAIaQABCBxIsKDBgwgTKlzIsKHDhxAjSpxIsaLFixgzatzIsaPHjyBDihxJsqTJkyhTqlzJsqXLlzBjypxJs6bNmzhz6tzJs6fPn0CDCh1KtKjRo0iTKl3KtKnTp1CjSp1KtarVq1izagUQEAA7 | base64 -d > "$work/expression.gif"
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -I"$inc" "$repo/scripts/verify-reactive-sprite.cpp" -o "$work/probe" $(pkg-config --libs libobs)
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hypershader/bin/64bit:${LD_LIBRARY_PATH:-}"
for mode in ${HS_REACTION_TEST_MODES:-source filter source-motion filter-motion source-timing source-instant filter-timing filter-instant source-stale filter-stale source-instant-stale source-mapped source-gif filter-gif source-gif-idle source-atlas filter-atlas source-atlas-idle source-atlas-burst source-atlas-strong filter-atlas-accent}; do
  kind="${mode%%-*}"
  effect="$data/shaders/root/$kind.reactive_sprite.effect"
  expected="shaders/root/$kind.reactive_sprite.effect"
  if [[ "$mode" == *-mapped ]]; then effect="$data/shaders/tests/reaction_mapped_spring.effect"; expected="shaders/tests/reaction_mapped_spring.effect"; fi
  mkdir "$work/$mode"
  (cd "$work/$mode" && HOME="$work/$mode" XDG_CONFIG_HOME="$work/$mode" xvfb-run -a timeout 45 "$work/probe" "$module" "$data" "$effect" "$work" "$mode") >"$work/$mode.log" 2>&1 || { tail -60 "$work/$mode.log"; exit 1; }
  grep "^REACTIVE_SPRITE " "$work/$mode.log"
  grep -q "Loaded shader: $expected" "$work/$mode.log"
  ! grep -Eq 'Shader load failed|set to invalid size' "$work/$mode.log"
done
