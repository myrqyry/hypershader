#!/usr/bin/env python3
"""Exercise the candidate archive boundary using real Git repositories."""
import pathlib
import subprocess
import tarfile
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).resolve().with_name("stage-release-source.py")


class ReleaseSourceTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.repo = self.root / "repo"
        self.repo.mkdir()
        self.git("init", "-q")
        self.git("config", "user.email", "test@example.invalid")
        self.git("config", "user.name", "Test")
        self.write("data/shaders/old.effect", "old history")
        self.commit()
        self.git("rm", "data/shaders/old.effect")
        self.write("data/shaders/current.effect", "candidate")
        self.write("data/models/unreviewed.onnx", "model")
        self.write("src/main.c", "source")
        self.write(".gitignore", "data/shaders/local.effect\n")
        self.commit()
        entries = []
        for decision, path in [
            ("include", "data/shaders/current.effect"),
            ("exclude", "data/models/unreviewed.onnx"),
        ]:
            entries.append(f"{decision} {self.git('rev-parse', 'HEAD:'+path).strip()} {path}")
        self.write("scripts/release-assets.txt", "\n".join(entries)+"\n")
        self.commit()
        self.write("data/shaders/local.effect", "ignored")
        self.write("data/shaders/current.effect", "uncommitted edit")
        self.output = self.root / "candidate.tar"

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.repo), *args], text=True)

    def write(self, path, content):
        p = self.repo / path
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(content)

    def commit(self):
        self.git("add", "-A")
        self.git("commit", "-qm", "fixture")

    def stage(self):
        return subprocess.run(["python3", str(SCRIPT), "--repo", str(self.repo),
                               "--output", str(self.output)], capture_output=True, text=True)

    def test_exports_only_pinned_committed_assets_and_no_history(self):
        result = self.stage()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with tarfile.open(self.output) as archive:
            names = archive.getnames()
            self.assertIn("src/main.c", names)
            self.assertIn("data/shaders/current.effect", names)
            self.assertEqual(archive.extractfile("data/shaders/current.effect").read(), b"candidate")
            for path in ("data/shaders/local.effect", "data/shaders/old.effect",
                         "data/models/unreviewed.onnx", ".git"):
                self.assertNotIn(path, names)

    def test_new_tracked_asset_fails_closed(self):
        self.write("data/shaders/current.effect", "candidate")
        self.write("data/shaders/new.effect", "unclassified")
        self.commit()
        result = self.stage()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unclassified", result.stderr)
        self.assertFalse(self.output.exists())

    def test_changed_tracked_asset_fails_closed(self):
        self.commit()
        result = self.stage()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("changed", result.stderr)
        self.assertFalse(self.output.exists())

    def test_existing_output_is_not_overwritten(self):
        self.output.write_bytes(b"keep")
        self.assertNotEqual(self.stage().returncode, 0)
        self.assertEqual(self.output.read_bytes(), b"keep")


if __name__ == "__main__":
    unittest.main()
