#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d /tmp/hypershader-browse-XXXXXX)"
trap 'rm -rf "$work"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -O2 -fPIC \
    $(pkg-config --cflags Qt6Core) \
    "$repo/scripts/verify-shader-library-browse.cpp" \
    -o "$work/catalog" $(pkg-config --libs Qt6Core)
"$work/catalog" "$repo/data/shaders"
