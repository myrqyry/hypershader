#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hypershader.so}"
data="${2:-$repo/data}"
work="$(mktemp -d /tmp/hyper-bool-default-XXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -w -I"$inc" "$repo/scripts/verify-stdlib-loader.cpp" \
 -o "$work/probe" $(pkg-config --libs libobs)
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hypershader/bin/64bit:${LD_LIBRARY_PATH:-}"
mkdir -p "$work/home"
(cd "$work" && HOME="$work/home" XDG_CONFIG_HOME="$work/home" \
 xvfb-run -a timeout 20 "$work/probe" "$module" "$data" \
 "$data/shaders/root/orphic_turing_bloom.effect" audit) > "$work/report.log" 2>&1 || {
 tail -20 "$work/report.log";exit 1;
}
grep -Fq "Loaded shader: shaders/root/orphic_turing_bloom.effect" "$work/report.log"
grep -Fq "name='reset_simulation' label='Reseed Simulation' has_default=1" "$work/report.log"
if grep -Eq "invalid size|Shader load failed|Error creating shader" "$work/report.log"; then
 grep -E 'invalid size|Shader load failed|Error creating shader' "$work/report.log" | head -7
 exit 1
fi
grep '^SHADER_AUDIT' "$work/report.log"
echo "BOOL_DEFAULT_NATIVE_PASS=1"
