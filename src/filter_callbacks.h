#ifndef FILTER_CALLBACKS_H
#define FILTER_CALLBACKS_H

#include <obs-module.h>

/* Callback prototypes shared between filter-hypershader.c and properties_builder.c */
bool channel_type_changed(void *priv, obs_properties_t *props, obs_property_t *p, obs_data_t *settings);
bool channel_preview_clicked(obs_properties_t *props, obs_property_t *property, void *data);
bool load_example_clicked(obs_properties_t *props, obs_property_t *property, void *data);
bool load_shadertoy_clicked(obs_properties_t *props, obs_property_t *property, void *data);
bool load_shadertoy_url_clicked(obs_properties_t *props, obs_property_t *property, void *data);
bool load_shadertoy_json_clicked(obs_properties_t *props, obs_property_t *property, void *data);
bool example_selection_changed2(void *priv, obs_properties_t *props, obs_property_t *p, obs_data_t *settings);
bool save_current_preset_clicked(obs_properties_t *props, obs_property_t *property, void *data);
bool export_shader_clicked(obs_properties_t *props, obs_property_t *property, void *data);

bool confirm_add_shader_file_clicked(obs_properties_t *props, obs_property_t *property, void *data);

bool create_from_raw_clicked(obs_properties_t *props, obs_property_t *property, void *data);
bool refresh_sources_clicked(obs_properties_t *props, obs_property_t *property, void *data);
bool reset_time_clicked(obs_properties_t *props, obs_property_t *property, void *data);
bool preview_sound_clicked(obs_properties_t *props, obs_property_t *property, void *data);
bool log_audio_mixes_clicked(obs_properties_t *props, obs_property_t *property, void *data);
bool shader_file_changed(void *priv, obs_properties_t *props, obs_property_t *p, obs_data_t *settings);
bool refresh_properties_clicked(obs_properties_t *props, obs_property_t *property, void *data);
bool analyze_image_clicked(obs_properties_t *props, obs_property_t *property, void *data);

#endif /* FILTER_CALLBACKS_H */
