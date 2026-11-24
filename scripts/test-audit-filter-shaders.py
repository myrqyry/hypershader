#!/usr/bin/env python3
import importlib.util
from pathlib import Path

path = Path(__file__).with_name("audit-filter-shaders.py")
spec = importlib.util.spec_from_file_location("audit_filter_shaders", path)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

sample = (
    "SHADER_AUDIT samples=10 mean_rgb=62.07 delta_rgb=50.76 "
    "min_rgb=10 max_rgb=242 modified_frames=10 repeat_delta_rgb=0.125\n"
)
metrics = mod.parse_audit_metrics(sample)
assert metrics["samples"] == 10
assert metrics["mean_rgb"] == 62.07
assert metrics["modified_frames"] == 10
assert metrics["repeat_delta_rgb"] == 0.125
assert mod.determinism_state(metrics) == "DIVERGED"

scientific = mod.parse_audit_metrics(
    "SHADER_AUDIT samples=10 mean_rgb=62.07 delta_rgb=50.76 "
    "min_rgb=10 max_rgb=242 modified_frames=10 repeat_delta_rgb=1.02124e-06"
)
assert scientific["repeat_delta_rgb"] == 1.02124e-06
assert mod.determinism_state(scientific) == "MATCH"

metrics["repeat_delta_rgb"] = 0.0
assert mod.determinism_state(metrics) == "MATCH"
assert mod.determinism_state({"samples": 10}) == "UNKNOWN"
print("PASS: audit parser preserves repeat-determinism signal")


visible = {
    "samples": 10,
    "mean_rgb": 62.0,
    "delta_rgb": 50.0,
    "min_rgb": 10,
    "max_rgb": 242,
    "modified_frames": 10,
    "repeat_delta_rgb": 0.125,
}
assert mod.classify_metrics(visible) == "POSSIBLE_NONDETERMINISTIC"
visible["repeat_delta_rgb"] = 0.0
assert mod.classify_metrics(visible) == "EFFECT_VISIBLE"
