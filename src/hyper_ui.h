#pragma once
#include <obs-module.h>
#include "hypershader_data.h"

/**
 * @brief Initialize UI resources (e.g. scan examples)
 */
void hyper_ui_init(void);

/**
 * @brief Free UI resources
 */
void hyper_ui_free(void);

/**
 * @brief Main properties generation function
 * @param data struct hypershader_data pointer
 * @return obs_properties_t*
 */
obs_properties_t *hyper_ui_get_properties(void *data);
/* Kind-specific callbacks keep the correct ordering even when OBS passes
 * data=NULL while enumerating addable source/filter/transition properties. */
obs_properties_t *hyper_ui_get_source_properties(void *data);
obs_properties_t *hyper_ui_get_transition_properties(void *data);

/**
 * @brief Helper to get user presets directory
 * @return bstrdup'd path (caller must free)
 */
char *hyper_ui_get_user_presets_dir(void);

/**
 * @brief Save current shader as a preset
 */
/**
 * @brief Get default settings
 */
void hyper_ui_get_defaults(obs_data_t *settings);
void hyper_ui_get_source_defaults(obs_data_t *settings);
void hyper_ui_get_transition_defaults(obs_data_t *settings);
void hyper_ui_migrate_legacy_preset(obs_data_t *settings);
bool scan_shaders_dir(void);
void apply_parameter_default(gs_eparam_t *param, struct shader_param_meta *m);
