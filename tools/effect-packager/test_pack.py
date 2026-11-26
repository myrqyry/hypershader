import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

TOOL = Path(__file__).with_name("pack.py")


def run(*args):
    return subprocess.run([sys.executable, str(TOOL), *map(str, args)], capture_output=True, text=True)


class NativeEffectPackTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.root = self.base / "assets"
        (self.root / "root").mkdir(parents=True)
        (self.root / "stdlib").mkdir()
        (self.root / "textures").mkdir()
        (self.root / "root" / "demo.effect").write_text(
            '#include "../stdlib/helper.glsl"\n'
            'uniform texture2d tex; // test\n'
            'technique Draw { pass { pixel_shader = Draw; } }\n')
        (self.root / "stdlib" / "helper.glsl").write_text("float helper() { return 1.; }\n")
        (self.root / "textures" / "pattern.png").write_bytes(b"fake texture for pack tests")
        self.archive = self.base / "demo.hs"
        self.dest = self.base / "extracted"

    def pack(self):
        return run("pack", self.root / "root" / "demo.effect", self.archive,
                   "--root", self.root, "--asset", self.root / "textures" / "pattern.png")

    def test_roundtrip_and_manifest_integrity(self):
        result = self.pack()
        self.assertEqual(result.returncode, 0, result.stderr)
        with zipfile.ZipFile(self.archive) as z:
            names = sorted(z.namelist())
            self.assertEqual(names, [
                "manifest.json", "root/demo.effect", "stdlib/helper.glsl",
                "textures/pattern.png"])
            manifest = json.loads(z.read("manifest.json"))
            self.assertEqual(manifest["format"], "hypershader-native-effect")
            self.assertEqual(manifest["version"], 1)
            self.assertEqual(manifest["entry"], "root/demo.effect")
            for name, sha in manifest["files"].items():
                self.assertEqual(hashlib.sha256(z.read(name)).hexdigest(), sha)
        result = run("unpack", self.archive, self.dest)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            (self.dest / "stdlib/helper.glsl").read_text(),
            (self.root / "stdlib/helper.glsl").read_text())

    def test_deterministic_archive(self):
        self.assertEqual(self.pack().returncode, 0)
        other = self.base / "second.hs"
        self.assertEqual(run("pack", self.root / "root/demo.effect", other, "--root", self.root,
                             "--asset", self.root / "textures/pattern.png").returncode, 0)
        self.assertEqual(self.archive.read_bytes(), other.read_bytes())

    def test_refuse_external_includes(self):
        (self.root / "root/demo.effect").write_text('#include "../../outside.glsl"\n')
        self.assertNotEqual(self.pack().returncode, 0)
        self.assertFalse(self.archive.exists())

    def test_refuse_missing_include(self):
        (self.root / "root/demo.effect").write_text('#include "../stdlib/missing.glsl"\n')
        self.assertNotEqual(self.pack().returncode, 0)
        self.assertFalse(self.archive.exists())

    def test_refuse_tampered_entry_without_partial_import(self):
        self.assertEqual(self.pack().returncode, 0)
        altered = self.base / "tampered.hs"
        with zipfile.ZipFile(self.archive) as source, zipfile.ZipFile(altered, "w") as output:
            for item in source.infolist():
                output.writestr(item.filename, b"tampered" if item.filename == "root/demo.effect"
                                else source.read(item.filename))
        result = run("unpack", altered, self.dest)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.dest.exists())

    def test_refuse_archive_path_traversal(self):
        evil = self.base / "evil.hs"
        with zipfile.ZipFile(evil, "w") as z:
            z.writestr("manifest.json", json.dumps({"format": "hypershader-native-effect",
                "version": 1, "entry": "../evil.effect",
                "files": {"../evil.effect": hashlib.sha256(b"x").hexdigest()}}))
            z.writestr("../evil.effect", b"x")
        self.assertNotEqual(run("unpack", evil, self.dest).returncode, 0)
        self.assertFalse((self.base / "evil.effect").exists())

    def test_refuse_existing_target(self):
        self.assertEqual(self.pack().returncode, 0)
        self.dest.mkdir()
        marker = self.dest / "user-data.txt"
        marker.write_text("do not touch")
        self.assertNotEqual(run("unpack", self.archive, self.dest).returncode, 0)
        self.assertEqual(marker.read_text(), "do not touch")

    def test_refuse_symlink_asset(self):
        (self.root / "textures" / "leak.png").symlink_to("/etc/hosts")
        result = run("pack", self.root / "root/demo.effect", self.archive,
                     "--root", self.root, "--asset", self.root / "textures/leak.png")
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
