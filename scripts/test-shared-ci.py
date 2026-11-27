"""Shared CI packaging regression tests against real project assets."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import shutil

spec = importlib.util.spec_from_file_location("driver", Path(__file__).resolve().parents[1] / ".github/scripts/ci-driver.py")
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)

class PackageTests(unittest.TestCase):
    def test_layout_and_payload(self):
        for version in ("32.2.0", "33.0.3"):
            with self.subTest(version=version), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                stage = root / "stage"
                (stage / "bin/64bit").mkdir(parents=True)
                (stage / "bin/64bit/hypershader.so").write_bytes(b"module fixture")
                (stage / "hypershader.so").write_bytes(b"module fixture")
                data = stage / "data"
                shutil.copytree(driver.ROOT / "data", data, ignore=shutil.ignore_patterns("AGENTS.md"))
                package = root / "package"
                driver.prepare_payload(stage, package, "linux-x86_64", version)
                prefix = package / "hypershader"
                module = prefix / ("bin/64bit/hypershader.so" if version == "32.2.0" else "hypershader.so")
                self.assertEqual(module.read_bytes(), b"module fixture")
                self.assertEqual((prefix / "LICENSE").read_bytes(), (driver.ROOT / "LICENSE").read_bytes())
                self.assertTrue((prefix / "data/shaders/root/template.effect").is_file())
                (data / "unexpected").write_text("unexpected")
                with self.assertRaises(RuntimeError):
                    driver.prepare_payload(stage, root / "bad", "linux-x86_64", version)

    def test_windows_debug_symbols_excluded(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage = root / "stage/hypershader"
            (stage / "bin/64bit").mkdir(parents=True)
            (stage / "hypershader.dll").write_bytes(b"module fixture")
            (stage / "bin/64bit/hypershader.dll").write_bytes(b"module fixture")
            (stage / "bin/64bit/hypershader.pdb").write_bytes(b"symbols")
            shutil.copytree(driver.ROOT / "data", stage / "data", ignore=shutil.ignore_patterns("AGENTS.md"))
            package = root / "package"
            driver.prepare_payload(root / "stage", package, "windows-x64", "33.0.3")
            self.assertEqual((package / "hypershader/hypershader.dll").read_bytes(), b"module fixture")
            self.assertFalse(list(package.rglob("*.pdb")))
            self.assertFalse((package / "hypershader/bin").exists())

if __name__ == "__main__":
    unittest.main()
