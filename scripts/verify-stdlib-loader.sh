#!/usr/bin/env bash
# Isolated real libobs loader tests; never opens or modifies the user's OBS.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="${1:-$repo/build_x86_64/hypershader.so}"
data="$repo/data"
work="$(mktemp -d /tmp/hyper-stdlib-verification-XXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
libdir="$(pkg-config --variable=libdir libobs)"
g++ -std=c++17 -O2 -I"$inc" "$repo/scripts/verify-stdlib-loader.cpp" \
    -o "$work/test-stdlib" $(pkg-config --libs libobs) -Wl,-rpath,"$libdir"
mkdir -p "$work/hypershader"
export LD_LIBRARY_PATH="$HOME/.config/obs-studio/plugins/hypershader/bin/64bit:${LD_LIBRARY_PATH:-}"
read -r -a modes <<< "${STDLIB_TEST_MODES:-math wrapper managed vector scalar macro bloom bloom_off sampling compat missing apply}"
for mode in "${modes[@]}"; do
    echo "TEST: stdlib $mode"
    shader="$data/shaders/tests/stdlib_${mode}_probe.effect"
    if [[ "$mode" == "missing" ]]; then
        shader="$data/shaders/tests/stdlib_missing_include_probe.effect"
    elif [[ "$mode" == "compat" ]]; then
        shader="$data/shaders/tests/import/shadertoy_probe.effect"
    elif [[ "$mode" == "managed" ]]; then
        shader="$data/shaders/tests/stdlib_wrapper_probe.effect"
    elif [[ "$mode" == "apply" ]]; then
        shader="$data/shaders/tests/stdlib_math_probe.effect"
    fi
    (cd "$work" && HOME="$work/home" XDG_CONFIG_HOME="$work/config" MESA_SHADER_CACHE_DISABLE=true \
      xvfb-run -a -s '-screen 0 1280x720x24' timeout 18 \
      "$work/test-stdlib" "$module" "$data" "$shader" "$mode") \
      >"$work/${mode}.log" 2>&1 || {
        tail -35 "$work/${mode}.log"
        exit 1
      }
    if [[ "$mode" == "missing" ]]; then
      grep -q 'Required #include failed: ../../stdlib/this_library_module_does_not_exist.glsl' "$work/${mode}.log" ||
        { echo "FAIL: missing include was not a hard, explicit error"; exit 1; }
      if grep -q 'Loaded shader: shaders/tests/stdlib_missing_include_probe.effect' "$work/${mode}.log"; then
        echo 'FAIL: missing include yielded a loaded effect'; exit 1
      fi
    elif [[ "$mode" == "managed" ]]; then
      grep -q 'Loaded shader: managed/builder-applied/11111111-1111-1111-1111-111111111111.effect' "$work/$mode.log" ||
        { echo "FAIL: isolated managed shader did not compile and load"; exit 1; }
      grep -q 'STDLIB_LOADER mode=managed.*PASS=1' "$work/$mode.log" ||
        { echo "FAIL: managed shader did not render correct pixels"; exit 1; }
    elif [[ "$mode" == "compat" ]]; then
      grep -q "Loaded shader: shaders/tests/import/shadertoy_probe.effect" "$work/$mode.log"
    elif [[ "$mode" == "apply" ]]; then
      grep -q "Loaded shader: shaders/tests/stdlib_wrapper_probe.effect" "$work/$mode.log" ||
        { echo "FAIL: updated shader never loaded"; exit 1; }
      grep -q "apply_submitted=1" "$work/$mode.log" ||
        { echo "FAIL: source update did not persist"; exit 1; }
    else
      if [[ "$mode" == "math" ]] && grep -q '\[hypershader\] ├─ Name : ../../stdlib/does_not_exist.glsl' "$work/${mode}.log"; then
        echo "FAIL: metadata parser treated a comment as a real include"; exit 1
      fi
      grep -q "Loaded shader: shaders/tests/stdlib_${mode}_probe.effect" "$work/${mode}.log" ||
        { echo "FAIL: $mode effect never loaded"; exit 1; }
    fi
    grep 'STDLIB_LOADER' "$work/${mode}.log"
done
echo 'PASS: stdlib imports, wrapper, missing dependency and isolated per-instance shader Apply'
