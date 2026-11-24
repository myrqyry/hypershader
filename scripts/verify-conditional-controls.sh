#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d /tmp/hyper-condition-XXXXXX)"
trap 'rm -rf "$work"' EXIT
cc -std=c11 -O2 -I"$repo/src" -c "$repo/src/shader/shader_visibility.c" -o "$work/parser.o"
g++ -std=c++17 -O2 -I"$repo/src" "$repo/scripts/verify-conditional-controls.cpp" "$work/parser.o" -lm -o "$work/probe"
"$work/probe"
