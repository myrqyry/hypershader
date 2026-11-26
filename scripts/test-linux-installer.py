#!/usr/bin/env python3
"""Isolated installer integration tests (no user OBS state)."""
import pathlib
import subprocess
import tempfile
import unittest

INSTALLER = pathlib.Path(__file__).resolve().parents[1] / "install.sh"


class InstallerTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.payload = self.root / "payload"
        self.destination = self.root / "destination"
        for rel, content in {
            "bin/64bit/hypershader.so": b"module",
            "bin/64bit/libsample.so": b"dependency",
            "data/shaders/root/example.effect": b"shader",
            "data/stdlib/example.glsl": b"include",
            "data/locale/en-US.ini": b"locale",
        }.items():
            p = self.payload / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(content)

    def install(self, source):
        return subprocess.run(
            ["bash", str(INSTALLER), str(source), str(self.destination)],
            cwd=self.root, capture_output=True, text=True,
        )

    def assert_payload(self):
        for p in self.payload.rglob("*"):
            if p.is_file():
                self.assertEqual((self.destination / p.relative_to(self.payload)).read_bytes(), p.read_bytes())
        self.assertEqual((self.destination / "hypershader.so").read_bytes(), b"module")

    def test_structured_payload_copies_nested_resources_and_both_modules(self):
        result = self.install(self.payload)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assert_payload()

    def test_cmake_build_uses_install_rules(self):
        source = self.root / "source"
        source.mkdir()
        (source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.16)\nproject(test NONE)\n'
            f'install(DIRECTORY "{self.payload}/" DESTINATION .)\n'
        )
        build = self.root / "build"
        subprocess.run(["cmake", "-S", str(source), "-B", str(build)], check=True, capture_output=True)
        result = self.install(build)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assert_payload()

    def test_missing_module_does_not_create_destination(self):
        (self.payload / "bin/64bit/hypershader.so").unlink()
        result = self.install(self.payload)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.destination.exists())

    def test_mismatched_modules_fail_before_install(self):
        (self.payload / "hypershader.so").write_bytes(b"stale")
        result = self.install(self.payload)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.destination.exists())


if __name__ == "__main__":
    unittest.main()
