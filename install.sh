#!/usr/bin/env bash
# Install an existing Linux build or structured hypershader package.
set -euo pipefail

PLUGIN_NAME=hypershader
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
destination="${2:-${XDG_CONFIG_HOME:-$HOME/.config}/obs-studio/plugins/$PLUGIN_NAME}"

if (( $# > 2 )); then
    echo "Usage: $0 [build-or-package-directory] [destination]" >&2
    exit 2
fi

if (( $# )); then
    source_dir="$1"
elif [[ -f "$repo/bin/64bit/$PLUGIN_NAME.so" && -d "$repo/data" ]]; then
    source_dir="$repo"
else
    source_dir=""
    for candidate in build_x86_64 build-local build; do
        if [[ -f "$repo/$candidate/cmake_install.cmake" ]]; then
            source_dir="$repo/$candidate"
            break
        fi
    done
fi

if [[ -z "$source_dir" || ! -d "$source_dir" ]]; then
    echo "No build or package directory found. Build first or specify its path." >&2
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
if [[ -f "$source_dir/cmake_install.cmake" ]]; then
    # Install through CMake, including nested shaders, locale, and dependencies.
    # Stage before touching the destination so incomplete builds fail safely.
    payload="$work/stage"
    cmake --install "$source_dir" --prefix "$payload"
else
    payload="$source_dir"
fi

legacy="$payload/bin/64bit/$PLUGIN_NAME.so"
modern="$payload/$PLUGIN_NAME.so"
if [[ ! -s "$legacy" || ! -d "$payload/data/shaders/root" || ! -d "$payload/data/stdlib" || ! -d "$payload/data/locale" ]]; then
    echo "Incomplete hypershader package: module, shader library, stdlib and locale are required." >&2
    exit 1
fi
if [[ -e "$modern" ]] && ! cmp -s "$modern" "$legacy"; then
    echo "Package contains mismatched OBS 33 and legacy modules; refusing installation." >&2
    exit 1
fi
if [[ "$(realpath -m "$payload")" == "$(realpath -m "$destination")" ]]; then
    echo "Source and destination must differ." >&2
    exit 1
fi

# Caller must close OBS before replacing a loaded plugin.
mkdir -p "$destination"
cp -a "$payload/bin" "$payload/data" "$destination/"
# OBS 33 loads the root copy; older OBS versions use bin/64bit.
cp -L "$destination/bin/64bit/$PLUGIN_NAME.so" "$destination/$PLUGIN_NAME.so"
cmp "$destination/bin/64bit/$PLUGIN_NAME.so" "$destination/$PLUGIN_NAME.so"
echo "Installed hypershader into $destination"
