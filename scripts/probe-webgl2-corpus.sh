#!/usr/bin/env bash
# Optional diagnostic probe; no third-party shaders are checked into the project.
# Usage: scripts/probe-webgl2-corpus.sh /path/to/llm-shader-toy/shaders
# Optional HYPER_CORPUS_REPORT_DIR=/path/to/reports persists the TSV results.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
source_dir="${1:?Pass the upstream directory containing .glsl files}"
module="${2:-$root/build_x86_64/hypershader.so}"
data="${3:-$root/data}"
source_dir="$(realpath "$source_dir")"
module="$(realpath "$module")"
data="$(realpath "$data")"
work="$(mktemp -d /tmp/hyper-webgl2-probe-XXXXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -w -I"$inc" "$root/scripts/probe-webgl2-native.cpp" -o "$work/compile" $(pkg-config --libs libobs)
python3 "$root/scripts/probe-webgl2-import.py" "$source_dir" "$work/raw" --mode raw
python3 "$root/scripts/probe-webgl2-import.py" "$source_dir" "$work/wrapped" --mode wrapped
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hypershader/bin/64bit:${LD_LIBRARY_PATH:-}"
for mode in raw wrapped; do
  mkdir -p "$work/home-$mode"
  (cd "$work" && HOME="$work/home-$mode" XDG_CONFIG_HOME="$work/home-$mode" \
    xvfb-run -a timeout 110 "$work/compile" "$module" "$data" "$work/$mode" "$work/$mode.tsv") \
    >"$work/$mode.log" 2>&1 || {
      echo "Native libobs probe failed: $mode" >&2
      tail -35 "$work/$mode.log" >&2
      exit 1
    }
  awk -F '\t' -v mode="$mode" 'NR>1{n++;if($2=="succeeded")yes++;else print mode" fail " $1 ": " substr($3,1,130)} END{printf "%s NATIVE_COMPILED=%d TOTAL=%d\n",mode,yes,n; if(n==0)exit 1;}' "$work/$mode.tsv"
done
if [[ -n "${HYPER_CORPUS_REPORT_DIR:-}" ]]; then
  mkdir -p "$HYPER_CORPUS_REPORT_DIR"
  cp "$work/raw.tsv" "$work/wrapped.tsv" "$HYPER_CORPUS_REPORT_DIR/"
fi
