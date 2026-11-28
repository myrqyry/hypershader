#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="$(realpath "$1")"
data="$(realpath "$2")"
work="$(mktemp -d /tmp/hs-teardown.XXXXXX)"
trap 'rm -rf "$work"' EXIT
g++ -std=c++17 -O2 -I"$(pkg-config --variable=includedir libobs)/obs" "$repo/scripts/verify-teardown-logging.cpp" -o "$work/probe" $(pkg-config --libs libobs)
(cd "$work" && HOME="$work" XDG_CONFIG_HOME="$work" xvfb-run -a timeout 15 "$work/probe" "$module" "$data") >"$work/log" 2>&1 || { tail -35 "$work/log"; exit 1; }
grep '^TEARDOWN_LOG ' "$work/log"
