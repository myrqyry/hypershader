#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d /tmp/hs-original-plugin-XXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -pthread -I"$inc" "$repo/scripts/verify-original-instruments-plugin.cpp" \
 -o "$work/probe" $(pkg-config --libs libobs)
module="${HYPERSHADER_MODULE:-$repo/build_qt_reload/hypershader.so}"
data="${HYPERSHADER_DATA:-$repo/data}"
if ! xvfb-run -a -s '-screen 0 1280x720x24' timeout 36 \
 "$work/probe" "$module" "$data" "$data/shaders/root" > "$work/probe.log" 2>&1; then
  tail -60 "$work/probe.log"
  exit 1
fi
if grep -E "Parameter '(iAudio|audio_)[^']*' set to invalid size" "$work/probe.log"; then
  echo "Native audio uniform was not bound before shader draw" >&2
  exit 1
fi
grep -E '^ORIGINAL_(PLUGIN|INSTRUMENT)' "$work/probe.log"
