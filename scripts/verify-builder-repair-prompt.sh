#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d /tmp/hyper-repair-prompt-XXXXXXXX)"
trap 'rm -rf "$work"' EXIT
g++ -std=c++17 -O2 -Wall -Wextra $(pkg-config --cflags Qt6Core) \
  "$root/scripts/verify-builder-repair-prompt.cpp" -o "$work/test" $(pkg-config --libs Qt6Core)
"$work/test"
