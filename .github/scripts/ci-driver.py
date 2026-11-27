"""Caller-owned hypershader build/test/package adapter for obs-plugin-ci."""
import importlib.util
import os
import plistlib
from pathlib import Path
import shutil
import subprocess
import tarfile
import zipfile
import re

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("native_sdk", Path(__file__).with_name("native-sdk.py"))
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)
run = sdk.run
digest = sdk.digest

def files(root):
    paths = list(root.rglob("*"))
    if any(p.is_symlink() for p in paths):
        raise RuntimeError(f"Unexpected symlink in {root}")
    return {p.relative_to(root).as_posix(): p for p in paths if p.is_file()}

def assets():
    # The release manifest is authoritative, not an arbitrary data directory.
    expected = {}
    for line in (ROOT / "scripts/release-assets.txt").read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        decision, oid, path = line.split(maxsplit=2)
        if decision not in ("include", "exclude") or not path.startswith("data/"):
            raise RuntimeError(f"Invalid asset entry: {line}")
        blob = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD:" + path], text=True).strip()
        if blob != oid:
            raise RuntimeError(f"Unreviewed asset revision: {path}")
        if decision == "include":
            expected[path.removeprefix("data/")] = ROOT / path
    return expected

def verify_assets(data):
    expected = assets()
    actual = files(data)
    if set(actual) != set(expected) or any(actual[n].read_bytes() != expected[n].read_bytes() for n in expected):
        raise RuntimeError(f"Installed asset mismatch: {set(actual) ^ set(expected)}")

def prepare_payload(stage, package, target, version):
    sdk.sdk_pin(version)
    if target == "linux-x86_64":
        binary = stage / "hypershader.so"
        legacy = stage / "bin/64bit/hypershader.so"
        if legacy.read_bytes() != binary.read_bytes():
            raise RuntimeError("Linux legacy and modern staged binaries differ")
        data = stage / "data"
    elif target == "windows-x64":
        binary = stage / "hypershader/hypershader.dll"
        legacy = stage / "hypershader/bin/64bit/hypershader.dll"
        if legacy.read_bytes() != binary.read_bytes():
            raise RuntimeError("Windows legacy and modern staged binaries differ")
        data = stage / "hypershader/data"
    elif target in ("macos-arm64", "macos-x86_64"):
        bundle = stage / "hypershader.plugin"
        data = bundle / "Contents/Resources"
        verify_assets(data)
        binary = bundle / "Contents/MacOS/hypershader"
        if not binary.is_file() or not binary.stat().st_size:
            raise RuntimeError("Missing native module")
        info = plistlib.loads((bundle / "Contents/Info.plist").read_bytes())
        if info.get("CFBundleExecutable") != "hypershader" or info.get("CFBundlePackageType") != "BNDL":
            raise RuntimeError("Invalid plugin bundle metadata")
        files(bundle)
        shutil.copytree(bundle, package / bundle.name)
        result = package / bundle.name / "Contents/MacOS/hypershader"
        shutil.copy2(ROOT / "LICENSE", package / bundle.name / "Contents/Resources/LICENSE")
        return result
    else:
        raise RuntimeError(f"Unsupported target {target}")
    verify_assets(data)
    if not binary.is_file() or not binary.stat().st_size:
        raise RuntimeError("Missing native module")
    dest = package / "hypershader"
    module = dest / ("bin/64bit" if version == sdk.DEFAULT_VERSION else "") / binary.name
    module.parent.mkdir(parents=True)
    shutil.copy2(binary, module)
    shutil.copytree(data, dest / "data")
    shutil.copy2(ROOT / "LICENSE", dest / "LICENSE")
    return module

def manifest(paths):
    return "".join(f"{digest(path)}  {name}\n" for name, path in sorted(paths.items()))

