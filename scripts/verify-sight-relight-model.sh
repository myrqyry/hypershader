#!/usr/bin/env bash
# Actual model test, requires existing local Sight module and model.
set -euo pipefail
if [[ $# != 5 ]]; then
    echo "Usage: $0 SIGHT_MODULE SIGHT_DATA HYPER_MODULE HYPER_DATA DEPTH_MODEL" >&2
    exit 2
fi
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d /tmp/hs-relight-model-XXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -pthread -I"$inc" -I"$repo/src" "$repo/scripts/verify-sight-relight-model-obs.cpp" -o "$work/probe" $(pkg-config --libs libobs)
xvfb-run -a timeout 60 "$work/probe" "$@" "$repo/data/shaders/root/filter.sight_relight.effect"
