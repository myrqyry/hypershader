#!/usr/bin/env python3
"""Keep OBS/Qt settings labels readable without emoji-font fallback.

The core OBS property system renders text labels, not theme-owned QIcons.
Assert labels remain text-only across this plugin's English translations.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
LOCALE = ROOT / "data/locale/en-US.ini"
NATIVE_UI = ROOT / "src/hyper_ui.c"

def verify() -> list[str]:
    errors: list[str] = []
    entries: dict[str, str] = {}
    for number, line in enumerate(LOCALE.read_text(encoding="utf-8").splitlines(), 1):
        if not line or line.startswith("[") or line.startswith("#"):
            continue
        match = re.fullmatch(r'([A-Za-z_][A-Za-z_0-9]*)="(.*)"', line)
        if not match:
            errors.append(f"Locale line {number}: malformed entry")
            continue
        key, value = match.groups()
        if key in entries:
            errors.append(f"Locale line {number}: duplicate key {key}")
        entries[key] = value
        if any(ord(c) > 127 for c in value):
            errors.append(f"Locale line {number}: font-dependent non-ASCII glyph in {key}: {value!r}")
        if not value.strip():
            errors.append(f"Locale line {number}: empty label {key}")
    # These are observable OBS properties; preserve their meaning and keys.
    expected = {
        "ShaderPreset": "Shader Preset",
        "CustomFile": "Custom File...",
        "OpenShaderBuilder": "Open Shader Builder",
        "AnimationSpeed": "Animation Speed",
        "ResetShaderTimer": "Reset Shader Timer",
        "InputType": "Input Type",
        "ShaderParameters": "Shader Parameters",
        "GeneralSettings": "General Settings",
        "AudioReactive": "Audio Reactive",
    }
    for key, value in expected.items():
        if entries.get(key) != value:
            errors.append(f"{key}: expected {value!r}, found {entries.get(key)!r}")
    # These two native OBS property labels bypass localization entirely.
    native_source = NATIVE_UI.read_text(encoding="utf-8")
    if '"Image File Path"' not in native_source:
        errors.append("Native channel file picker must use an Image File Path label")
    if '"●"' in native_source or '"○"' in native_source:
        errors.append("Channel indicators must use readable text, not font glyphs")
    if '"📁 Image File Path"' in native_source:
        errors.append("Native channel file picker contains emoji")
    if '"Choose a shader…"' in native_source:
        errors.append("Preset picker contains a font-dependent glyph")
    print(f"SETTINGS_LABELS locale_entries={len(entries)} failures={len(errors)} PASS={int(not errors)}")
    return errors

if __name__ == "__main__":
    failures = verify()
    for error in failures:
        print("FAIL:", error)
    sys.exit(bool(failures))