def package_binary(package, archive, target):
    paths = files(package)
    if target == "linux-x86_64":
        with tarfile.open(archive, "w:gz") as tar:
            for name, path in sorted(paths.items()):
                tar.add(path, arcname=name, recursive=False)
        with tarfile.open(archive, "r:gz") as tar:
            members = tar.getmembers()
            if len(members) != len(paths) or {m.name for m in members} != set(paths) or any(
                not m.isfile() or tar.extractfile(m).read() != paths[m.name].read_bytes() for m in members
            ):
                raise RuntimeError("Binary tar payload mismatch")
    else:
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as z:
            for name, path in sorted(paths.items()):
                z.write(path, name)
        with zipfile.ZipFile(archive) as z:
            if len(z.namelist()) != len(paths) or set(z.namelist()) != set(paths) or z.testzip() or any(
                z.read(name) != path.read_bytes() for name, path in paths.items()
            ):
                raise RuntimeError("Binary ZIP payload mismatch")
    (archive.parent / "SHA256SUMS").write_text(manifest({archive.name: archive}), encoding="utf-8", newline="\n")
    return paths

def source_package(work, payload):
    work.mkdir()
    archive = work / "hypershader-source.tar.gz"
    candidate = work / "candidate.tar"
    run("python3", ROOT / "scripts/stage-release-source.py", "--output", candidate)
    # Re-root the already reviewed committed source inventory, omit agent guidance.
    with tarfile.open(candidate) as src, tarfile.open(archive, "w:gz") as dest:
        for member in src.getmembers():
            if any(part in ("AGENTS.md", "ACTIVE_WORK.md", ".meristem") for part in member.name.split("/")):
                continue
            stream = src.extractfile(member) if member.isfile() else None
            member.name = "hypershader/" + member.name
            dest.addfile(member, stream)
    candidate.unlink()
    (work / "SHA256SUMS").write_text(manifest({archive.name: archive}), encoding="utf-8", newline="\n")
    (work / "PAYLOAD-SHA256SUMS").write_text(manifest(payload), encoding="utf-8", newline="\n")

def verify_catalog(binary, data, prefix, work):
    # Use the exact source-built SDK, never the host or installed user plugin.
    env = os.environ.copy()
    env["PKG_CONFIG_PATH"] = str(prefix / "lib/pkgconfig")
    env["LD_LIBRARY_PATH"] = str(prefix / "lib")
    env["LIBGL_ALWAYS_SOFTWARE"] = "1"
    env["HS_AUDIT_LOG"] = str(work / "shader-audit.log")
    run("bash", ROOT / "scripts/verify-shipped-shaders.sh", binary, data, env=env)
    log = (work / "shader-audit.log").read_text(errors="replace")
    expected = {p.name for p in (data / "shaders/root").glob("*.effect")}
    rows = re.findall(r"^SHIPPED (\S+) .*? PASS=1(?: |$)", log, re.M)
    total = re.findall(r"^SHIPPED_TOTAL (\d+) PASS=1$", log, re.M)
    if len(rows) != len(expected) or set(rows) != expected or total != [str(len(expected))] or len(rows) < int(os.environ["MINIMUM_TESTS"]):
        raise RuntimeError("Incomplete native shader render audit")
    print(f"Verified {len(rows)} shipped effect render contracts")

