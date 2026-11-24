#!/usr/bin/env python3
from pathlib import Path

source = Path("src/hyper_ui.c").read_text()
dock_source = Path("src/ui/shader_builder_dock.cpp").read_text()

start = source.find("obs_properties_t *hyper_ui_get_properties")
end = source.find("static void free_dynamic_examples_internal", start)
if start < 0 or end < 0:
    raise SystemExit("FAIL: could not isolate hyper_ui_get_properties")
runtime = source[start:end]

general_start = source.find("static void properties_add_compositing_controls")
general_end = source.find("void properties_add_input_settings", general_start)
general = source[general_start:general_end]

checks = [
    ("runtime exposes linked category and shader chooser",
     '"shader_category"' in runtime and '"preset"' in runtime and
     '"Category"' in runtime and 'shader_category_changed2' in runtime),
    ("category filters without changing shader_path",
     "shader_picker_fill(preset, initial_category, current)" in runtime and
     "shader_category_changed2" in source and
     "obs_property_list_clear(picker)" in source),
    ("saved category is independent of shader preset ID",
     'obs_data_set_default_string(settings, "shader_category", "filter")' in source and
     'obs_data_set_default_int(settings, "preset", -1)' in source),
    ("new filter begins unselected", 'obs_data_set_default_int(settings, "preset", -1)' in source),
    ("new filter has realtime animation default",
     'obs_data_set_default_double(settings, "speed", 1.0)' in source),
    ("historical built-in IDs remain stable",
     'strcmp(id, "root/pframe_drift.effect") == 0' in source and
     'strcmp(id, "root/realworld_focus_bulge.effect") == 0' in source),
    ("picker scans installed root and loose shaders",
     'scan_installed_effect_directory(shaders_dir, "root")' in source and
     'scan_installed_effect_directory(shaders_dir, "")' in source),
    ("picker includes installed named collections",
     'scan_installed_effect_directory(shaders_dir, relative)' in source),
    ("first-open shader visibility follows current settings",
     "sync_ui_visibility(s, props, current_settings)" in runtime),
    ("custom file appears after chooser",
     runtime.find("properties_add_header(props)") > runtime.find('obs_properties_add_list(props, "preset"')),
    ("runtime exposes dock shortcut", '"open_shader_builder"' in runtime),
    ("runtime no longer builds tag browser", '"preset_tag_filter"' not in runtime),
    ("runtime no longer filters user shaders by tags", "current_tag_filter" not in runtime),
    ("audio meter is not constructed", 'obs_properties_add_bool(props, "enable_audio_meter"' not in source),
    ("showcase comparison is not constructed", '"showcase_enabled"' not in general),
    ("dead global scale is not constructed", '"scale"' not in general),
    ("global time controls are not conditional on compiled shader inputs",
     "shader_uses_runtime_time_controls" not in source and
     'properties_add_global_overrides(props, s)' in runtime),
    ("global speed applies to every shader instance",
     'global, "speed", "Global Animation Speed"' in general),
    ("every shader exposes compositor controls",
     'properties_add_global_overrides(props, s)' in runtime and
     'properties_add_compositing_controls(global)' in general),
    ("animation and compositor share one global overrides group",
     '"global_overrides_group", "Global Overrides"' in general and
     '"controls_group"' not in general and '"composition_group"' not in general),
    ("independent base/effect opacity controls",
     '"compose_base_opacity"' in source and '"compose_effect_opacity"' in source),
    ("optional visibility is null-safe", "set_property_visible_if_present" in source),
    ("AI authoring is absent from runtime filters", "properties_add_ai_settings(props, s)" not in runtime),
    ("dock owns AI prompt", "m_aiPrompt" in dock_source),
    ("dock owns async generation", "onGenerateAiClicked" in dock_source and "std::thread" in dock_source),
    ("dock finalizes generated hs_shade_pixel with native wrapper", "HYPERSHADER_PIXEL_WRAPPER" in dock_source),
    ("dock uses local LLM system prompt", "LLM_SYSTEM_PROMPT" in dock_source),
]

failed = 0
for label, ok in checks:
    print(("PASS" if ok else "FAIL") + ": " + label)
    if not ok:
        failed += 1
if failed:
    raise SystemExit(1)
