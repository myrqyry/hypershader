"""Real material atlas pixel and integrity checks."""
import importlib.util
import shutil
import tempfile
import unittest
from pathlib import Path
from PIL import Image

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location("atlas",HERE/"pack-hypbr-atlas.py")
atlas=importlib.util.module_from_spec(spec)
spec.loader.exec_module(atlas)
BUNDLE=HERE.parent/"data/materials/classic_red_brick"

class AtlasTests(unittest.TestCase):
    def test_actual_map_interiors_and_periodic_gutters(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/"atlas.png"
            atlas.pack(BUNDLE,output)
            with Image.open(output) as packed:
                self.assertEqual(packed.size,(1542,514))
                for i,name in enumerate(atlas.MAPS):
                    with Image.open(BUNDLE/name) as source:
                        m=source.convert("RGB")
                    x=i*514
                    self.assertEqual(packed.crop((x+1,1,x+513,513)).tobytes(),m.tobytes())
                    self.assertEqual(packed.crop((x,1,x+1,513)).tobytes(),m.crop((511,0,512,512)).tobytes())
                    self.assertEqual(packed.crop((x+513,1,x+514,513)).tobytes(),m.crop((0,0,1,512)).tobytes())
                    self.assertEqual(packed.crop((x+1,0,x+513,1)).tobytes(),m.crop((0,511,512,512)).tobytes())
                    self.assertEqual(packed.crop((x+1,513,x+513,514)).tobytes(),m.crop((0,0,512,1)).tobytes())
                    self.assertEqual(packed.getpixel((x,0)),m.getpixel((511,511)))
            self.assertEqual(output.read_bytes(),(BUNDLE/"atlas.png").read_bytes())
            with self.assertRaisesRegex(ValueError,"already exists"):
                atlas.pack(BUNDLE,output)
    def test_rejects_corrupt_map_before_output(self):
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/"bundle"
            shutil.copytree(BUNDLE,source)
            (source/"normal.png").write_bytes(b"corrupt")
            output=Path(directory)/"atlas.png"
            with self.assertRaisesRegex(ValueError,"hash mismatch"):
                atlas.pack(source,output)
            self.assertFalse(output.exists())

if __name__=="__main__":
    unittest.main()
