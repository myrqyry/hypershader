#!/usr/bin/env bash
# Native libobs/OpenGL verification of the six original instruments.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
python3 "$repo/scripts/verify-original-instruments.py"
if [[ $# -ne 1 ]]; then echo "Usage: $0 OUTPUT_DIRECTORY" >&2;exit 2;fi
work="$1"
mkdir -p "$work"
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -Wall -Wextra -I"$inc" \
 "$repo/scripts/verify-original-instruments-obs.cpp" -o "$work/verify-original" \
 $(pkg-config --libs libobs libpng)
xvfb-run -a -s '-screen 0 1280x720x24' timeout 40 \
 "$work/verify-original" "$repo/data/shaders/root" "$work"
