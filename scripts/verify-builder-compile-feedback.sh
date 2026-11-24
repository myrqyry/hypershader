#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d /tmp/hyper-builder-feedback-XXXXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -O2 -Wall -Wextra -I"$inc" "$root/scripts/verify-builder-compile-feedback.cpp" -o "$work/test" $(pkg-config --libs libobs)
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hyperShaderFX/bin/64bit:${LD_LIBRARY_PATH:-}"
mkdir -p "$work/home"
(cd "$work" && HOME="$work/home" XDG_CONFIG_HOME="$work/home" \
  xvfb-run -a timeout 25 "$work/test" "$root/build_x86_64/hyperShaderFX.so" \
  "$root/data" "$root/data/shaders/tests/stdlib_wrapper_probe.effect") > "$work/log" 2>&1 || {
    grep -E 'COMPILE_FEEDBACK|Shader load failed|Timed out|BUILDER_NATIVE|Loaded shader:|Failed|Preprocess|Preprocessing|Compiled preprocessed|Shader compile|ERROR|warn|Detail' "$work/log" | tail -60 || true
    exit 1
  }
# The stale-revision assertion only counts if the real runtime finished compiling
# the third shader, rather than remaining pending because work never ran.
grep -Fq 'Loaded shader: managed/builder-applied/33333333-3333-3333-3333-333333333333.effect' "$work/log"
grep '^BUILDER_NATIVE_COMPILE_FEEDBACK_PASS=1' "$work/log"
grep '^COMPILE_FEEDBACK' "$work/log"