def main():
    target, version, archive_name = (os.environ[k] for k in ("TARGET", "OBS_VERSION", "ARCHIVE"))
    if os.environ["MODULE"] != "hypershader" or os.environ["PUBLIC_NAME"] != "hypershader":
        raise RuntimeError("Wrong caller module")
    extension = "tar.gz" if target == "linux-x86_64" else "zip"
    if archive_name != f"hypershader-{target}-obs{version}-test.{extension}":
        raise RuntimeError("Wrong archive name")
    sdk.sdk_pin(version)
    work = Path(os.environ["RUNNER_TEMP"]) / "plugin-ci"
    work.mkdir()
    build = work / "build"
    stage = work / "stage"
    if target == "linux-x86_64":
        sdk_work = work / "obs"
        run("bash", ROOT / ".github/scripts/prepare-obs32-sdk.sh", sdk_work, version)
        prefix = sdk_work / "sdk"
        # Ensure the probe's pkg-config metadata cannot silently fall back to a host SDK.
        pc = prefix / "lib/pkgconfig/libobs.pc"
        if not pc.is_file():
            raise RuntimeError(f"Missing pinned SDK pkg-config metadata: {pc}")
        generator = ["-G", "Ninja", "-DCMAKE_BUILD_TYPE=RelWithDebInfo"]
        deps = []
    else:
        host, generator = sdk.native_config()
        if host != target:
            raise RuntimeError("Wrong native host")
        obs = sdk.source_archive(work, version)
        sdk.patch_obs_sdk(obs, target, version)
        prefix = work / "sdk"
        obs_build = work / "obs-build"
        run("cmake", "-S", obs, "-B", obs_build, *generator,
            "-DOBS_CMAKE_VERSION:STRING=3.0.0", f"-DOBS_VERSION_OVERRIDE:STRING={version}",
            "-DENABLE_PLUGINS=OFF", "-DENABLE_FRONTEND=OFF", "-DENABLE_BROWSER=OFF", "-DENABLE_SCRIPTING=OFF")
        run("cmake", "--build", obs_build, "--config", "Release", "--target", "obs-frontend-api", "--parallel", "4")
        run("cmake", "--install", obs_build, "--config", "Release", "--component", "Development", "--prefix", prefix)
        deps = list(sdk.dependency_dirs(obs, target, version))
    run("cmake", "-S", ROOT, "-B", build, *generator,
        "-DHYPERSHADER_EXTERNAL_SDK=ON", "-DHYPERSHADER_BUILD_OBS_FROM_SOURCE=OFF",
        "-DENABLE_QT=OFF", "-DENABLE_FRONTEND_API=OFF",
        "-DAUTO_ENABLE_ONNX_RUNTIME=OFF", "-DUSE_ONNX_RUNTIME=OFF", "-DUSE_LITERT=OFF",

        f"-Dlibobs_DIR={sdk.config_dir(prefix, 'libobs').as_posix()}",
        "-DCMAKE_PREFIX_PATH=" + ";".join(p.as_posix() for p in [prefix, *deps]))
    if target == "windows-x64":
        if "experimental:c11atomics" not in (build / "hypershader.vcxproj").read_text():
            raise RuntimeError("Generated Visual Studio project lacks C11 atomics")
    run("cmake", "--build", build, "--config", "RelWithDebInfo", "--parallel", "4")
    run("cmake", "--install", build, "--config", "RelWithDebInfo", "--prefix", stage)
    package = work / "payload"
    binary = prepare_payload(stage, package, target, version)
    if target == "linux-x86_64":
        verify_catalog(binary, package / "hypershader/data", prefix, work)
    if target.startswith("macos"):
        run(*sdk.mac_arch_command(binary, target))
        run("codesign", "--force", "--sign", "-", package / "hypershader.plugin")
        run("codesign", "--verify", "--strict", package / "hypershader.plugin")
    elif target == "windows-x64":
        with binary.open("rb") as f:
            if f.read(2) != b"MZ":
                raise RuntimeError("Invalid PE module")
            f.seek(0x3c)
            f.seek(int.from_bytes(f.read(4), "little"))
            if f.read(6) != b"PE\0\0\x64\x86":
                raise RuntimeError("Not Windows x64")
    else:
        exports = subprocess.check_output(["nm", "-D", "--defined-only", str(binary)], text=True)
        if not {"obs_module_load", "obs_module_set_pointer", "obs_module_ver"} <= {line.split()[-1] for line in exports.splitlines() if line.split()}:
            raise RuntimeError("Missing required OBS module exports")
    payload = package_binary(package, work / archive_name, target)
    if target == "linux-x86_64" and version == sdk.DEFAULT_VERSION:
        source_package(work.parent / "plugin-ci-source", payload)

if __name__ == "__main__":
    main()
