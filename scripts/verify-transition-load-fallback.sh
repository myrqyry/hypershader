#!/usr/bin/env bash
# Real libobs failure-path test: missing effect must display scenes without GL shader errors.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="$repo/build_x86_64/hyperShaderFX.so"
data="$repo/data"
test -s "$module"
temp="$(mktemp -d /tmp/hyper-transition-fallback-XXXXXXXX)"
trap 'rm -rf "$temp"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -I"$inc" \
  "$repo/scripts/verify-warp-dither-transition.cpp" -o "$temp/test" $(pkg-config --libs libobs)
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hyperShaderFX/bin/64bit:${LD_LIBRARY_PATH:-}"
export MESA_SHADER_CACHE_DISABLE=true
# The absolute .effect path deliberately does not exist, so shader creation fails.
# A missing shader must still fall back safely instead of drawing without a vertex shader.
if ! (cd "$temp" && HOME="$temp" XDG_CONFIG_HOME="$temp" xvfb-run -a timeout 40 \
  "$temp/test" "$module" "$data" "$temp/does-not-exist.effect" --expect-fallback \
  >"$temp/obs.log" 2>&1); then
  tail -45 "$temp/obs.log"
  exit 1
fi
if grep -Eq 'No vertex shader specified|device_draw \(GL\) failed' "$temp/obs.log"; then
  echo 'FAIL: missing-effect transition issued an unbound draw call'
  grep -m 5 -E 'No vertex shader specified|device_draw \(GL\) failed' "$temp/obs.log"
  exit 1
fi
grep 'TRANSITION_MISSING_SHADER_FALLBACK.*PASS=1' "$temp/obs.log"

# Existing files outside trusted shader roots are forbidden. The UI diagnostic
# must say so, while the renderer still displays A/B without GL draw errors.
cp "$repo/data/shaders/root/transition.warp_dither.effect" "$temp/external.effect"
if ! (cd "$temp" && HOME="$temp" XDG_CONFIG_HOME="$temp" xvfb-run -a timeout 40 \
  "$temp/test" "$module" "$data" "$temp/external.effect" --expect-fallback \
  >"$temp/external.log" 2>&1); then
  tail -45 "$temp/external.log"
  exit 1
fi
if grep -Eq 'No vertex shader specified|device_draw \(GL\) failed' "$temp/external.log"; then
  echo 'FAIL: blocked external shader issued an unbound draw call'
  exit 1
fi
if ! grep -q 'outside plugin data directory.*rejected' "$temp/external.log"; then
  echo 'FAIL: external shader is not described as rejected'
  grep -m 4 -E 'SECURITY:' "$temp/external.log" || true
  exit 1
fi
grep 'TRANSITION_MISSING_SHADER_FALLBACK.*PASS=1' "$temp/external.log"
