#!/usr/bin/env python3
"""Portable ShaderLab-compatible slDp depth-in-PNG interchange.

Upstream: https://github.com/NotRayST/ShaderLab/blob/main/common/depth_chunk.h
Original format copyright (c) 2026 Ray_st, MIT.
PNG: https://www.w3.org/TR/png-3/
Binary layouts intentionally match ShaderLab's packed little-endian C++ structs.
No native ReShade, D3D, plugin, or GPU dependencies.
"""
from __future__ import annotations

import argparse
import errno
import json
import math
import os
from pathlib import Path
import struct
import sys
import tempfile
import time
import zlib

SIGNATURE = b"\x89PNG\r\n\x1a\n"
HEADER = struct.Struct("<IHHIIIffIIQ64s")
MAGIC = 0x534C4431
VERSION = 1
MAX_PIXELS = 16_777_216
MAX_PNG_BYTES = 128 * 1024 * 1024
ENCODINGS = {"r32": 0, "r16": 1, "r8": 2, "r4": 3}
INVERSE_ENCODINGS = {code: name for name, code in ENCODINGS.items()}


def _chunk(kind: bytes, data: bytes) -> bytes:
    if len(kind) != 4 or len(data) >= 2**31:
        raise ValueError("Invalid PNG chunk")
    return (struct.pack(">I", len(data)) + kind + data +
            struct.pack(">I", zlib.crc32(kind + data)))


def chunks(png: bytes) -> list[tuple[bytes, bytes]]:
    """Validate PNG chunk framing/CRCs; this does not pixel-decode IDAT."""
    if len(png) < 45 or len(png) > MAX_PNG_BYTES or not png.startswith(SIGNATURE):
        raise ValueError("Invalid PNG signature or file size")
    result = []
    offset = len(SIGNATURE)
    seen_ihdr = False
    seen_idat = False
    while offset < len(png):
        if len(png) - offset < 12:
            raise ValueError("Truncated PNG chunk header")
        length = struct.unpack_from(">I", png, offset)[0]
        if length > 2**31 - 1 or length > len(png) - offset - 12:
            raise ValueError("Invalid or truncated PNG chunk size")
        kind = png[offset + 4:offset + 8]
        payload = png[offset + 8:offset + 8 + length]
        checksum = struct.unpack_from(">I", png, offset + 8 + length)[0]
        if zlib.crc32(kind + payload) != checksum:
            raise ValueError("PNG CRC mismatch in " + repr(kind))
        if not all(65 <= byte <= 90 or 97 <= byte <= 122 for byte in kind) or not (65 <= kind[2] <= 90):
            raise ValueError("Invalid PNG chunk name")
        if not seen_ihdr:
            if kind != b"IHDR" or len(payload) != 13:
                raise ValueError("Missing or invalid IHDR")
            w, h = struct.unpack_from(">II", payload)
            _check_dims(w, h)
            seen_ihdr = True
        elif kind == b"IHDR":
            raise ValueError("Duplicate IHDR")
        if kind[0] <= 90 and kind not in (b"IHDR", b"PLTE", b"IDAT", b"IEND"):
            raise ValueError("Cannot safely edit PNG with unknown critical chunk")
        if kind == b"IDAT":
            seen_idat = True
        result.append((kind, payload))
        offset += 12 + length
        if kind == b"IEND":
            if length or not seen_idat or offset != len(png):
                raise ValueError("Invalid PNG ending or trailing data")
            return result
    raise ValueError("PNG lacks IEND")


def _check_dims(width: int, height: int) -> int:
    if (width < 1 or height < 1 or
            width > 16384 or height > 16384 or width * height > MAX_PIXELS):
        raise ValueError("Unsupported or excessive depth dimensions")
    return width * height


def _png_size(parts: list[tuple[bytes, bytes]]) -> tuple[int, int]:
    return struct.unpack_from(">II", parts[0][1])


