#!/usr/bin/env bash
# Deterministic pure-C musical timing test (no OBS or GUI required).
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
tmp="$(mktemp -d /tmp/hyper-music-clock-XXXXXX)"
trap 'rm -rf "$tmp"' EXIT
"${CC:-cc}" -std=c17 -O2 -Wall -Wextra -Werror "$repo/scripts/verify-music-timing.c" \
    "$repo/src/audio/music_timing.c" -lm -o "$tmp/verify"
"$tmp/verify"
