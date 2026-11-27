#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
module="$(realpath "${1:-$root/build_x86_64/hypershader.so}")"
data="$(realpath "${2:-$root/data}")"
"$root/scripts/verify-two-buffer-graph.sh" "$module" "$data" "$data/shaders/root/source.wave_chamber.effect" source-wave
"$root/scripts/verify-two-buffer-graph.sh" "$module" "$data" "$data/shaders/root/source.chemical_garden.effect" source-chemistry
