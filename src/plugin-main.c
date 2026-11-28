#include <obs-module.h>

static void log_startup_banner(void)
{
	blog(LOG_INFO, "[hypershader] ╔════════════════════════════════════╗");
	blog(LOG_INFO, "[hypershader] ║  hypershader loaded successfully  ║");
	blog(LOG_INFO, "[hypershader] ╚════════════════════════════════════╝");
}

static void log_runtime_status(void)
{
	blog(LOG_INFO, "[hypershader] Runtime status:");
	blog(LOG_INFO, "[hypershader]  ├─ Filters     : registered");
	blog(LOG_INFO, "[hypershader]  ├─ Source types : registered");
#ifdef HAVE_SHADER_BUILDER_DOCK
	blog(LOG_INFO, "[hypershader]  └─ Shader dock  : enabled");
#else
	blog(LOG_INFO, "[hypershader]  └─ Shader dock  : unavailable");
#endif
}

#ifdef HYPERSHADER_FORCE_OBS_MODULE_VER
#pragma message("WARNING: HYPERSHADER_FORCE_OBS_MODULE_VER is ON — do not use in release builds!")
/* Manual module declaration to override version from headers (32.1.0) with
 * runtime version (32.0.1).  The installed libobs headers are newer than the
 * running OBS binary, so we pin obs_module_ver() to the runtime ABI.
 * ONLY enable this via CMake option HYPERSHADER_FORCE_OBS_MODULE_VER=ON
 * for local development with ABI mismatches. */
static obs_module_t *obs_module_pointer;
MODULE_EXPORT void obs_module_set_pointer(obs_module_t *module);
void obs_module_set_pointer(obs_module_t *module)
{
	obs_module_pointer = module;
}
obs_module_t *obs_current_module(void)
{
	return obs_module_pointer;
}
MODULE_EXPORT uint32_t obs_module_ver(void);
uint32_t obs_module_ver(void)
{
	return (32u << 24) | (0u << 16) | 1u;
}
#else
OBS_DECLARE_MODULE()
#endif

OBS_MODULE_USE_DEFAULT_LOCALE("hypershader", "en-US")

extern void hs_include_cache_clear(void);
extern void hs_graph_budget_init(void);
extern struct obs_source_info hypershader_filter_info;
extern struct obs_source_info hypershader_source_info;
extern struct obs_source_info hypershader_transition_info;

#include "hyper_ui.h"

/* Shader Builder Dock (Qt WebEngine) */
#ifdef HAVE_SHADER_BUILDER_DOCK
extern void register_shader_builder_dock(void);
extern void unregister_shader_builder_dock(void);
#endif

bool obs_module_load(void)
{
	log_startup_banner();

#ifdef HYPERSHADER_DEBUG_ACTIVE_MARKER
	/* Unique runtime marker to verify this build is active (see AGENTS.md Rule 0) */
	blog(LOG_INFO, "DEBUG_ACTIVE_HYPERSHADER_9F2A1");
#endif

	if (!hs_channel_image_runtime_init())
		blog(LOG_WARNING, "[hypershader] Image-file channels are unavailable on this OBS runtime");

    hs_graph_budget_init();
	hyper_ui_init();
	obs_register_source(&hypershader_filter_info);
	obs_register_source(&hypershader_source_info);
	obs_register_source(&hypershader_transition_info);
	log_runtime_status();

#ifdef HAVE_SHADER_BUILDER_DOCK
	register_shader_builder_dock();
	blog(LOG_INFO, "[hypershader] └─ Dock bridge    : online");
#endif

	return true;
}

void obs_module_unload(void)
{
#ifdef HAVE_SHADER_BUILDER_DOCK
	unregister_shader_builder_dock();
#endif
	hyper_ui_free();
    hs_include_cache_clear();
}
