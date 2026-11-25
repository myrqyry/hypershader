#!/usr/bin/env bash
# Isolated native OBS 32 GPU/shader/Sight-procedure checks. No installation.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d /tmp/hypershader-sight-verify-XXXXXX)"
trap 'rm -rf "$work"' EXIT
inc="$(pkg-config --variable=includedir libobs)/obs"
libdir="$(pkg-config --variable=libdir libobs)"
if [[ -v CXX ]]; then cxx="$CXX"; else cxx=g++; fi
build_probe() {
    "$cxx" -std=c++17 -O2 -pthread -I"$repo/src" -I"$inc" "$1" -o "$2" \
        $(pkg-config --libs libobs) -Wl,-rpath,"$libdir"
}
"${CC:-cc}" -std=c17 -O2 -Wall -Wextra -Werror -I"$repo/src" \
    "$repo/scripts/verify-sight-subject-stats.c" -lm -o "$work/subject_stats"
"$work/subject_stats"
"${CC:-cc}" -std=c17 -O2 -Wall -Wextra -Werror -I"$repo/src" \
    "$repo/scripts/verify-sight-text-mask.c" -lm -o "$work/text_mask"
"$work/text_mask"
"${CC:-cc}" -std=c17 -O2 -Wall -Wextra -Werror -I"$repo/src" "$repo/scripts/verify-sight-flow.c" -lm -o "$work/flow"
"$work/flow"
"${CC:-cc}" -std=c17 -Wall -Wextra -Werror -I"$repo/src" "$repo/scripts/verify-sight-flow-step.c" -o "$work/flow_step"
"$work/flow_step"
"${CC:-cc}" -std=c17 -O2 -Wall -Wextra -Werror -I"$repo/src" "$repo/scripts/verify-sight-touch.c" -lm -o "$work/touch"
"$work/touch"
"${CC:-cc}" -std=c17 -O2 -Wall -Wextra -Werror -I"$repo/src" "$repo/scripts/verify-sight-semantic.c" -lm -o "$work/semantic"
"$work/semantic"
build_probe "$repo/scripts/verify-sight-semantic-obs.cpp" "$work/semantic_obs"
build_probe "$repo/scripts/verify-sight-materials-obs.cpp" "$work/materials_gpu"
build_probe "$repo/scripts/verify-sight-touch-gpu-obs.cpp" "$work/touch_gpu"
build_probe "$repo/scripts/verify-sight-touch-obs.cpp" "$work/touch_obs"
build_probe "$repo/scripts/verify-sight-gpu-obs.cpp" "$work/gpu"
build_probe "$repo/scripts/verify-sight-flow-echo-obs.cpp" "$work/echo_gpu"
build_probe "$repo/scripts/verify-sight-flow-sparks-obs.cpp" "$work/sparks_gpu"
build_probe "$repo/scripts/verify-sight-depth-glass-obs.cpp" "$work/glass_gpu"
build_probe "$repo/scripts/verify-sight-relight-obs.cpp" "$work/relight_gpu"
build_probe "$repo/scripts/verify-sight-shader-obs.cpp" "$work/shader"
build_probe "$repo/scripts/verify-sight-client-obs.cpp" "$work/client"
build_probe "$repo/scripts/verify-sight-simple-effects.cpp" "$work/simple"

xvfb-run -a -s '-screen 0 1280x720x24' timeout 25 "$work/touch_gpu" "$repo/data/shaders/root/filter.sight_hand_ripples.effect"
xvfb-run -a -s '-screen 0 1280x720x24' timeout 25 "$work/gpu"
xvfb-run -a -s '-screen 0 1280x720x24' timeout 25 "$work/materials_gpu" \
    "$repo/data/shaders/root/filter.sight_semantic_materials.effect" "$repo/data/stdlib/semantic_regions.glsl"
xvfb-run -a -s '-screen 0 1280x720x24' timeout 25 "$work/simple" "$repo/data/shaders/root"
xvfb-run -a -s '-screen 0 1280x720x24' timeout 25 "$work/echo_gpu" "$repo/data/shaders/root/filter.sight_flow_echo.effect"
xvfb-run -a -s '-screen 0 1280x720x24' timeout 25 "$work/sparks_gpu" "$repo/data/shaders/root/filter.sight_flow_sparks.effect"
xvfb-run -a -s '-screen 0 1280x720x24' timeout 25 "$work/glass_gpu" "$repo/data/shaders/root/filter.sight_depth_glass.effect"
xvfb-run -a -s '-screen 0 1280x720x24' timeout 25 "$work/relight_gpu" "$repo/data/shaders/root/filter.sight_relight.effect"
xvfb-run -a -s '-screen 0 1280x720x24' timeout 25 "$work/shader" \
    "$repo/data/shaders/root/filter.sight_hand_highlights.effect"
