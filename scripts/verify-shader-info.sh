#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d /tmp/hyper-shader-info-XXXXXXXX)"
trap 'rm -rf "$work"' EXIT
cat > "$work/custom.effect" <<'EFFECT'
// @description "A useful shader"
// @notes "Use linear-color blending."
// @author "Artist One"
// @credits "Concept by Artist Two"
// @source "https://example.org/shaders/original"
// @license "CC-BY-4.0"
uniform float custom_factor;
// @author "Forged Name"
EFFECT
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I"$repo/src" \
 "$repo/scripts/verify-shader-info.cpp" -o "$work/unit"
"$work/unit" \
 "$repo/data/shaders/root/shabon/source.shabon_aurora_waves.effect" \
 "$repo/data/shaders/root/plasma.effect" \
 "/another/obs/plugin/data/shaders/root/shabon/source.shabon_aurora_waves.effect" \
 "$work/custom.effect"
