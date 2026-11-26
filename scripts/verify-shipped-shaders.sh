#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="$(realpath "${1:-$repo/build_x86_64/hypershader.so}")"
data="$(realpath "${2:-$repo/data}")"
work="$(mktemp -d /tmp/hs-shipped-audit.XXXXXX)"
export MESA_SHADER_CACHE_DISABLE=true
trap 'rm -rf "$work"' EXIT
mkdir "$work/home"
g++ -std=c++17 -O2 -I"$(pkg-config --variable=includedir libobs)/obs" -I"$repo/src" "$repo/scripts/verify-shipped-shaders.cpp" -o "$work/probe" $(pkg-config --libs libobs)
status=0
(cd "$work/home" && HOME="$PWD" XDG_CONFIG_HOME="$PWD" LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a timeout 120 "$work/probe" "$module" "$data") >"$work/log" 2>&1 || status=$?
grep -E '^(SHIPPED|PLAYGROUND)' "$work/log" || tail -60 "$work/log"
if [[ -n "${HS_AUDIT_LOG:-}" ]]; then cp "$work/log" "$HS_AUDIT_LOG"; fi
exit "$status"