xvfb-run -a -s '-screen 0 1280x720x24' timeout 25 "$work/shader" \
    "$repo/data/shaders/tests/sight_snapshot_pipeline_probe.effect"

sight_repo="$repo/../live-latent-lab-sight-public"
if [[ -v SIGHT_MODULE ]]; then sight_module="$SIGHT_MODULE"; else
    sight_module="$sight_repo/build-public/live-latent-lab-sight.so"
fi
if [[ -v SIGHT_DATA ]]; then sight_data="$SIGHT_DATA"; else
    sight_data="$sight_repo/data"
fi
if [[ -f "$sight_module" && -d "$sight_data" ]]; then
    if [[ -n "${HYPERSHADER_MODULE:-}" ]]; then
        xvfb-run -a -s '-screen 0 1280x720x24' timeout 30 "$work/client" \
            "$sight_module" "$sight_data" "$HYPERSHADER_MODULE" "$repo/data"
    else
        xvfb-run -a -s '-screen 0 1280x720x24' timeout 25 "$work/client" \
            "$sight_module" "$sight_data"
    fi
else
    echo "SIGHT_CLIENT_CHECK_SKIPPED: pass SIGHT_MODULE and SIGHT_DATA to verify shared module"
fi
if [[ -n "${HYPERSHADER_MODULE:-}" && -n "${SIGHT_GESTURE_MODEL:-}" && -n "${SIGHT_GESTURE_TEST_IMAGE:-}" && -n "${SIGHT_MEDIAPIPE_LIB:-}" && -n "${SIGHT_IMAGE_SOURCE_MODULE:-}" && -n "${SIGHT_IMAGE_SOURCE_DATA:-}" ]]; then
    xvfb-run -a -s '-screen 0 1280x720x24' timeout 45 "$work/touch_obs" \
        "$sight_module" "$sight_data" "$HYPERSHADER_MODULE" "$repo/data" \
        "$SIGHT_IMAGE_SOURCE_MODULE" "$SIGHT_IMAGE_SOURCE_DATA" "$SIGHT_GESTURE_MODEL" \
        "$SIGHT_MEDIAPIPE_LIB" "$SIGHT_GESTURE_TEST_IMAGE" "$repo/data/shaders/root/filter.sight_hand_ripples.effect"
else
    echo "SIGHT_RIPPLE_REAL_CHECK_SKIPPED: supply module and gesture fixture variables"
fi
if [[ -n "${HYPERSHADER_MODULE:-}" && -n "${SIGHT_SEMANTIC_MODEL:-}" && -n "${SIGHT_SEMANTIC_TEST_IMAGE:-}" && -n "${SIGHT_IMAGE_SOURCE_MODULE:-}" && -n "${SIGHT_IMAGE_SOURCE_DATA:-}" ]]; then
    xvfb-run -a -s '-screen 0 1280x720x24' timeout 45 "$work/semantic_obs" \
        "$sight_module" "$sight_data" "$HYPERSHADER_MODULE" "$repo/data" \
        "$SIGHT_IMAGE_SOURCE_MODULE" "$SIGHT_IMAGE_SOURCE_DATA" "$SIGHT_SEMANTIC_MODEL" \
        "$SIGHT_SEMANTIC_TEST_IMAGE" "$repo/data/shaders/tests/sight_semantic_probe.effect"
    xvfb-run -a -s '-screen 0 1280x720x24' timeout 45 "$work/semantic_obs" \
        "$sight_module" "$sight_data" "$HYPERSHADER_MODULE" "$repo/data" \
        "$SIGHT_IMAGE_SOURCE_MODULE" "$SIGHT_IMAGE_SOURCE_DATA" "$SIGHT_SEMANTIC_MODEL" \
        "$SIGHT_SEMANTIC_TEST_IMAGE" "$repo/data/shaders/root/filter.sight_semantic_prism.effect"
    xvfb-run -a -s '-screen 0 1280x720x24' timeout 120 "$work/semantic_obs" \
        "$sight_module" "$sight_data" "$HYPERSHADER_MODULE" "$repo/data" \
        "$SIGHT_IMAGE_SOURCE_MODULE" "$SIGHT_IMAGE_SOURCE_DATA" "$SIGHT_SEMANTIC_MODEL" \
        "$SIGHT_SEMANTIC_TEST_IMAGE" "$repo/data/shaders/root/filter.sight_semantic_materials.effect"
else
    echo "SIGHT_SEMANTIC_REAL_CHECK_SKIPPED: supply modules and semantic fixture variables"
fi
echo "HYPERSHADER_SIGHT_INTEROP_STATIC_AND_GPU_PASS=1"
