#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

fail() {
  printf '%s\n' "[portability-check] $1" >&2
  exit 1
}

require_match() {
  pattern="$1"
  file="$2"
  message="$3"
  if ! grep -Eq "$pattern" "$file"; then
    fail "$message ($file)"
  fi
}

reject_match() {
  pattern="$1"
  file="$2"
  message="$3"
  if match=$(grep -nE "$pattern" "$file"); then
    fail "$message ($file)\nFirst failure found at:\n$match"
  fi
}

require_match "C_STANDARD[[:space:]]+11" "$ROOT_DIR/CMakeLists.txt" "Missing C11 standard in CMake"
require_match "\\/std:c11" "$ROOT_DIR/CMakeLists.txt" "Missing MSVC C11 flag in CMake"
require_match "#ifndef M_PI" "$ROOT_DIR/vendor/kissfft/kiss_fftr.c" "Missing M_PI fallback in kiss_fftr.c"

reject_match "[[:space:]]->d_type" "$ROOT_DIR/src/hyper_ui.c" "Do not use dirent.d_type (not portable across filesystems/platforms)"
reject_match "#include[[:space:]]+<dirent\.h>" "$ROOT_DIR/src/filter-hypershader.c" "filter-hypershader.c must not include dirent.h"

printf '%s\n' "[portability-check] OK"
