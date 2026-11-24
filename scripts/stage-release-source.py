#!/usr/bin/env python3
"""Stage a history-free candidate from a committed, pinned data inventory."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile


def stage(repo: Path, ref: str, output: Path):
    def git(*args):
        return subprocess.check_output(["git", "--literal-pathspecs", "-C", str(repo), *args])

    commit = git("rev-parse", "--verify", "--end-of-options", ref + "^{commit}").decode().strip()
    manifest = {}
    for line in git("show", commit + ":scripts/release-assets.txt").decode().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        decision, oid, path = line.split(maxsplit=2)
        if decision not in ("include", "exclude") or not path.startswith("data/") or path in manifest:
            raise ValueError(f"Invalid or duplicate asset entry: {path}")
        manifest[path] = (decision, oid)

    paths = []
    actual = set()
    for entry in git("ls-tree", "-r", "-z", commit).split(b"\0"):
        if not entry:
            continue
        info, raw_path = entry.split(b"\t", 1)
        mode, kind, oid = info.decode().split()
        path = raw_path.decode()
        if mode not in ("100644", "100755") or kind != "blob":
            raise ValueError(f"Unsupported source entry: {path}")
        if path.startswith("data/"):
            actual.add(path)
            if path not in manifest:
                raise ValueError(f"unclassified asset: {path}")
            decision, expected = manifest[path]
            if oid != expected:
                raise ValueError(f"changed asset: {path}; review and update its manifest entry")
            if decision == "exclude":
                continue
        paths.append(path)

    if missing := manifest.keys() - actual:
        raise ValueError(f"Manifest entries absent from commit: {', '.join(sorted(missing))}")
    if output.exists() or output.is_symlink():
        raise ValueError(f"Output already exists: {output}")
    if not output.parent.is_dir():
        raise ValueError(f"Output directory does not exist: {output.parent}")

    # Git writes the selected commit, not the working tree or its history.
    # Check the resulting archive too (e.g. export-ignore attributes).
    with tempfile.TemporaryDirectory(dir=output.parent) as temp:
        archive = Path(temp) / "candidate.tar"
        with archive.open("wb") as stream:
            subprocess.run(["git", "--literal-pathspecs", "-C", str(repo),
                            "archive", "--format=tar", commit, "--", *paths],
                           stdout=stream, check=True)
        with tarfile.open(archive) as stream:
            members = stream.getmembers()
            if any(not (m.isfile() or m.isdir()) for m in members):
                raise ValueError("Unexpected nonregular archive member")
            files = [m.name for m in members if m.isfile()]
            if len(files) != len(set(files)) or set(files) != set(paths):
                raise ValueError("Archive contents differ from selected commit inventory")
            for path, (decision, oid) in manifest.items():
                if decision != "include":
                    continue
                blob = stream.extractfile(path).read()
                algorithm = "sha1" if len(oid) == 40 else "sha256"
                digest = hashlib.new(algorithm, f"blob {len(blob)}\0".encode() + blob).hexdigest()
                if digest != oid:
                    raise ValueError(f"Archived asset differs from pinned blob: {path}")
        # Same-directory hard link is atomic and will not overwrite an existing file.
        os.link(archive, output)
    included = sum(decision == "include" for decision, _ in manifest.values())
    print(f"Candidate {commit}: {included} included / {len(manifest)-included} excluded data files")
    print(f"Source archive: {output} (not a licensing clearance or release package)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--ref", default="HEAD")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        stage(args.repo.resolve(), args.ref, args.output.absolute())
    except (ValueError, OSError, subprocess.CalledProcessError, tarfile.TarError) as error:
        print(f"Cannot stage release source: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
