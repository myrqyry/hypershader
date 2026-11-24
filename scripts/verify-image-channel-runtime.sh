#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d /tmp/hyper-image-runtime-XXXXXX)"
trap 'rm -rf "$work"' EXIT

python3 - "$work/channel.png" <<'PY'
import struct
import sys
import zlib
from pathlib import Path

def chunk(name, payload):
    return struct.pack(">I", len(payload)) + name + payload + struct.pack(">I", zlib.crc32(name + payload))

rows = [
    bytes([0, 255, 0, 0, 255, 0, 255, 0, 255]),
    bytes([0, 0, 0, 255, 255, 255, 255, 255, 255]),
]
png = (
    b"\x89PNG\r\n\x1a\n"
    + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 6, 0, 0, 0))
    + chunk(b"IDAT", zlib.compress(b"".join(rows)))
    + chunk(b"IEND", b"")
)
Path(sys.argv[1]).write_bytes(png)
PY

printf %s R0lGODlhAgACAIEAAP8AAAAAAAAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQACgAAACwAAAAAAgACAAAIBgABCAQQEAAh+QQBCgABACwAAAAAAgACAIEAgAAAAAAAAAAAAAAIBgABCAQQEAAh+QQBCgABACwAAAAAAgACAIEAAP8AAAAAAAAAAAAIBgABCAQQEAA7 | base64 -d > "$work/animated.gif"

printf %s R0lGODlhAgACAIEAAP8AAAAAAAAAAAAAACH5BAAKAAAALAAAAAACAAIAAAgGAAEIBBAQACH5BAEKAAEALAAAAAACAAIAgQCAAAAAAAAAAAAAAAgGAAEIBBAQACH5BAEKAAEALAAAAAACAAIAgQAA/wAAAAAAAAAAAAgGAAEIBBAQADs= | base64 -d > "$work/oneshot.gif"

obs32_include="$(pkg-config --variable=includedir libobs)/obs"
gcc -std=c17 -O1 -I"$repo/src" -I"$obs32_include" \
    -c "$repo/src/channel_image_compat.c" -o "$work/compat.o"
gcc -std=c17 -O1 -I"$repo/src" -I"$obs32_include" \
    "$repo/scripts/verify-image-channel-runtime.c" "$work/compat.o" \
    $(pkg-config --libs libobs) -ldl -o "$work/image-runtime"

echo "OBS32 host runtime:"
(cd "$work" && xvfb-run -a "$work/image-runtime" "$work/channel.png" "$work/animated.gif" "$work/oneshot.gif")

flatpak_location="$(flatpak info --show-location com.obsproject.Studio)"
test -r "$flatpak_location/files/include/obs/obs-config.h"
grep -q '#define LIBOBS_API_MAJOR_VER 33' "$flatpak_location/files/include/obs/obs-config.h"

echo "OBS33 Flatpak runtime using the same OBS32-built binary:"
(cd "$work" && xvfb-run -a flatpak run \
    --filesystem="$work" \
    --command="$work/image-runtime" \
    com.obsproject.Studio "$work/channel.png" "$work/animated.gif" "$work/oneshot.gif")
