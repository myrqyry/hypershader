#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
cache="$repo/build_x86_64/CMakeCache.txt"
if [[ ! -f "$cache" ]]; then
  echo "First configure build_x86_64 with USE_ONNX_RUNTIME=ON" >&2
  exit 2
fi
ort_inc="$(sed -n 's/^ONNXRUNTIME_INCLUDE:PATH=//p' "$cache")"
ort_lib="$(sed -n 's/^ONNXRUNTIME_LIB:FILEPATH=//p' "$cache")"
if [[ ! -d "$ort_inc" || ! -f "$ort_lib" ]]; then
  echo "MediaPipe runtime test requires configured ONNX Runtime headers/library" >&2
  exit 2
fi
work="$(mktemp -d /tmp/hyper-mp-probe-XXXXXX)"
trap 'rm -rf "$work"' EXIT
g++ -std=c++17 -O2 -DUSE_ONNX_MEDIAPIPE -I"$repo/src" -I"$ort_inc" -I"$(pkg-config --variable=includedir libobs)/obs" \
  "$repo/scripts/verify-mediapipe-runtime.cpp" "$repo/src/mediapipe/mp_wrapper.cpp" \
  -o "$work/probe" $(pkg-config --libs libobs) "$ort_lib" \
  -Wl,-rpath,"$(dirname "$ort_lib")" -pthread
if [[ -n "${1:-}" ]]; then
  "$work/probe" "$repo/data/models/mediapipe" "$1"
else
  "$work/probe" "$repo/data/models/mediapipe"
fi
