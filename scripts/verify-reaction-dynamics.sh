#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d /tmp/hyper-reaction-unit-XXXXXX)"
trap 'rm -rf "$work"' EXIT
cc -std=c17 -Wall -Wextra -Werror -I"$repo/src" "$repo/scripts/verify-reaction-dynamics.c" -lm -o "$work/test"
"$work/test"
