
#!/usr/bin/env python3
from pathlib import Path
import json

root = Path(__file__).resolve().parents[2]
spec = json.loads((root / "buildspec.json").read_text())
obs = spec["dependencies"]["obs-studio"]
assert obs["hashes"].get("linux-x86_64"), "Linux OBS source hash must be pinned"

linux_buildspec = root / "cmake/linux/buildspec.cmake"
assert linux_buildspec.exists(), "Linux source SDK bootstrap is missing"
text = linux_buildspec.read_text()
assert "set(dependencies_list obs-studio)" in text
assert "_check_dependencies()" in text

defaults = (root / "cmake/linux/defaults.cmake").read_text()
assert "HYPERSHADER_BUILD_OBS_FROM_SOURCE" in defaults
assert "include(buildspec)" in defaults
assert "_hypershader_pinned_libobs_dir" in defaults
assert "DEFINED ENV{libobs_DIR}" in defaults
assert "elseif(NOT libobs_DIR)" in defaults
assert "HYPERSHADER_BUILD_OBS_FROM_SOURCE AND (NOT DEFINED libobs_DIR OR NOT libobs_DIR)" in defaults
assert r"\.deps/" in defaults, "source packages must exclude downloaded SDKs"
assert r"\.meristem/local/" in defaults, "source packages must exclude local Meristem state"

bootstrap = (root / "cmake/common/buildspec_common.cmake").read_text()
assert "elseif(OS_LINUX)" in bootstrap
assert 'set(_cmake_generator "Ninja")' in bootstrap

builder = (root / ".github/scripts/build-ubuntu").read_text()
assert "-DHYPERSHADER_BUILD_OBS_FROM_SOURCE=ON" in builder

setup = (root / ".github/scripts/utils.zsh/setup_ubuntu").read_text()
assert "ppa:obsproject/obs-studio" not in setup
package_lines = [line.strip().rstrip("\\").strip() for line in setup.splitlines()]
assert "obs-studio" not in package_lines

workflow = (root / ".github/workflows/ci.yml").read_text()
for runner in ("ubuntu-24.04", "windows-2022", "macos-15"):
    assert f"runs-on: {runner}" in workflow
assert "ubuntu-latest" not in workflow
assert "test-ci-sdk-contract.py" in workflow

obs33_workflow = (root / ".github/workflows/obs33-compatibility.yml").read_text()
assert "OBS 33.0.1" in obs33_workflow
assert "runs-on: ubuntu-26.04" in obs33_workflow
assert "build-obs33-compat.sh" in obs33_workflow
windows_helpers = (root / "cmake/windows/helpers.cmake").read_text()
assert 'DESTINATION "${target}/bin/64bit"' in windows_helpers
assert 'DESTINATION "${target}"' in windows_helpers

cmake_lists = (root / "CMakeLists.txt").read_text()
assert 'PROPERTIES COMPILE_OPTIONS "/std:c17;/experimental:c11atomics"' in cmake_lists, \
    "MSVC atomics flags must be serialized as per-C-source AdditionalOptions"
for required_c_source in (
    "src/plugin-main.c",
    "src/filter-hypershader.c",
    "src/audio/audio_processor.c",
    "src/hyper_audio.c",
):
    assert required_c_source in cmake_lists
compiler_common = (root / "cmake/common/compiler_common.cmake").read_text()
assert "set(CMAKE_C_STANDARD 17)" in compiler_common
assert "set(CMAKE_C_STANDARD_REQUIRED TRUE)" in compiler_common
windows_builder = (root / ".github/scripts/Build-Windows.ps1").read_text()
assert "Generated Visual Studio project is missing the C11 atomics opt-in." in windows_builder
assert "experimental:c11atomics" in windows_builder
for audio_source in ("src/hyper_audio.c", "src/audio/audio_processor.c"):
    source = (root / audio_source).read_text()
    assert "__atomic_load_n(&s->audio_reconfig_pending" not in source
    assert "__atomic_store_n(&s->audio_reconfig_pending" not in source
assert "atomic_load_explicit(&s->audio_reconfig_pending" in (root / "src/hyper_audio.c").read_text()
assert "atomic_store_explicit(&s->audio_reconfig_pending" in (root / "src/audio/audio_processor.c").read_text()
filter_source = (root / "src/filter-hypershader.c").read_text()
state_header = (root / "src/hypershader_data.h").read_text()
assert "gs_get_adapter_name" not in filter_source, "do not call a nonexistent OBS graphics API"
assert "gs_stage_texture(s->audio_staging_surf" not in filter_source, "staging is GPU-to-CPU readback, not upload"
assert "audio_staging_surf" not in state_header
assert "gs_texture_set_image(s->audio_texture" in filter_source

# Windows portability: compiled sources must use the shared compatibility layer
# instead of unguarded POSIX-only APIs.
for portable_source in (
    "src/filter-hypershader.c",
    "src/hyper_ui.c",
    "src/shader/shader_metadata.c",
):
    text = (root / portable_source).read_text()
    assert "platform_compat.h" in text, f"{portable_source} must include platform_compat.h"
assert "realpath(" not in filter_source.replace("hs_realpath(", ""), "raw realpath remains in filter source"
assert "strcasecmp(" not in filter_source.replace("hs_strcasecmp(", ""), "raw strcasecmp remains in filter source"
hyper_ui = (root / "src/hyper_ui.c").read_text()
assert "strtok_r(" not in hyper_ui.replace("hs_strtok_r(", ""), "raw strtok_r remains in UI source"
shader_metadata = (root / "src/shader/shader_metadata.c").read_text()
assert "realpath(" not in shader_metadata.replace("hs_realpath(", ""), "raw realpath remains in metadata source"
audio_processor = (root / "src/audio/audio_processor.c").read_text()
assert "clock_gettime(" not in audio_processor
compat = (root / "src/platform_compat.h").read_text()
for required in ("_fullpath", "_stricmp", "strtok_s", "S_ISDIR", "S_ISREG"):
    assert required in compat, f"Windows compatibility helper missing {required}"
# The curated repository uses main, not the development repository's master.
for candidate in (workflow, obs33_workflow):
    assert candidate.count("branches: [main, master]") == 2, "CI must cover the default branch for pushes and PRs"
    assert "actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1" in candidate
    assert "persist-credentials: false" in candidate
    assert "cancel-in-progress: true" in candidate
    assert "github.sha" not in candidate, "Concurrency must cancel superseded runs on the same ref"
assert "check_readme_links.py" in workflow
assert "test-readme-links.py" in workflow

print("PASS: hypershader CI uses explicit runners, pinned Linux OBS SDK, and portable graphics/atomic paths")
