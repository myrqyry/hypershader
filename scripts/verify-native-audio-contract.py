#!/usr/bin/env python3
"""Ensure native audio uniforms are owned by hypershader, not inherited dialect aliases."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
legacy = re.compile(r"\biAudio(?:Level|Peak|BeatConfidence|BeatPhase|BarPhase|Beat|Envelope|Kick|Snare|Flux|BPM|Bands?(?:[0-7])?)\b")
names = (
    "audio_level", "audio_peak", "audio_beat", "audio_envelope",
    "audio_kick", "audio_snare", "audio_flux", "audio_bpm",
    "audio_beat_phase", "audio_bar_phase", "audio_beat_confidence",
    *(f"audio_band{i}" for i in range(8)),
)
sources = (
    *(p for p in (root / "src").rglob("*") if p.suffix in {".c", ".h", ".cpp", ".hpp"} and p.name != "miniaudio.h"),
    *(p for p in (root / "data").rglob("*") if p.suffix in {".glsl", ".effect", ".ini"}),
    *(p for p in (root / "scripts").rglob("*") if p.suffix in {".py", ".sh", ".cpp"} and p.name != "verify-native-audio-contract.py"),
)
bad = []
for source in sources:
    content = source.read_text(errors="replace")
    match = legacy.search(content)
    if match:
        bad.append(f"{source.relative_to(root)}: {match.group(0)}")
standard = (root / "data/stdlib/hypershader_standard.glsl").read_text()
runtime = (root / "src/filter-hypershader.c").read_text()
builtin = (root / "src/hypershader_data.h").read_text()
missing = [name for name in names if f"uniform float {name};" not in standard]
if 'strncmp(name, "audio_", 6)' in builtin:
    bad.append("native runtime treats custom audio_gain/audio_* controls as engine-owned")
if '"audio_kick"' not in builtin or 'audio_names[]' not in builtin:
    bad.append("builtin contract missing explicit audio_kick")
for signal in ("audio_level", "audio_peak"):
    if f's->ep_{signal} = gs_effect_get_param_by_name(s->effect, "{signal}");' not in runtime:
        bad.append(f"native shader load does not cache {signal}")
if '"audio_kick"' not in runtime:
    bad.append("runtime does not bind native audio_kick")
for shader in (root / "data/shaders/root").glob("*.effect"):
    for line in shader.read_text().splitlines():
        if "@audio" in line and any(re.search(r"\buniform\s+\w+\s+" + name + r"\b", line) for name in (*names, "audio_reaction_state")):
            bad.append(f"{shader.name}: built-in audio uniform must not have a metadata audio binding")
if missing:
    bad.append("native standard missing uniforms: "+", ".join(missing))
if bad:
    print("NATIVE_AUDIO_CONTRACT_FAIL:")
    print("\n".join(bad[:40]))
    print(f"offenders={len(bad)}")
    raise SystemExit(1)
print(f"NATIVE_AUDIO_CONTRACT_PASS=1 uniforms={len(names)} files={len(tuple(sources))}")
