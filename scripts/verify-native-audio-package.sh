#!/usr/bin/env bash
# Verify that the exact installed payload works in both OBS 32 and 33 layouts.
# Uses isolated libobs/OpenGL; never writes into the user's OBS installation.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="${HYPERSHADER_BUILD:-$repo/build_qt_reload}"
work="$(mktemp -d /tmp/hs-native-audio-package-XXXXXX)"
trap 'rm -rf "$work"' EXIT
cmake --install "$build" --prefix "$work/stage" > "$work/install.log"
stage="$work/stage"
test -s "$stage/hyperShaderFX.so"
test -s "$stage/bin/64bit/hyperShaderFX.so"
cmp "$stage/hyperShaderFX.so" "$stage/bin/64bit/hyperShaderFX.so"
if test -f "$stage/bin/64bit/libonnxruntime.so.1.17.1"; then
    readelf -d "$stage/hyperShaderFX.so" | grep -F '$ORIGIN/bin/64bit'
    ldd "$stage/hyperShaderFX.so" | grep -F "=> $stage/bin/64bit/libonnxruntime.so.1.17.1"
fi
for path in "$stage/hyperShaderFX.so" "$stage/bin/64bit/hyperShaderFX.so"; do
    HYPERSHADER_MODULE="$path" HYPERSHADER_DATA="$stage/data" \
        bash "$repo/scripts/verify-original-instruments-plugin.sh"
done
echo NATIVE_AUDIO_PACKAGED_OBS_LAYOUTS_PASS=1
