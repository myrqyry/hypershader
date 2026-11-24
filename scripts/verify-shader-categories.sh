#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d /tmp/hyper-category-unit-XXXXXXXX)"
trap 'rm -rf "$work"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I"$repo/src" "$repo/scripts/verify-shader-categories.cpp" -o "$work/verify"
"$work/verify"
