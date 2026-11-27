#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
c++ -std=c++17 -O2 -I"$inc" "$repo/scripts/verify-effect-hotkeys.cpp" -o "$work/probe" $(pkg-config --libs libobs)
(cd "$work" && xvfb-run -a timeout 30 "$work/probe" "${1:-$repo/build_x86_64/hypershader.so}" "${2:-$repo/data}" "${3:-hypershader_filter}")
