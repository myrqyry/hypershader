#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d /tmp/hs-relight-XXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -I"$inc" "$repo/scripts/verify-sight-relight-obs.cpp" -o "$work/probe" $(pkg-config --libs libobs)
xvfb-run -a timeout 25 "$work/probe" "$repo/data/shaders/root/filter.sight_relight.effect"
