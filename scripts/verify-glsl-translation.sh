#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d /tmp/hs-translate.XXXXXX)"
trap 'rm -rf "$work"' EXIT
args=(-std=c17 -O2 -Wall -Wextra -Werror -I"$repo/src" -I"$(pkg-config --variable=includedir libobs)/obs")
if [[ $# -gt 0 ]]; then
    git -C "$repo" show "$1:src/filter-hypershader.c" | sed -n '/^static char \*replace_word(/,/^\/\* Boilerplate expansion/{ /^\/\* Boilerplate expansion/d; p; }' > "$work/baseline.inc"
    cc "${args[@]}" -DHS_TRANSLATOR_BASELINE -I"$work" "$repo/scripts/test-glsl-translation.c" -o "$work/old" $(pkg-config --libs libobs)
    if [[ ! -f "$repo/src/shader/glsl-translate.h" ]]; then
        "$work/old"
        exit
    fi
    "$work/old" bench | tee "$work/old-bench"
fi
cc "${args[@]}" -fsanitize=undefined "$repo/scripts/test-glsl-translation.c" -o "$work/test" $(pkg-config --libs libobs)
"$work/test"
cc "${args[@]}" "$repo/scripts/test-glsl-translation.c" -o "$work/fast" $(pkg-config --libs libobs)
"$work/fast" bench | tee "$work/new-bench"
if [[ -f "$work/old-bench" ]]; then
    diff <(grep '^TRANSLATE_HASH ' "$work/old-bench") <(grep '^TRANSLATE_HASH ' "$work/new-bench")
    echo 'TRANSLATE_BENCH_OUTPUT_IDENTICAL=1'
fi
