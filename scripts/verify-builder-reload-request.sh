#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d /tmp/hyper-builder-reload-XXXXXXXX)"
trap 'rm -rf "$work"' EXIT
g++ -std=c++17 -O2 -I"$repo/src" "$repo/scripts/verify-builder-reload-request.cpp" \
  -o "$work/test" $(pkg-config --cflags --libs Qt6Core)
"$work/test"
