#!/usr/bin/env python3
"""Native shader authoring never depends on external-format identifiers."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OLD = re.compile(r"\b(?:i(?:Time(?:Delta)?|Resolution|Frame(?:Rate)?|SampleRate|Mouse|Date|Scale|Progress|PreviousFrame|Channel(?:Resolution|Time|Atlas)?[0-3]?|Buffer[AB]\w*|Graph\w*|Sight\w*|Face\w*|Landmarks|ReferenceLandmarks|Segmentation)|mainImage|HYPERSHADER_MAINIMAGE_WRAPPER)\b")

class NativeVocabulary(unittest.TestCase):
    def test_native_authoring_has_no_foreign_builtin_names(self):
        paths = [p for p in (ROOT / "data/shaders").rglob("*.effect") if p != ROOT / "data/shaders/tests/import/shadertoy_probe.effect"]
        paths += [p for p in (ROOT / "data/stdlib").glob("*.glsl") if not p.name.endswith("_compat.glsl")]
        paths += list((ROOT / "tools/shader-builder").rglob("*.js"))
        paths += [ROOT / p for p in ("data/shaders/SHADERS.md", "data/shaders/OBS_SHADER_SYNTAX.md", "src/llm/llm_context.h", "src/ui/shader_builder_dock.cpp")]
        violations = []
        for p in paths:
            source = p.read_text()
            # C/JS strings can contain a regex boundary or escaped newline
            # immediately before a shader identifier.
            source = re.sub(r"\\[bnrt]", " ", source)
            matches = sorted(set(OLD.findall(source)))
            if matches:
                violations.append((str(p.relative_to(ROOT)), matches))
        self.assertFalse(violations, violations)

    def test_native_contract_names(self):
        std = (ROOT / "data/stdlib/hypershader_standard.glsl").read_text()
        for name in ("hs_time", "hs_resolution", "hs_input0", "hs_delta_time", "hs_frame_index", "HYPERSHADER_PIXEL_WRAPPER", "hs_shade_pixel"):
            self.assertRegex(std, rf"\b{name}\b")
        runtime = (ROOT / "src/filter-hypershader.c").read_text()
        for name in ("hs_time", "hs_resolution", "hs_history", "hs_buffer_a", "hs_sight_depth", "hs_graph_budget_mib"):
            self.assertIn(f'"{name}"', runtime)

if __name__ == "__main__":
    unittest.main()
