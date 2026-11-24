#!/usr/bin/env python3
"""Migration preserves source, comments, literal strings and custom identifiers."""
import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).with_name("migrate-shader-vocabulary.py")
spec = importlib.util.spec_from_file_location("migration", SCRIPT)
migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(migration)

class MigrationTests(unittest.TestCase):
    def test_builtin_names_only(self):
        original = '''// iTime remains in commentary
#include "iTime.glsl"
uniform float iTime;
uniform float3 iResolution;
float iTimeScale;
void mainImage(out float4 color, in float2 pixel) {
    color = float4(iTime, iResolution.x, iTimeScale, 1);
}
HYPERSHADER_MAINIMAGE_WRAPPER
/* iBufferA */
'''
        converted = migration.convert(original)
        self.assertIn("uniform float hs_time;", converted)
        self.assertIn("uniform float3 hs_resolution;", converted)
        self.assertIn("void hs_shade_pixel(", converted)
        self.assertIn("HYPERSHADER_PIXEL_WRAPPER", converted)
        self.assertIn("float iTimeScale;", converted)
        self.assertIn("// iTime remains in commentary", converted)
        self.assertIn('#include "iTime.glsl"', converted)
        self.assertIn("/* iBufferA */", converted)
        self.assertEqual(migration.convert(converted), converted)

    def test_mixed_names_rejected(self):
        with self.assertRaises(ValueError):
            migration.convert("uniform float iTime; uniform float hs_time;")

    def test_cli_never_overwrites(self):
        with tempfile.TemporaryDirectory() as temp:
            src = Path(temp) / "old.effect"
            dst = Path(temp) / "new.effect"
            src.write_text("uniform float iTime;")
            subprocess.run([sys.executable, str(SCRIPT), str(src), str(dst)], check=True, capture_output=True)
            self.assertEqual(src.read_text(), "uniform float iTime;")
            self.assertEqual(dst.read_text(), "uniform float hs_time;")
            result = subprocess.run([sys.executable, str(SCRIPT), str(src), str(dst)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(dst.read_text(), "uniform float hs_time;")

if __name__ == "__main__":
    unittest.main()
