"""Keep named uniform lookup and settings reads out of repeated pass binding."""
from pathlib import Path
import unittest

source = (Path(__file__).resolve().parents[1] / "src/filter-hypershader.c").read_text()

def body(name):
    import re
    return re.search(r"static [^;\n]+ " + name + r"\([^;]*?\)\s*\{(.*?)\n\}", source, re.S)[1]

class RenderControlCache(unittest.TestCase):
    def test_pass_binding_uses_resolved_values(self):
        bind = body("graph_bind_controls")
        for expensive in ("obs_source_get_settings", "obs_data_", "snprintf", "strcmp",
                          "gs_effect_get_param_info", "performance_modulated_value"):
            self.assertNotIn(expensive, bind)
        self.assertIn("render_value", bind)

    def test_frame_paths_do_not_read_settings(self):
        for name in ("graph_snapshot_controls", "performance_modulated_value",
                     "update_reaction_state", "update_animated_channels",
                     "update_sprite_atlases", "sight_tick"):
            with self.subTest(name=name):
                for expensive in ("obs_source_get_settings", "obs_data_",
                                  "reaction_setting(", "atlas_setting("):
                    self.assertNotIn(expensive, body(name))

    def test_compositor_handles_resolved_at_creation(self):
        self.assertNotIn("gs_effect_get_param_by_name", body("output_draw_composited"))
        self.assertIn("compose_base_image", body("output_compositor_prepare"))

if __name__ == "__main__":
    unittest.main()
