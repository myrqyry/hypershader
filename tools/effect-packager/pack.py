#!/usr/bin/env python3
"""Build and extract portable, inspectable HyperShader native effect bundles.

Only copies existing native .effect and explicitly listed assets. No shader
translation, OBS installation, auto-execution, or arbitrary filesystem writes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import sys
import tempfile
import zipfile

FORMAT = "HyperShader-native-effect"
MAX_FILES = 128
MAX_FILE_SIZE = 16 * 1024 * 1024
MAX_TOTAL = 64 * 1024 * 1024
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"\r\n]+)"', re.MULTILINE)


def fail(message):
    raise ValueError(message)


def valid_name(name):
    if not isinstance(name, str) or not name or "\\" in name or "\0" in name:
        fail("invalid archive path")
    parts = PurePosixPath(name).parts
    if name.startswith("/") or not parts or any(p in (".", "..") for p in parts):
        fail("unsafe archive path")
    if str(PurePosixPath(name)) != name:
        fail("noncanonical archive path")
    return name


def sha256(blob):
    return hashlib.sha256(blob).hexdigest()


def walk_assets(root, entry, assets):
    root = root.resolve(strict=True)
    if not root.is_dir():
        fail("asset root must be a directory")
    pending = [entry, *assets]
    blobs = {}
    while pending:
        path = pending.pop()
        # Reject symlinks at every level, not only at the leaf.
        if path.is_symlink():
            fail("symlink input is not supported")
        absolute = path.resolve(strict=True)
        try:
            relative = absolute.relative_to(root).as_posix()
        except ValueError:
            fail("asset or include escapes the selected root")
        valid_name(relative)
        if relative in blobs:
            continue
        if len(blobs) >= MAX_FILES:
            fail("too many files")
        if not absolute.is_file():
            fail("asset is not a file")
        # Resolve of a parent symlink could point inside the root; explicitly
        # reject symlinks throughout the input path.
        current = path
        while current != root and current != current.parent:
            if current.is_symlink():
                fail("symlinked asset paths are not supported")
            current = current.parent
        if absolute.stat().st_size > MAX_FILE_SIZE:
            fail("asset too large")
        blob = absolute.read_bytes()
        if len(blob) > MAX_FILE_SIZE or sum(map(len, blobs.values())) + len(blob) > MAX_TOTAL:
            fail("package size limit exceeded")
        blobs[relative] = blob
        if absolute.suffix.lower() in (".effect", ".glsl", ".hlsl", ".inc"):
            try:
                shader = blob.decode("utf-8")
            except UnicodeDecodeError:
                fail("shader includes must be valid UTF-8")
            for include in INCLUDE.findall(shader):
                pending.append(absolute.parent / include)
    return blobs


def archive_info(name):
    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = (stat.S_IFREG | 0o644) << 16
    return info


def pack(effect, output, root, assets):
    if effect.suffix.lower() != ".effect":
        fail("entry shader must be a native .effect file")
    root = root.resolve(strict=True)
    entry = effect.resolve(strict=True).relative_to(root).as_posix()
    blobs = walk_assets(root, effect, assets)
    manifest = {
        "format": FORMAT,
        "version": 1,
        "entry": valid_name(entry),
        "files": {name: sha256(blobs[name]) for name in sorted(blobs)},
    }
    if output.exists():
        fail("output already exists; refusing to overwrite")
    output.parent.mkdir(parents=True, exist_ok=True)
    tmp = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, prefix=".hs-", suffix=".tmp",
                                         delete=False) as handle:
            tmp = Path(handle.name)
        with zipfile.ZipFile(tmp, "w") as archive:
            data = (json.dumps(manifest, sort_keys=True, indent=2) + "\n").encode()
            archive.writestr(archive_info("manifest.json"), data)
            for name in sorted(blobs):
                archive.writestr(archive_info(name), blobs[name])
        if output.exists():
            fail("output appeared during packaging; refusing overwrite")
        os.link(tmp, output)  # fails if destination appeared, without clobber
    finally:
        if tmp is not None:
            tmp.unlink(missing_ok=True)


def unpack(archive_path, destination):
    if destination.exists() or destination.is_symlink():
        fail("destination exists; refusing to overwrite")
    with zipfile.ZipFile(archive_path) as archive:
        infos = archive.infolist()
        if len(infos) < 2 or len(infos) > MAX_FILES + 1:
            fail("invalid file count")
        names = [valid_name(i.filename) for i in infos]
        if len(set(names)) != len(names) or "manifest.json" not in names:
            fail("duplicate files or missing manifest")
        for info in infos:
            mode = (info.external_attr >> 16) & 0o170000
            if info.is_dir() or (mode not in (0, stat.S_IFREG)):
                fail("archive has non-regular file entry")
            if info.file_size > MAX_FILE_SIZE or info.compress_size > MAX_FILE_SIZE:
                fail("archive member exceeds limit")
        if sum(i.file_size for i in infos) > MAX_TOTAL:
            fail("expanded archive exceeds limit")
        if archive.getinfo("manifest.json").file_size > 256 * 1024:
            fail("manifest too large")
        manifest = json.loads(archive.read("manifest.json"))
        if not isinstance(manifest, dict) or manifest.get("format") != FORMAT or manifest.get("version") != 1:
            fail("unknown package format")
        entry = valid_name(manifest.get("entry"))
        if not entry.endswith(".effect"):
            fail("entry must be a native .effect")
        files = manifest.get("files")
        if not isinstance(files, dict) or set(files) != set(names) - {"manifest.json"} or entry not in files:
            fail("manifest does not enumerate exactly the included files")
        blobs = {}
        for name in files:
            valid_name(name)
            blob = archive.read(name)
            if sha256(blob) != files[name]:
                fail("package integrity check failed for " + name)
            blobs[name] = blob

    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".hs-unpack-", dir=destination.parent))
    try:
        for name, blob in blobs.items():
            target = staging.joinpath(*PurePosixPath(name).parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(blob)
        if destination.exists() or destination.is_symlink():
            fail("destination appeared during unpacking")
        staging.rename(destination)
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    p = commands.add_parser("pack")
    p.add_argument("effect", type=Path)
    p.add_argument("archive", type=Path)
    p.add_argument("--root", required=True, type=Path, help="Bounded shader/asset library root")
    p.add_argument("--asset", action="append", default=[], type=Path,
                   help="Additional referenced image, LUT, or other asset (repeatable)")
    p = commands.add_parser("unpack")
    p.add_argument("archive", type=Path)
    p.add_argument("destination", type=Path, help="New directory, never an existing one")
    args = parser.parse_args()
    try:
        if args.action == "pack":
            pack(args.effect, args.archive, args.root, args.asset)
        else:
            unpack(args.archive, args.destination)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile, json.JSONDecodeError) as error:
        parser.exit(1, "Effect package rejected: " + str(error) + "\n")


if __name__ == "__main__":
    main()
