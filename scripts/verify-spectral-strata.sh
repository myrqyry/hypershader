#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hypershader.so}"
data="${2:-$repo/data}"
work="$(mktemp -d /tmp/hyper-spectral-strata-XXXXXX)"
# Mesa cache writes can outlive the probe under llvmpipe and race cleanup.
# Disabling the cache keeps both test output and temporary-dir cleanup stable.
export MESA_SHADER_CACHE_DISABLE=true
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -I"$inc" "$repo/scripts/verify-spectral-strata.cpp" \
    -o "$work/probe" $(pkg-config --libs libobs)
(cd "$work" && HOME="$work" XDG_CONFIG_HOME="$work" \
    xvfb-run -a timeout 40 "$work/probe" "$module" "$data") >"$work/result.log" 2>&1 || {
    grep -E 'SPECTRAL_STRATA|AUDIO_UI|Loaded shader|Shader load|error:|Failed|ASSERT|SOUND' "$work/result.log" | tail -25 || true
    tail -45 "$work/result.log"
    exit 1
}
grep '^SPECTRAL_STRATA' "$work/result.log"
grep -q 'SPECTRAL_STRATA .*PASS=1' "$work/result.log"
