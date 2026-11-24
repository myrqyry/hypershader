#!/usr/bin/env bash
# Isolated real libobs shader compilation; does not load or modify the user's OBS.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
scratch="$(mktemp -d /tmp/hyper-builder-obs-XXXXXXXX)"
trap 'rm -rf "$scratch"' EXIT
mkdir -p "$scratch/data/shaders"
ln -s "$root/data/stdlib" "$scratch/data/stdlib"
HYPER_BUILDER_EFFECT_PATH="$scratch/data/shaders/generated.effect" node "$root/scripts/verify-shader-builder.js"
HYPER_BUILDER_EFFECT_PATH="$scratch/data/shaders/generated.effect" node "$root/scripts/verify-colormaps.js"
HYPER_CURATED_EFFECT_DIR="$scratch/data/shaders" node "$root/scripts/verify-curated-builder.js"
g++ -std=c++17 -O2 -I"$(pkg-config --variable=includedir libobs)/obs" \
    "$root/scripts/verify-shader-builder-obs.cpp" -o "$scratch/verify" \
    $(pkg-config --libs libobs)
xvfb-run -a -s '-screen 0 1280x720x24' timeout 25 "$scratch/verify" "$scratch/data/shaders/generated-empty.effect"
xvfb-run -a -s '-screen 0 1280x720x24' timeout 25 "$scratch/verify" "$scratch/data/shaders/generated.effect"
xvfb-run -a -s '-screen 0 1280x720x24' timeout 25 "$scratch/verify" "$scratch/data/shaders/generated-swirl.effect"
for colormap in jet spectral lava_waves space; do
    xvfb-run -a -s '-screen 0 1280x720x24' timeout 25 "$scratch/verify" "$scratch/data/shaders/generated-colormap_${colormap}.effect"
done
for variant in 0 1 2 3 4 5; do
    xvfb-run -a -s '-screen 0 1280x720x24' timeout 25 "$scratch/verify" "$scratch/data/shaders/curated-${variant}.effect"
done
g++ -std=c++17 -O2 -I"$(pkg-config --variable=includedir libobs)/obs" \
    "$root/scripts/verify-colormaps-obs.cpp" -o "$scratch/verify-colormap-pixels" \
    $(pkg-config --libs libobs)
xvfb-run -a -s '-screen 0 1280x720x24' timeout 25 "$scratch/verify-colormap-pixels" \
    "$scratch/data/shaders/generated-colormap_jet.effect" \
    "$scratch/data/shaders/generated-colormap_spectral.effect" \
    "$scratch/data/shaders/generated-colormap_lava_waves.effect" \
    "$scratch/data/shaders/generated-colormap_space.effect"