def _pack_depth(values: list[float], width: int, height: int, encoding: int) -> bytes:
    values = [1.0 if not math.isfinite(float(v)) else
              max(0.0, min(1.0, float(v))) for v in values]
    if encoding == 0:
        return struct.pack("<" + str(len(values)) + "f", *values)
    if encoding in (1, 2):
        levels = 65535 if encoding == 1 else 255
        codes = [int(value * levels + 0.5) for value in values]
        delta = []
        for row in range(height):
            previous = 0
            for code in codes[row * width:(row + 1) * width]:
                delta.append((code - previous) & levels)
                previous = code
        return struct.pack("<" + str(len(delta)) +
                           ("H" if encoding == 1 else "B"), *delta)
    if encoding == 3:
        packed = bytearray((len(values) + 1) // 2)
        for i, value in enumerate(values):
            nibble = int(value * 15 + 0.5)
            packed[i // 2] |= nibble << (4 if i % 2 == 0 else 0)
        return bytes(packed)
    raise ValueError("Unsupported depth encoding")


def _unpack_depth(raw: bytes, width: int, height: int, encoding: int) -> list[float]:
    count = width * height
    if encoding == 0:
        floats = struct.unpack("<" + str(count) + "f", raw)
        return [1.0 if not math.isfinite(v) else max(0., min(1., v)) for v in floats]
    if encoding in (1, 2):
        levels = 65535 if encoding == 1 else 255
        codes = struct.unpack("<" + str(count) + ("H" if encoding == 1 else "B"), raw)
        out = []
        for row in range(height):
            running = 0
            for delta in codes[row * width:(row + 1) * width]:
                running = (running + delta) & levels
                out.append(running / levels)
        return out
    if encoding == 3:
        return [((raw[i // 2] >> (4 if i % 2 == 0 else 0)) & 15) / 15.
                for i in range(count)]
    raise ValueError("Unsupported depth encoding")


def embed(png: bytes, depth: list[float], width: int, height: int,
          *, encoding: str = "r16", far_plane: float = 1000.,
          game: str = "hypershader", timestamp: int | None = None) -> bytes:
    """Add or replace a ShaderLab-compatible depth chunk without changing IDAT."""
    parts = chunks(png)
    count = _check_dims(width, height)
    if _png_size(parts) != (width, height) or len(depth) != count:
        raise ValueError("Depth dimensions/count must match PNG image")
    if encoding not in ENCODINGS:
        raise ValueError("Unknown encoding")
    # The ShaderLab-compatible header encodes a fixed 1.0 near plane.
    # Never emit a depth chunk that this same decoder must reject.
    if not math.isfinite(far_plane) or far_plane < 1.0:
        raise ValueError("Invalid far plane")
    if sum(kind == b"slDp" for kind, _ in parts) > 1:
        raise ValueError("Multiple depth chunks in source PNG")
    byte_name = game.encode("utf-8")
    if len(byte_name) >= 64:
        raise ValueError("Game name too long")
    depth = [1.0 if not math.isfinite(float(v)) else max(0., min(1., float(v))) for v in depth]
    raw = _pack_depth(depth, width, height, ENCODINGS[encoding])
    compressed = zlib.compress(raw)
    stamp = int(time.time() * 1000) if timestamp is None else timestamp
    if not 0 <= stamp < 2**64:
        raise ValueError("Invalid timestamp")
    # A supplied, correctly dimensioned depth map is valid even when flat.
    # The flag indicates presence of depth data, not depth variance.
    valid_flag = 1
    header = HEADER.pack(MAGIC, VERSION, ENCODINGS[encoding],
                         width, height, valid_flag, 1., far_plane,
                         len(raw), len(compressed), stamp,
                         byte_name.ljust(64, b"\x00"))
    result = [(kind, data) for kind, data in parts
              if kind not in (b"slDp", b"IEND")]
    result.append((b"slDp", header + compressed))
    result.append((b"IEND", b""))
    # Reject oversized outputs before allocating a second full PNG copy.
    total = len(SIGNATURE) + sum(12 + len(data) for _, data in result)
    if total > MAX_PNG_BYTES:
        raise ValueError("Embedded PNG exceeds size limit")
    return SIGNATURE + b"".join(_chunk(kind, data) for kind, data in result)


def extract(png: bytes, *, decode_samples: bool = True) -> tuple[dict, list[float] | None]:
    """Validate an slDp chunk; inspect can skip per-pixel object expansion."""
    parts = chunks(png)
    matches = [data for kind, data in parts if kind == b"slDp"]
    if not matches:
        raise ValueError("PNG has no slDp depth chunk")
    if len(matches) != 1:
        raise ValueError("Multiple / duplicate slDp depth chunks")
    payload = matches[0]
    if len(payload) < HEADER.size:
        raise ValueError("Incomplete slDp header")
    (magic, version, encoding, width, height, flags, near, far,
     raw_size, compressed_size, timestamp, name) = HEADER.unpack_from(payload)
    if magic not in (MAGIC, 0x50444C53) or version != VERSION:
        raise ValueError("Unsupported ShaderLab depth header")
    if encoding not in INVERSE_ENCODINGS:
        raise ValueError("Unsupported depth encoding")
    count = _check_dims(width, height)
    expected = ((count * 4) if encoding == 0 else
                (count * 2) if encoding == 1 else
                count if encoding == 2 else (count + 1) // 2)
    compressed = payload[HEADER.size:]
    if raw_size != expected or compressed_size != len(compressed):
        raise ValueError("Depth sizes do not match metadata")
    if not math.isfinite(far) or far <= 0:
        raise ValueError("Invalid depth far plane")
    if not math.isfinite(near) or near <= 0 or near > far:
        raise ValueError("Invalid depth near plane")
    # Enforce output limit before accepting untrusted zlib streams.
    decoder = zlib.decompressobj()
    try:
        raw = decoder.decompress(compressed, expected + 1)
    except zlib.error as exc:
        raise ValueError("Invalid compressed depth") from exc
    if (len(raw) != expected or not decoder.eof or decoder.unconsumed_tail
            or decoder.unused_data):
        raise ValueError("Truncated or excessive compressed depth")
    metadata = {"width": width, "height": height,
                "encoding": INVERSE_ENCODINGS[encoding],
                "depth_valid": bool(flags & 1), "near_plane": near,
                "far_plane": far, "timestamp": timestamp,
                "game": name.split(b"\x00", 1)[0].decode("utf-8", errors="replace")}
    return metadata, (_unpack_depth(raw, width, height, encoding)
                      if decode_samples else None)


def _read(path: Path, maximum: int = MAX_PNG_BYTES) -> bytes:
    if path.stat().st_size > maximum:
        raise ValueError("File exceeds size limit")
    return path.read_bytes()


def _write_new(path: Path, content: bytes) -> None:
    """Publish a complete output without overwriting an existing destination.

    Prefer an atomic hard link. When the filesystem prohibits hard links,
    use exclusive file creation as a portable no-clobber fallback. On that
    fallback only, readers may see an incomplete output during a crash.
    """
    if os.path.lexists(path):
        raise ValueError("Output already exists: " + str(path))
    if not path.parent.is_dir():
        raise ValueError("Output parent directory does not exist")
    fd, temporary = tempfile.mkstemp(prefix=".depth-png-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        # Linking a complete temporary file publishes it atomically with
        # no-replace semantics, including when the destination is a symlink.
        # Both names are in the same filesystem/directory.
        try:
            os.link(temporary, path)
        except FileExistsError as exc:
            raise ValueError("Output already exists: " + str(path)) from exc
        except OSError as exc:
            if exc.errno not in (errno.EOPNOTSUPP, errno.ENOSYS, errno.EPERM):
                raise
            if os.name == "nt":
                # Windows rename refuses an existing target, and can publish
                # the validated temp file in one operation.
                try:
                    os.rename(temporary, path)
                except FileExistsError as occupied:
                    raise ValueError("Output already exists: " + str(path)) from occupied
            else:
                # O_EXCL also refuses existing symbolic links, including
                # dangling ones. Never open without O_EXCL or use os.replace.
                try:
                    output_fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
                except FileExistsError as occupied:
                    raise ValueError("Output already exists: " + str(path)) from occupied
                try:
                    with os.fdopen(output_fd, "wb") as stream:
                        if stream.write(content) != len(content):
                            raise OSError("Incomplete output write")
                        stream.flush()
                        os.fsync(stream.fileno())
                except BaseException:
                    os.unlink(path)
                    raise
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="command", required=True)
    insert = actions.add_parser("embed", help="Embed little-endian float32 depth into PNG")
    insert.add_argument("input_png", type=Path)
    insert.add_argument("depth_f32", type=Path)
    insert.add_argument("output_png", type=Path)
    insert.add_argument("--encoding", choices=ENCODINGS, default="r16")
    insert.add_argument("--far-plane", type=float, default=1000.)
    take = actions.add_parser("extract", help="Extract float32 depth from PNG")
    take.add_argument("input_png", type=Path)
    take.add_argument("output_f32", type=Path)
    inspect = actions.add_parser("inspect", help="Validate and describe depth metadata")
    inspect.add_argument("input_png", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "embed":
            source = _read(args.input_png)
            width, height = _png_size(chunks(source))
            count = _check_dims(width, height)
            raw = _read(args.depth_f32, count * 4)
            if len(raw) != count * 4:
                raise ValueError("Float32 depth file size does not match image pixels")
            data = struct.unpack("<" + str(count) + "f", raw)
            result = embed(source, data, width, height, encoding=args.encoding,
                           far_plane=args.far_plane)
            _write_new(args.output_png, result)
            print(f"Embedded {count} depth pixels ({args.encoding}) into {args.output_png}")
        elif args.command == "extract":
            metadata, floats = extract(_read(args.input_png))
            result = struct.pack("<" + str(len(floats)) + "f", *floats)
            _write_new(args.output_f32, result)
            print(json.dumps(metadata, indent=2))
        else:
            # Validate the compressed payload and bounds without converting
            # up to 16 million samples to Python objects just for metadata.
            metadata, _ = extract(_read(args.input_png), decode_samples=False)
            print(json.dumps(metadata, indent=2))
    except (OSError, ValueError, struct.error, OverflowError) as exc:
        print(f"depth-png: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
