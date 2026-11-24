#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
cmake = (root / "CMakeLists.txt").read_text()
assert "_hypershader_modern_plugin_destination" in cmake
assert 'DESTINATION "${_hypershader_modern_plugin_destination}"' in cmake
assert "OBS_PLUGIN_DESTINATION MATCHES" in cmake and "64bit$" in cmake

print("PASS: CMake preserves both OBS module layouts")
