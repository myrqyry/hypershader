#!/usr/bin/env python3
"""Interoperability tests for portable ShaderLab slDp embedded-depth PNGs."""
import importlib.util
import errno
import math
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "depth_png.py"
PNG = b"\x89PNG\r\n\x1a\n"
HEADER = struct.Struct("<IHHIIIffIIQ64s")


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(
        ">I", zlib.crc32(kind + data) & 0xffffffff
    )


def image(w=2, h=2):
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    # Use a plain packing expression: this test image should be externally
    # decodable as ordinary RGBA PNG, even without any slDp depth.
    rows = b"".join(
        b"\x00" + b"".join(bytes((x * 70, y * 60, 100, 255)) for x in range(w))
        for y in range(h)
    )
    return PNG + chunk(b"IHDR", ihdr) + chunk(b"tEXt", b"Author\x00Test") + \
        chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")


def reference_sl_dp_png():
    # Independent little-endian fixture matching ShaderLab's packed
    # DepthMapHeader and horizontal 16-bit delta encoding.
    depth = (0, 16384, 32768, 65535)
    delta = (0, 16384, 32768, 32767)
    raw = struct.pack("<4H", *delta)
    compressed = zlib.compress(raw)
    header = HEADER.pack(0x534c4431, 1, 1, 2, 2, 1,
                         1., 1000., len(raw), len(compressed),
                         1720000000000, b"Fixture" + bytes(57))
    base = image()
    return base[:-12] + chunk(b"slDp", header + compressed) + base[-12:], depth


class DepthPNGTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("depth_png", TOOL)
        cls.module = importlib.util.module_from_spec(spec)
        sys.modules["depth_png"] = cls.module
        spec.loader.exec_module(cls.module)

    def test_all_encoding_roundtrips(self):
        src = image()
        depth = [0., 0.25, 0.5, 1.]
        tolerances = {"r32": 1e-7, "r16": 1/65535 + 1e-7,
                      "r8": 1/255 + 1e-7, "r4": 1/15 + 1e-7}
        for encoding, tol in tolerances.items():
            with self.subTest(encoding=encoding):
                png = self.module.embed(src, depth, 2, 2, encoding=encoding)
                metadata, decoded = self.module.extract(png)
                self.assertEqual(metadata["width"], 2)
                self.assertEqual(metadata["height"], 2)
                self.assertEqual(metadata["encoding"], encoding)
                for a, b in zip(decoded, depth):
                    self.assertLessEqual(abs(a-b), tol)

    def test_flat_depth_is_valid_when_explicitly_supplied(self):
        with self.subTest('constant'):
            packed = self.module.embed(image(), [0.5] * 4, 2, 2)
            self.assertTrue(self.module.extract(packed)[0]['depth_valid'])
        with self.subTest('single_pixel'):
            packed = self.module.embed(image(1, 1), [0.4], 1, 1)
            self.assertTrue(self.module.extract(packed)[0]['depth_valid'])

    def test_embed_does_not_generate_invalid_near_far_bounds(self):
        with self.assertRaisesRegex(ValueError, 'far plane'):
            self.module.embed(image(), [0., .25, .5, 1.], 2, 2,
                              far_plane=0.5)

    def test_rejects_invalid_depth_near_planes(self):
        fixture, _ = reference_sl_dp_png()
        typ, payload = next((t, d) for t, d in self.module.chunks(fixture)
                            if t == b'slDp')
        for near in (0., -1., math.inf, math.nan, 1001.):
            with self.subTest(near=near):
                modified = bytearray(payload)
                modified[20:24] = struct.pack('<f', near)
                bad = image()[:-12] + chunk(typ, bytes(modified)) + image()[-12:]
                with self.assertRaisesRegex(ValueError, 'near plane'):
                    self.module.extract(bad)

    def test_embedded_output_respects_size_cap(self):
        source = image()
        full = self.module.embed(source, [0., .25, .5, 1.], 2, 2)
        self.assertGreater(len(full), len(source))
        old_limit = self.module.MAX_PNG_BYTES
        try:
            self.module.MAX_PNG_BYTES = len(full) - 1
            with self.assertRaisesRegex(ValueError, 'size limit'):
                self.module.embed(source, [0., .25, .5, 1.], 2, 2)
        finally:
            self.module.MAX_PNG_BYTES = old_limit

    def test_metadata_inspection_does_not_expand_depth_samples(self):
        with tempfile.TemporaryDirectory() as directory:
            source = self.module.embed(image(), [0., .25, .5, 1.], 2, 2)
            output = Path(directory) / 'source.png'
            output.write_bytes(source)
            original = self.module._unpack_depth
            try:
                def refuse_expansion(*_args):
                    raise AssertionError('inspect must not expand depth samples')
                self.module._unpack_depth = refuse_expansion
                # The real CLI entry point must succeed without expansion.
                self.assertEqual(self.module.main(['inspect', str(output)]), 0)
            finally:
                self.module._unpack_depth = original

    def test_golden_pngs_written_by_original_shaderlab_cpp(self):
        # Generated with upstream commit 00aebdf's unmodified
        # depth_chunk.cpp using its own bundled miniz implementation.
        for encoding in ("r32", "r16", "r8", "r4"):
            with self.subTest(encoding=encoding):
                png = (ROOT / "scripts" / "fixtures" /
                       ("shaderlab-" + encoding + ".png")).read_bytes()
                metadata, floats = self.module.extract(png)
                self.assertEqual(metadata["encoding"], encoding)
                for observed, expected in zip(floats, (0., .25, .5, 1.)):
                    self.assertLessEqual(abs(observed - expected),
                                         1 / 15 + 1e-6)

    def test_reads_upstream_shaderslab_r16_fixture(self):
        png, quantized = reference_sl_dp_png()
        metadata, floats = self.module.extract(png)
        self.assertEqual(metadata["encoding"], "r16")
        self.assertEqual(metadata["far_plane"], 1000.0)
        self.assertEqual(metadata["timestamp"], 1720000000000)
        self.assertEqual(metadata["game"], "Fixture")
        for value, raw in zip(floats, quantized):
            self.assertAlmostEqual(value, raw/65535., places=6)

    def test_original_image_chunks_preserved_on_reembed(self):
        original = image()
        a = self.module.embed(original, [0., .25, .5, 1.], 2, 2)
        b = self.module.embed(a, [1., .5, .25, 0.], 2, 2)
        self.assertEqual(b.count(b"slDp"), 1)
        self.assertEqual(
            [(t, d) for t, d in self.module.chunks(b) if t != b"slDp"],
            list(self.module.chunks(original)),
        )
        self.assertAlmostEqual(self.module.extract(b)[1][0], 1.)

    def test_png_chunk_crc_and_structure_validation(self):
        original = image()
        broken_crc = bytearray(original)
        broken_crc[32] ^= 0x01
        for bad in (b"not a png", bytes(broken_crc), original[:-3],
                    original + b"illegal trailer"):
            with self.subTest(data=bad[:12]):
                with self.assertRaises(ValueError):
                    self.module.extract(bad)
        with self.assertRaises(ValueError):
            self.module.embed(broken_crc, [0.] * 4, 2, 2)

    def test_rejects_duplicate_depth_chunks(self):
        png, _ = reference_sl_dp_png()
        typ, data = next((t, d) for t, d in self.module.chunks(png)
                         if t == b"slDp")
        duplicate = png[:-12] + chunk(typ, data) + png[-12:]
        with self.assertRaisesRegex(ValueError, "multiple|duplicate"):
            self.module.extract(duplicate)

    def test_rejects_mismatched_dimensions_and_malformed_depth_payload(self):
        original = image()
        with self.assertRaises(ValueError):
            self.module.embed(original, [0.] * 4, 1, 4)
        with self.assertRaises(ValueError):
            self.module.embed(original, [0.] * 3, 2, 2)
        fixture, _ = reference_sl_dp_png()
        typ, payload = next((t, d) for t, d in self.module.chunks(fixture)
                            if t == b"slDp")
        corrupt = bytearray(payload)
        # packed header byte offset for declared uncompressed byte count
        corrupt[28:32] = struct.pack("<I", 99)
        png = image()[:-12] + chunk(typ, bytes(corrupt)) + image()[-12:]
        with self.assertRaises(ValueError):
            self.module.extract(png)
        huge = bytearray(payload)
        huge[8:12] = struct.pack("<I", 9999999)
        png = image()[:-12] + chunk(typ, bytes(huge)) + image()[-12:]
        with self.assertRaises(ValueError):
            self.module.extract(png)

    def test_truncated_and_overflowed_compressed_stream_rejected(self):
        fixture, _ = reference_sl_dp_png()
        typ, payload = next((t, d) for t, d in self.module.chunks(fixture)
                            if t == b"slDp")
        for compressed in (zlib.compress(bytes(1000)), b"\x78\x9c"):
            head = bytearray(payload[:HEADER.size])
            head[32:36] = struct.pack("<I", len(compressed))
            png = image()[:-12] + chunk(typ, bytes(head) + compressed) + image()[-12:]
            with self.subTest(compressed=compressed[:3]):
                with self.assertRaises(ValueError):
                    self.module.extract(png)

    def test_existing_broken_symlink_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            destination = root / "output.png"
            destination.symlink_to(root / "does-not-exist")
            with self.assertRaisesRegex(ValueError, "exists"):
                self.module._write_new(destination, image())
            self.assertTrue(destination.is_symlink())
            self.assertFalse((root / "does-not-exist").exists())

    def test_odd_pixel_count_in_r4(self):
        png = image(1, 3)
        values = [0.0, 0.5, 1.0]
        encoded = self.module.embed(png, values, 1, 3, encoding="r4")
        _, unpacked = self.module.extract(encoded)
        self.assertEqual(len(unpacked), 3)
        self.assertAlmostEqual(unpacked[-1], 1.0)

    def test_outputs_remain_noclobber_when_hard_links_are_unsupported(self):
        with tempfile.TemporaryDirectory() as directory:
            dest = Path(directory) / 'output.png'
            original_link = self.module.os.link
            try:
                def unsupported_link(*_args):
                    raise OSError(errno.EOPNOTSUPP, 'Filesystem does not support hard links')
                self.module.os.link = unsupported_link
                self.module._write_new(dest, image())
                self.assertEqual(dest.read_bytes(), image())
                with self.assertRaisesRegex(ValueError, 'exists'):
                    self.module._write_new(dest, b'not allowed to overwrite')
                self.assertEqual(dest.read_bytes(), image())
                self.assertEqual(list(Path(directory).glob('.depth-png-*')), [])
            finally:
                self.module.os.link = original_link

    def test_cli_embed_extract_and_nonoverwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root/"source.png"
            raw = root/"depth.f32"
            packaged = root/"with-depth.png"
            unpacked = root/"roundtrip.f32"
            source.write_bytes(image())
            raw.write_bytes(struct.pack("<4f", 0., .25, .5, 1.))
            self.assertEqual(subprocess.run(
                [sys.executable, str(TOOL), "embed", str(source), str(raw),
                 str(packaged), "--encoding", "r16"],
                capture_output=True, text=True).returncode, 0)
            self.assertTrue(packaged.is_file())
            self.assertEqual(subprocess.run(
                [sys.executable, str(TOOL), "extract", str(packaged), str(unpacked)],
                capture_output=True, text=True).returncode, 0)
            floats = struct.unpack("<4f", unpacked.read_bytes())
            self.assertLess(abs(floats[1]-.25), 1e-4)
            duplicate = subprocess.run(
                [sys.executable, str(TOOL), "extract", str(packaged), str(unpacked)],
                capture_output=True, text=True)
            self.assertNotEqual(duplicate.returncode, 0)
            self.assertIn("exists", duplicate.stderr.lower())

if __name__ == "__main__":
    unittest.main()
