#!/usr/bin/env python3
"""Source-contract regressions for PR #38. Complements native libobs probes."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent
runtime = (ROOT / "src/filter-hypershader.c").read_text()
audio = (ROOT / "src/audio/audio_processor.c").read_text()
ui = (ROOT / "src/hyper_ui.c").read_text()
dock = (ROOT / "src/ui/shader_builder_dock.cpp").read_text()
compat = (ROOT / "data/stdlib/shaderfilter_compat.glsl").read_text()

def check(name, ok):
    assert ok, f"FAIL: {name}"
    print(f"PASS: {name}")

check("selectable audio source uses source capture callback",
      "obs_source_add_audio_capture_callback(" in audio and
      "start_source_audio_connection(" in runtime and
      "stop_source_audio_connection(" in runtime)
check("no unintended master-mix audio when audio source is None",
      "s->sound_enabled && s->audio_source" in runtime)
check("managed shader revisions are stored under writable OBS config",
      'obs_module_get_config_path(' in dock and
      'builder-applied' in dock and 'hs_managed_shader_path' in runtime)
check("managed shader revisions are resolved by runtime",
      'hs_managed_shader_path(path)' in runtime)
check("shader metadata does not write all defaults as doubles",
      'obs_data_set_double(task->settings, prop_name, (double)m->default_val)' not in runtime and
      'obs_data_set_double(live_settings, prop_name, (double)m->default_val)' not in runtime)
check("preset migration retains legacy shader file",
      'hyper_ui_migrate_legacy_preset(settings)' in runtime and
      '"preset_schema"' in ui)
check("builder classification ignores comment-only tokens",
      'stripShaderComments(' in dock)
check("compatibility mod macro compiles to native function",
      'fmod(' in compat and not re.search(r'(?<![a-zA-Z_])mod\(', compat))
source = runtime.split("static void source_video_render(", 1)[1].split("struct obs_source_info hypershader_source_info", 1)[0]
check("standalone feedback target resets before reuse",
      'gs_texrender_reset(curr)' in source)
check("standalone feedback target allocation guarded",
      'if (!curr || !prev)' in source)
print("PASS: PR38 source contract checks")
