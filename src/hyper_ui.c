#include <obs-module.h>
#include "hyper_ui.h"
#include "hypershader_data.h"
#include "shader/shader_display_name.h"
#include "shader/shader_categories.h"
#include "shader/shader_visibility.h"
#include "shader/shader_info.h"
#include "filter_callbacks.h"
#include "source-selection.h"
#include "interop/sight_client.h"
#include <util/darray.h>
#include "jsmn.h"
#include "platform_compat.h"
#include <util/bmem.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <direct.h>

#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif

#ifndef mkdir
#define mkdir(path, mode) _mkdir(path)
#endif

struct dirent {
	char d_name[MAX_PATH];
};

typedef struct {
	HANDLE handle;
	WIN32_FIND_DATAA find_data;
	struct dirent entry;
	int first;
} DIR;

static DIR *opendir(const char *path)
{
	if (!path || !path[0])
		return NULL;

	char pattern[MAX_PATH];
	size_t len = strlen(path);
	if (len == 0 || len >= MAX_PATH - 2)
		return NULL;

	if (path[len - 1] == '/' || path[len - 1] == '\\')
		snprintf(pattern, sizeof(pattern), "%s*", path);
	else
		snprintf(pattern, sizeof(pattern), "%s\\*", path);

	DIR *dir = bzalloc(sizeof(*dir));
	if (!dir)
		return NULL;

	dir->handle = FindFirstFileA(pattern, &dir->find_data);
	if (dir->handle == INVALID_HANDLE_VALUE) {
		bfree(dir);
		return NULL;
	}

	dir->first = 1;
	return dir;
}

static struct dirent *readdir(DIR *dir)
{
	if (!dir)
		return NULL;

	if (dir->first) {
		dir->first = 0;
	} else {
		if (!FindNextFileA(dir->handle, &dir->find_data))
			return NULL;
	}

	strncpy(dir->entry.d_name, dir->find_data.cFileName, sizeof(dir->entry.d_name) - 1);
	dir->entry.d_name[sizeof(dir->entry.d_name) - 1] = '\0';
	return &dir->entry;
}

static int closedir(DIR *dir)
{
	if (!dir)
		return -1;

	if (dir->handle != INVALID_HANDLE_VALUE)
		FindClose(dir->handle);

	bfree(dir);
	return 0;
}
#else
#include <dirent.h>
#endif

#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif

// Forward declarations
// Note: Use obs_source_update() instead of calling filter_update directly
// Actually filter_update is in core. hyper_ui should call obs_source_update?
// preset_changed calls filter_update. We might need to expose filter_update in a header or use obs_source_update which triggers it.
// In filter-hypershader.c, filter_update is static.
// But obs_source_update(context, settings) will trigger .update callback (filter_update).
// So relying on OBS to call filter_update is safer than calling it directly.

/* Forward declarations of internal static functions */
char *shaders_dir_from_known(void);
bool scan_shaders_dir(void);
char *hs_resolve_data_file(const char *path);
static bool scan_shaders_dir_internal(void);
void free_dynamic_examples(void);
static void free_dynamic_examples_internal(void);
void *sound_preview_thread_fn(void *data);
void *probe_audio_mixes_thread(void *data);
static void load_user_presets(void);
static void load_user_presets_internal(void);

static void sync_ui_visibility(struct hypershader_data *s, obs_properties_t *props, obs_data_t *settings);
static void sync_input_visibility(struct hypershader_data *s, obs_properties_t *props, obs_data_t *settings);

#ifdef HAVE_SHADER_BUILDER_DOCK
extern void shader_builder_dock_show(void);
#endif

bool open_shader_dock_clicked(obs_properties_t *props, obs_property_t *property, void *data);

/* The OBS property tree owns these independent, parsed visibility rules. No
 * metadata pointers or shader/filter lifetime are captured by callbacks. */
struct hs_ui_visibility_rule {
	hs_visibility_condition condition;
	obs_property_t *target;
	enum gs_shader_param_type controller_type;
	double fallback;
	bool controller_exists;
};
struct hs_ui_visibility {
	size_t count;
	struct hs_ui_visibility_rule rules[];
};

static bool hs_ui_visibility_apply(struct hs_ui_visibility *state, obs_data_t *settings)
{
	if (!state)
		return false;
	bool changed = false;
	for (size_t i = 0; i < state->count; ++i) {
		struct hs_ui_visibility_rule *rule = &state->rules[i];
		double value = rule->fallback;
		if (rule->controller_exists && settings) {
			char key[160];
			snprintf(key, sizeof(key), "param_%s", rule->condition.controller);
			if (obs_data_has_user_value(settings, key)) {
				switch (rule->controller_type) {
				case GS_SHADER_PARAM_BOOL:
					value = obs_data_get_bool(settings, key) ? 1.0 : 0.0;
					break;
				case GS_SHADER_PARAM_INT:
					value = (double)obs_data_get_int(settings, key);
					break;
				default:
					value = obs_data_get_double(settings, key);
				}
			}
		}
		const bool visible = rule->controller_exists &&
			hs_visibility_evaluate(&rule->condition, value);
		if (obs_property_visible(rule->target) != visible) {
			obs_property_set_visible(rule->target, visible);
			changed = true;
		}
	}
	return changed;
}

static bool hs_ui_visibility_changed(void *priv, obs_properties_t *props, obs_property_t *property,
				     obs_data_t *settings)
{
	(void)props;
	(void)property;
	return hs_ui_visibility_apply(priv, settings);
}

static void hs_ui_visibility_attach(obs_properties_t *props, struct hs_ui_visibility *state)
{
	for (obs_property_t *property = obs_properties_first(props); property;
	     obs_property_next(&property)) {
		if (obs_property_get_type(property) == OBS_PROPERTY_GROUP) {
			hs_ui_visibility_attach(obs_property_group_content(property), state);
		} else if (strncmp(obs_property_name(property), "param_", 6) == 0) {
			obs_property_set_modified_callback2(property, hs_ui_visibility_changed, state);
		}
	}
}

void properties_add_shader_params(obs_properties_t *props, struct hypershader_data *s)
{
	/* ═══════════════════════════════════════════════════════════════
     * CRITICAL SAFETY CHECKS - These prevent the segfault!
     * ═══════════════════════════════════════════════════════════════ */

	if (!props) {
		blog(LOG_ERROR, "[HyperShader] FATAL: Properties window pointer is NULL!");
		return;
	}

	if (!s) {
		blog(LOG_ERROR, "[HyperShader] FATAL: Shader data pointer is NULL!");
		return;
	}

	pthread_mutex_lock(&s->mutex);

	/* Use cached param views instead of live gs_effect inspection.
	 * This avoids obs_enter_graphics() during property construction,
	 * which can cause reentrancy/deadlock issues. */
	if (!s->param_views || s->param_view_count == 0) {
		blog(LOG_DEBUG, "[HyperShader] No cached shader params, skipping parameter UI");
		pthread_mutex_unlock(&s->mutex);
		return;
	}

	struct shader_meta_rc *meta_rc = s->metadata_rc;
	if (meta_rc) {
		metadata_rc_retain(meta_rc);
	} else {
		blog(LOG_WARNING, "[HyperShader] Shader metadata not initialized");
		pthread_mutex_unlock(&s->mutex);
		return;
	}

	blog(LOG_DEBUG, "[HyperShader] Building UI for %zu cached shader parameters", s->param_view_count);

	obs_properties_t *shader_props = obs_properties_create();
	if (!shader_props) {
		blog(LOG_ERROR, "[HyperShader] Failed to create properties group!");
		metadata_rc_release(&meta_rc);
		pthread_mutex_unlock(&s->mutex);
		return;
	}

	shader_meta_t *current_meta = &meta_rc->meta;

	/* One allocation for all rules; released when OBS frees shader_props. */
	struct hs_ui_visibility *visibility = bzalloc(sizeof(*visibility) +
						     current_meta->num * sizeof(visibility->rules[0]));
	if (visibility)
		obs_properties_set_param(shader_props, visibility, bfree);

	/* Group management variables */
	const char *last_group_name = NULL;
	obs_properties_t *last_group_props = shader_props;

	for (size_t i = 0; i < s->param_view_count; i++) {
		const char *name = s->param_views[i].name;
		enum gs_shader_param_type type = s->param_views[i].type;

		if (!name || !name[0])
			continue;

		if (hs_runtime_uniform(s, name))
			continue;

		/* Skip parameters controlled by audio mappings */
		bool is_mapped = false;
		for (int m = 0; m < AUDIO_MAP_SLOTS; m++) {
			if (s->audio_map_name[m] && strcmp(s->audio_map_name[m], name) == 0) {
				is_mapped = true;
				break;
			}
		}
		if (is_mapped)
			continue;

		char prop_name[128];
		snprintf(prop_name, sizeof(prop_name), "param_%s", name);

		/* Default ranges for numeric parameters */
		float ex_min = -1000.0f;
		float ex_max = 1000.0f;
		float ex_step = 0.05f;

		/* Build human-readable label */
		char clean_label[128];
		strncpy(clean_label, name, sizeof(clean_label) - 1);
		clean_label[sizeof(clean_label) - 1] = '\0';
		for (char *c = clean_label; *c; ++c) {
			if (*c == '_')
				*c = ' ';
		}
		const char *ex_label = clean_label;

		/* Look up metadata overrides */
		struct shader_param_meta *meta = NULL;
		for (size_t j = 0; j < current_meta->num; j++) {
			if (current_meta->array[j].name && strcmp(current_meta->array[j].name, name) == 0) {
				meta = &current_meta->array[j];
				if (meta->has_min)
					ex_min = meta->min;
				if (meta->has_max)
					ex_max = meta->max;
				if (meta->has_step)
					ex_step = meta->step;
				if (meta->label && meta->label[0])
					ex_label = meta->label;
				break;
			}
		}

		/* Handle grouping */
		obs_properties_t *target_props = shader_props;
		const char *group_name = (meta && meta->group) ? meta->group : NULL;

		if (group_name) {
			if (last_group_name && strcmp(group_name, last_group_name) == 0) {
				target_props = last_group_props;
			} else {
				char group_id[128];
				snprintf(group_id, sizeof(group_id), "group_%s", group_name);

				obs_property_t *gp = obs_properties_get(shader_props, group_id);
				if (!gp) {
					obs_properties_t *group_props = obs_properties_create();
					gp = obs_properties_add_group(shader_props, group_id, group_name,
								      OBS_GROUP_NORMAL, group_props);
				}

				obs_properties_t *content = obs_property_group_content(gp);
				if (content)
					target_props = content;

				last_group_name = group_name;
				last_group_props = target_props;
			}
		}

		if (obs_properties_get(target_props, prop_name))
			continue;

		switch (type) {
		case GS_SHADER_PARAM_FLOAT:
			if (meta && (meta->has_min || meta->has_max)) {
				obs_properties_add_float_slider(target_props, prop_name, ex_label, ex_min, ex_max,
								ex_step);
			} else {
				obs_properties_add_float(target_props, prop_name, ex_label, ex_min, ex_max, ex_step);
			}
			break;

		case GS_SHADER_PARAM_INT:
			if (meta && meta->list_options) {
				obs_property_t *p = obs_properties_add_list(target_props, prop_name, ex_label,
									    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
				char *dup = bstrdup(meta->list_options);
				char *saveptr = NULL;
				char *token = hs_strtok_r(dup, ",", &saveptr);
				int val = 0;
				while (token) {
					obs_property_list_add_int(p, token, val++);
					token = hs_strtok_r(NULL, ",", &saveptr);
				}
				bfree(dup);
			} else {
				obs_properties_add_int(target_props, prop_name, ex_label, (int)ex_min, (int)ex_max,
						       (int)(ex_step > 0.05 ? ex_step : 1));
			}
			break;

		case GS_SHADER_PARAM_BOOL:
			obs_properties_add_bool(target_props, prop_name, ex_label);
			break;

		case GS_SHADER_PARAM_VEC4:
		case GS_SHADER_PARAM_VEC3:
			obs_properties_add_color(target_props, prop_name, ex_label);
			break;

		case GS_SHADER_PARAM_TEXTURE:
			if (meta && meta->is_text) {
				obs_properties_add_text(target_props, prop_name, ex_label, OBS_TEXT_DEFAULT);
			}
			break;

		default:
			blog(LOG_DEBUG, "[HyperShader] Unsupported parameter type %d for '%s'", type, name);
			break;
		}

		if (visibility && meta && meta->visible_if) {
			struct hs_ui_visibility_rule rule = {0};
			if (!hs_visibility_parse(meta->visible_if, &rule.condition)) {
				blog(LOG_WARNING, "[HyperShader] Invalid @show_if condition for %s", name);
			}
			/* Missing/invalid controller means hidden, never a guessed value. */
			rule.target = obs_properties_get(target_props, prop_name);
			for (size_t j = 0; j < s->param_view_count; ++j) {
				if (s->param_views[j].name &&
				    strcmp(s->param_views[j].name, rule.condition.controller) == 0) {
					rule.controller_exists = true;
					rule.controller_type = s->param_views[j].type;
					for (size_t k = 0; k < current_meta->num; ++k) {
						struct shader_param_meta *ctrl = &current_meta->array[k];
						if (ctrl->name &&
						    strcmp(ctrl->name, rule.condition.controller) == 0 &&
						    ctrl->has_default)
							rule.fallback = ctrl->default_val;
					}
					break;
				}
			}
			if (rule.target)
				visibility->rules[visibility->count++] = rule;
		}
	}

	if (visibility && visibility->count) {
		hs_ui_visibility_attach(shader_props, visibility);
		obs_data_t *settings = s->context ? obs_source_get_settings(s->context) : NULL;
		hs_ui_visibility_apply(visibility, settings);
		if (settings)
			obs_data_release(settings);
	}

	if (obs_properties_first(shader_props)) {
		blog(LOG_DEBUG, "[HyperShader] Added 'Shader Parameters' group");
		obs_properties_add_group(props, "shader_params_group", obs_module_text("ShaderParameters"),
					 OBS_GROUP_NORMAL, shader_props);
	} else {
		blog(LOG_DEBUG, "[HyperShader] No shader parameters found");
		obs_properties_destroy(shader_props);
	}

	metadata_rc_release(&meta_rc);
	pthread_mutex_unlock(&s->mutex);
}

void properties_add_header(obs_properties_t *props)
{
	obs_property_t *pfile = obs_properties_add_path(props, "shader_file", obs_module_text("CustomShaderFile"),
						OBS_PATH_FILE, "Effect Files (*.effect);;All Files (*.*)",
						NULL);
	obs_property_set_long_description(pfile,
		"Custom shader files must be in HyperShader's data folder. "
		"Absolute paths outside that folder are blocked by the shader loader.");
	obs_property_set_modified_callback2(pfile, shader_file_changed, NULL);
}

static bool randomize_parameters_callback(obs_properties_t *props, obs_property_t *button, void *data)
{
	UNUSED_PARAMETER(button);
	struct hypershader_data *s = data;
	if (!s || !s->context)
		return false;

	obs_data_t *settings = obs_source_get_settings(s->context);
	if (!settings)
		return false;

	obs_property_t *prop = obs_properties_first(props);
	while (prop) {
		const char *name = obs_property_name(prop);
		/* Only randomize shader parameters (prefixed with param_) */
		if (obs_property_get_type(prop) == OBS_PROPERTY_FLOAT && strncmp(name, "param_", 6) == 0) {
			double min = obs_property_float_min(prop);
			double max = obs_property_float_max(prop);
			/* Simple random float between min and max */
			double range = max - min;
			double val = min + (range * ((double)rand() / (double)RAND_MAX));
			obs_data_set_double(settings, name, val);
		}
		obs_property_next(&prop);
	}

	obs_source_update(s->context, settings);
	obs_data_release(settings);
	return true;
}

/* Compositing and time use persistent global settings, not shader uniforms.
 * Put them in ONE section to distinguish them from per-shader parameters. */
static void properties_add_compositing_controls(obs_properties_t *group)
{
    obs_property_t *base = obs_properties_add_float_slider(
        group, "compose_base_opacity", "Base Opacity", 0.0, 1.0, 0.01);
    obs_property_set_long_description(base,
        "Opacity of the incoming base video. Standalone sources have no base video.");
    obs_property_t *effect = obs_properties_add_float_slider(
        group, "compose_effect_opacity", "Effect Opacity", 0.0, 1.0, 0.01);
    obs_property_set_long_description(effect,
        "Opacity of the shader output, independently of Base Opacity.");
    obs_property_t *mode = obs_properties_add_list(
        group, "compose_blend_mode", "Effect Blend Mode",
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(mode, "Normal", 0);
    obs_property_list_add_int(mode, "Add", 1);
    obs_property_list_add_int(mode, "Screen", 2);
    obs_property_list_add_int(mode, "Multiply", 3);
    obs_property_list_add_int(mode, "Overlay", 4);
    obs_property_list_add_int(mode, "Difference", 5);
    obs_property_set_long_description(mode,
        "Blend the effect with the incoming base video. A source has no base video.");
}

void properties_add_global_overrides(obs_properties_t *props, struct hypershader_data *s)
{
    obs_properties_t *global = obs_properties_create();
    obs_property_t *speed = obs_properties_add_float(
        global, "speed", "Global Animation Speed", 0.0, 10.0, 0.1);
    obs_property_set_long_description(speed,
        "Time multiplier for this entire shader (including its own local speed).\n"
        "0.0 pauses time, 1.0 is realtime, and higher values accelerate it.");
    obs_properties_add_button2(global, "reset_time", obs_module_text("ResetShaderTimer"),
                               reset_time_clicked, s);
    properties_add_compositing_controls(global);
    obs_properties_add_group(props, "global_overrides_group", "Global Overrides",
                             OBS_GROUP_NORMAL, global);
}

/* OBS's OBS_TEXT_INFO QLabel can interpret markup. Effect headers come from
 * third parties; escape markup instead of allowing a downloaded shader to
 * inject rich-text URLs or styling into the native settings panel. */
static void shader_ui_escape_text(char *out, size_t size, const char *input)
{
    if (!out || !size)
        return;
    size_t used = 0;
    for (const char *p = input ? input : ""; *p && used < size - 1; ++p) {
        const char *encoded = NULL;
        switch (*p) {
        case '&': encoded = "&amp;"; break;
        case '<': encoded = "&lt;"; break;
        case '>': encoded = "&gt;"; break;
        default: break;
        }
        if (encoded) {
            size_t n = strlen(encoded);
            if (n >= size - used)
                break;
            memcpy(out + used, encoded, n);
            used += n;
        } else {
            out[used++] = *p;
        }
    }
    out[used] = '\0';
}

static void shader_ui_add_readonly_info(obs_properties_t *props,
                                        const char *key, const char *label,
                                        const char *body)
{
    if (!body || !*body)
        return;
    obs_property_t *field = obs_properties_add_text(props, key, label, OBS_TEXT_INFO);
    if (!field)
        return;
    char escaped[4096];
    shader_ui_escape_text(escaped, sizeof(escaped), body);
    obs_property_set_long_description(field, escaped);
    obs_property_text_set_info_type(field, OBS_TEXT_INFO_NORMAL);
    obs_property_text_set_info_word_wrap(field, true);
}

/* Shader notes are CREATOR-PROVIDED DOCUMENTATION, not user-editable
 * fields. Attribution remains immutable and is read from the effect file.
 * Existing shader_notes_* settings are never read, written, or deleted;
 * scenes saved using the earlier UI retain that data for possible recovery. */
static void properties_add_shader_information(obs_properties_t *props,
                                              const char *shader_file)
{
    if (!props || !shader_file || !*shader_file)
        return;

    char *resolved = hs_resolve_data_file(shader_file);
    struct hs_shader_info info;
    hs_shader_info_read(resolved ? resolved : shader_file, &info);
    bfree(resolved);

    obs_properties_t *notes = obs_properties_create();
    shader_ui_add_readonly_info(notes, "shader_about", "Description",
                                info.description[0] ? info.description :
                                "No description provided in this shader's header.");
    if (info.notes[0])
        shader_ui_add_readonly_info(notes, "shader_details", "Technical Notes", info.notes);

    obs_properties_t *credits = obs_properties_create();
    shader_ui_add_readonly_info(credits, "shader_author", "Author", info.author);
    shader_ui_add_readonly_info(credits, "shader_credit", "Credits",
                                info.credit[0] ? info.credit :
                                "Not specified in this shader's header.");
    shader_ui_add_readonly_info(credits, "shader_source", "Original Source", info.source);
    shader_ui_add_readonly_info(credits, "shader_license", "License", info.license);

    obs_properties_t *details = obs_properties_create();
    obs_properties_add_group(details, "shader_notes_group", "Notes",
                             OBS_GROUP_NORMAL, notes);
    obs_properties_add_group(details, "shader_credits_group", "Credits",
                             OBS_GROUP_NORMAL, credits);
    obs_properties_add_group(props, "shader_information_group", "Shader Information",
                             OBS_GROUP_NORMAL, details);
}

/* Native OBS list properties are flat, so scene paths communicate hierarchy
 * without adding selectable headings that are not sources. Each UUID is listed once. */
struct hs_source_picker {
	obs_property_t *property;
	obs_source_t *self;
	uint32_t required_flags;
	DARRAY(obs_scene_t *) visited;
};

static bool picker_has_value(obs_property_t *property, const char *value)
{
	for (size_t i = 0; i < obs_property_list_item_count(property); ++i)
		if (strcmp(obs_property_list_item_string(property, i), value) == 0)
			return true;
	return false;
}

static void picker_add_source(struct hs_source_picker *picker, obs_source_t *source, const char *label)
{
	if (!source || source == picker->self || obs_source_removed(source) ||
	    !(obs_source_get_output_flags(source) & picker->required_flags))
		return;
	const char *uuid = obs_source_get_uuid(source);
	const char *name = obs_source_get_name(source);
	if (!uuid || !*uuid || !name || !*name)
		return;

	struct dstr value = {0};
	dstr_cat(&value, HS_SOURCE_SELECTION_PREFIX);
	dstr_cat(&value, uuid);
	if (!picker_has_value(picker->property, value.array))
		obs_property_list_add_string(picker->property, label, value.array);
	dstr_free(&value);
}

static void picker_walk_scene(struct hs_source_picker *picker, obs_scene_t *scene, const char *path);
struct hs_picker_frame {
	struct hs_source_picker *picker;
	const char *path;
};

static bool picker_walk_item(obs_scene_t *scene, obs_sceneitem_t *item, void *opaque)
{
	UNUSED_PARAMETER(scene);
	struct hs_picker_frame *frame = opaque;
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source || !obs_source_get_name(source))
		return true;
	struct dstr label = {0};
	dstr_copy(&label, frame->path);
	dstr_cat(&label, " / ");
	dstr_cat(&label, obs_source_get_name(source));
	picker_add_source(frame->picker, source, label.array);
	picker_walk_scene(frame->picker, obs_group_or_scene_from_source(source), label.array);
	dstr_free(&label);
	return true;
}

static void picker_walk_scene(struct hs_source_picker *picker, obs_scene_t *scene, const char *path)
{
	if (!scene)
		return;
	for (size_t i = 0; i < picker->visited.num; ++i)
		if (picker->visited.array[i] == scene)
			return;
	da_push_back(picker->visited, &scene);
	struct hs_picker_frame frame = {picker, path};
	obs_scene_enum_items(scene, picker_walk_item, &frame);
}

static bool picker_scene(void *opaque, obs_source_t *source)
{
	struct hs_source_picker *picker = opaque;
	const char *name = obs_source_get_name(source);
	if (!name || !*name)
		return true;
	picker_add_source(picker, source, name);
	picker_walk_scene(picker, obs_group_or_scene_from_source(source), name);
	return true;
}

static bool picker_unplaced(void *opaque, obs_source_t *source)
{
	picker_add_source(opaque, source, obs_source_get_name(source));
	return true;
}

static void populate_source_picker(obs_property_t *property, struct hypershader_data *s,
				   const char *setting, const char *legacy_uuid_key, uint32_t flags)
{
	struct hs_source_picker picker = {0};
	picker.property = property;
	picker.self = s ? s->context : NULL;
	picker.required_flags = flags;
	obs_enum_scenes(picker_scene, &picker);
	obs_enum_sources(picker_unplaced, &picker);
	da_free(picker.visited);

	/* Keep the old exact saved string selected even if it was renamed,
	 * imported, or removed. Changing the picker is an explicit retarget. */
	obs_data_t *settings = picker.self ? obs_source_get_settings(picker.self) : NULL;
	if (settings) {
		const char *saved = obs_data_get_string(settings, setting);
		if (saved && *saved && !picker_has_value(property, saved)) {
			const char *uuid = legacy_uuid_key ? obs_data_get_string(settings, legacy_uuid_key) : NULL;
			obs_source_t *source = hs_source_selection_resolve(saved, uuid);
			struct dstr label = {0};
			if (source) {
				dstr_copy(&label, obs_source_get_name(source));
				dstr_cat(&label, " (saved selection)");
				obs_source_release(source);
			} else {
				dstr_copy(&label, "Unavailable saved source: ");
				dstr_cat(&label, saved);
			}
			obs_property_list_add_string(property, label.array, saved);
			dstr_free(&label);
		}
		obs_data_release(settings);
	}
}

void properties_add_input_settings(obs_properties_t *props, struct hypershader_data *s)
{
	/* Channel samplers (hs_input0..hs_input3) — grouped per-channel */
	for (int i = 0; i < CHANNEL_COUNT; i++) {

        /* Referenced textures are optional user-supplied inputs, not
         * necessarily active sources; never mark None as "Active". */
        bool used = false;

		char key_group[64];
		char key_type[64];
		char key_file[64];
		char key_src[64];
		char label[64];
		snprintf(key_group, sizeof(key_group), "channel%d_group", i);
		snprintf(key_type, sizeof(key_type), "channel%d_type", i);
		snprintf(key_file, sizeof(key_file), "channel%d_file", i);
		snprintf(key_src, sizeof(key_src), "channel%d_source", i);
		snprintf(label, sizeof(label), "Texture Input %d", i);
        if (s) {
            pthread_mutex_lock(&s->mutex);
            used = s->channel_required[i];
            if (s->metadata_rc) {
                for (size_t m = 0; m < s->metadata_rc->meta.num; ++m) {
                    const struct shader_param_meta *meta = &s->metadata_rc->meta.array[m];
                    if (meta->input_slot == i + 1) {
                        snprintf(label, sizeof(label), "%s", meta->label ? meta->label : meta->name);
                        break;
                    }
                }
            }
            pthread_mutex_unlock(&s->mutex);
        }


		/* Create a sub-properties container and add it as a collapsible group */
		obs_properties_t *ch_props = obs_properties_create();
		obs_property_t *ptype = obs_properties_add_list(ch_props, key_type, obs_module_text("InputType"),
								OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		obs_property_list_add_int(ptype, obs_module_text("None"), 0);
		obs_property_list_add_int(ptype, obs_module_text("ImageFile"), 1);
		obs_property_list_add_int(ptype, obs_module_text("OBSSourceByName"), 2);
		obs_property_list_add_int(ptype, obs_module_text("AudioSpectrum"), 3);

		obs_properties_add_path(ch_props, key_file, "Image File Path", OBS_PATH_FILE,
					"Image files (*.png *.jpg *.jpeg *.bmp *.tga *.webp *.gif);;All files (*.*)", NULL);

		/* Source picker inside group */
		obs_property_t *psrc = obs_properties_add_list(ch_props, key_src, obs_module_text("SourceName"),
							       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_list_add_string(psrc, obs_module_text("NoneOption"), "");
		char key_uuid[64];
		snprintf(key_uuid, sizeof(key_uuid), "channel%d_uuid", i);
		populate_source_picker(psrc, s, key_src, key_uuid, OBS_SOURCE_VIDEO);

		/* Indent or group visibly */
        obs_property_t *group = obs_properties_add_group(
            props, key_group, label, OBS_GROUP_NORMAL, ch_props);
        obs_property_set_long_description(group,
            "Optional shader texture input. Select an image,"
            " OBS video source or audio spectrum, or leave None for no texture."
            " Live video inputs must already be showing; hidden, removed and"
            " self-referential sources are unavailable. Live capture is supported"
            " by filters and standalone sources, not transitions."
            " This is separate from the base video/compositing controls.");
        obs_property_set_visible(group, used);
		obs_property_set_visible(obs_properties_get(ch_props, key_file), used && s && s->channel_types[i] == 1);
		obs_property_set_visible(obs_properties_get(ch_props, key_src), used && s && s->channel_types[i] == 2);

		obs_property_set_modified_callback2(ptype, channel_type_changed, s);
	}

}

void properties_add_audio_settings(obs_properties_t *props, struct hypershader_data *s)
{
	obs_properties_add_bool(props, "enable_sound", obs_module_text("EnableSound"));
	obs_property_set_long_description(obs_properties_get(props, "enable_sound"),
						"Enable audio input for this shader. Select an audio source below; the source-mode audio presets can also use the OBS mix.");

	obs_property_t *audio_source = obs_properties_add_list(props, "audio_source", obs_module_text("AudioSource"),
								 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(audio_source, obs_module_text("NoneOption"), "");
	populate_source_picker(audio_source, s, "audio_source", NULL, OBS_SOURCE_AUDIO);

}

static bool sight_provider_changed(void *priv, obs_properties_t *props, obs_property_t *p,
                                   obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	sync_input_visibility(priv, props, settings);
	return true;
}

void properties_add_sight_settings(obs_properties_t *props, struct hypershader_data *s)
{
	obs_property_t *sight_enable = obs_properties_add_bool(
		props, "sight_use_tracking", "Use Sight");
	obs_property_set_modified_callback2(sight_enable, sight_provider_changed, s);
	obs_property_set_long_description(sight_enable,
		"Opt in to Face/Body/Hand Motion data published by Latent Lab Sight in the same OBS process. "
		"Requires Sight's producer filter on the chosen channel. Add one enabled Subject Cutout in Original source diagnostic view and/or Spatial Motion in Original source mode on this source. No second MediaPipe inference. "
		"Text Obfuscation Original source and Depth Camera Original source (share depth) can also share data. Keep analysis-only Sight filters consecutively upstream of HyperShader; transformed or unknown filters block deeper data. One enabled producer of each type. Flow expires after 500 ms, depth after 1500 ms, text after 2000 ms, other snapshots after 750 ms. Changing this setting does not modify Sight.");
	obs_data_t *sight_settings = s && s->context ? obs_source_get_settings(s->context) : NULL;
	const char *saved_channel = sight_settings ? obs_data_get_string(sight_settings, "sight_tracking_channel") : "face";
	obs_property_t *sight_channel = obs_properties_add_list(props, "sight_tracking_channel",
		"Sight Channel", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	if (!hs_sight_populate_channels(sight_channel, saved_channel)) {
		/* Older/missing producer module: retain old manual entry behavior. */
		obs_properties_remove_by_name(props, "sight_tracking_channel");
		sight_channel = obs_properties_add_text(props, "sight_tracking_channel",
			"Sight Channel", OBS_TEXT_DEFAULT);
	}
	if (sight_settings) obs_data_release(sight_settings);
	obs_property_set_long_description(sight_channel,
		"Choose a compatible configured Sight channel. Reopen properties to update the inventory. Older Sight modules accept a channel name manually. "
		"Coordinates refer to the Sight producer's camera, not automatically to this source.");
}

void properties_add_advanced_settings(obs_properties_t *props, struct hypershader_data *s)
{
	UNUSED_PARAMETER(s);
	obs_property_t *mp_graph_prop = obs_properties_add_list(props, "mp_controlnet_model", "MediaPipe Graph",
								OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(mp_graph_prop, "Face Region Mask (approximate)", MP_GRAPH_SELFIE_SEGMENTATION);
	obs_property_list_add_int(mp_graph_prop, "Face Mesh", MP_GRAPH_FACE_MESH);
	obs_property_list_add_int(mp_graph_prop, "Hand Tracking (not implemented)", MP_GRAPH_HAND_TRACKING);
	obs_property_list_add_int(mp_graph_prop, "Pose Estimation (not implemented)", MP_GRAPH_POSE_ESTIMATION);
	obs_property_list_add_int(mp_graph_prop, "Depth Estimation (not implemented)", MP_GRAPH_DEPTH_ESTIMATION);
	obs_property_list_add_int(mp_graph_prop, "Canny Graph (not implemented)", MP_GRAPH_CANNY);
	obs_property_list_add_int(mp_graph_prop, "Bystander Face Mask (no automatic blur)", MP_GRAPH_PRIVACY);
	for (size_t i = 2; i <= 5; ++i)
		obs_property_list_item_disable(mp_graph_prop, i, true);
	obs_property_set_long_description(
		mp_graph_prop,
		"Face Mesh uses ONNX inference. Face Region and Bystander Mask are approximate face-box masks, not semantic segmentation or automatic video blur. Other listed modes are not yet implemented.");

}

struct example_desc {
	const char *id;
	const char *label;
	const char *file;
	const char *desc;
};

static pthread_mutex_t global_metadata_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct example_desc *dynamic_examples = NULL;
static size_t dynamic_examples_count = 0;
static char **user_preset_names = NULL;
static char **user_preset_paths = NULL;
static char **user_preset_tags = NULL;
static int user_preset_count = 0;
static bool presets_loaded = false;

/* Keep the four historical picker IDs for saved scenes; any other installed
 * shader gets a stable path-derived ID, independent of directory ordering. */
static int64_t shader_example_preset_id(const char *id)
{
	if (id && strcmp(id, "root/pframe_drift.effect") == 0)
		return 0;
	if (id && strcmp(id, "root/realworld_focus_bulge.effect") == 0)
		return -101;
	if (id && strcmp(id, "root/realworld_focus_noise.effect") == 0)
		return -102;
	if (id && strcmp(id, "root/realworld_rgb_shift.effect") == 0)
		return -103;
	/* FNV-1a over the installed shader's relative path. Reserve small
	 * negative values for legacy presets and positive values for user presets.
	 * All 63 non-sign bits are preserved for collision resistance. */
	uint64_t hash = UINT64_C(14695981039346656037);
	for (const unsigned char *c = (const unsigned char *)(id ? id : ""); *c; c++) {
		hash ^= *c;
		hash *= UINT64_C(1099511628211);
	}
	return -(int64_t)((hash & UINT64_C(0x7fffffffffffffff)) | UINT64_C(0x1000));
}

static int compare_verified_examples(const void *lhs, const void *rhs)
{
	const struct example_desc *a = lhs, *b = rhs;
	/* Keep the original four at the top, in the same order. */
	const char *legacy[] = {"root/pframe_drift.effect", "root/realworld_focus_bulge.effect",
			       "root/realworld_focus_noise.effect", "root/realworld_rgb_shift.effect"};
	int ai = 4, bi = 4;
	for (int i = 0; i < 4; i++) {
		if (strcmp(a->id, legacy[i]) == 0)
			ai = i;
		if (strcmp(b->id, legacy[i]) == 0)
			bi = i;
	}
	if (ai != bi)
		return ai < bi ? -1 : 1;
	return strcmp(a->id, b->id);
}

static char *get_user_presets_dir(void)
{
	obs_module_t *module = obs_get_module("hypershader");
	if (!module) {
		/* Fallback for local testing or if module not found by name */
		const char *xdg = getenv("XDG_CONFIG_HOME");
		char buf[PATH_MAX];
		if (!xdg || xdg[0] == '\0') {
			const char *home = getenv("HOME");
			if (!home)
				return NULL;
			snprintf(buf, sizeof(buf), "%s/.config/obs-studio/plugins/hypershader/presets", home);
		} else {
			snprintf(buf, sizeof(buf), "%s/obs-studio/plugins/hypershader/presets", xdg);
		}
		return bstrdup(buf);
	}
	char *path = obs_module_get_config_path(module, "presets");
	return path; /* caller frees */
}

/* Ensure a directory exists (simple mkdir -p) */
static void ensure_dir_exists(const char *dir)
{
	if (!dir)
		return;
	char tmp[PATH_MAX];
	strncpy(tmp, dir, sizeof(tmp));
	tmp[sizeof(tmp) - 1] = '\0';
	for (char *p = tmp + 1; *p; ++p) {
		if (*p == '/') {
			*p = '\0';
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
	mkdir(tmp, 0755);
}

/* Custom File stays at its pre-expansion serialized value. */
static int hyper_ui_custom_preset_index(void)
{
	int count = 0;
	pthread_mutex_lock(&global_metadata_mutex);
	if (!dynamic_examples_count)
		scan_shaders_dir_internal();
	if (!presets_loaded)
		load_user_presets_internal();
	count = 1 + user_preset_count;
	pthread_mutex_unlock(&global_metadata_mutex);
	return count;
}

/* Old scenes had zero verified built-ins: user preset IDs started at 0.
 * Preserve the saved shader_file rather than interpreting old IDs in the
 * new picker. Only write the migration marker after a mapping is recorded. */
void hyper_ui_migrate_legacy_preset(obs_data_t *settings)
{
	if (!settings || obs_data_has_user_value(settings, "preset_schema"))
		return;
	const char *path = obs_data_get_string(settings, "shader_file");
	int64_t mapped = -1;
	if (path && path[0]) {
		pthread_mutex_lock(&global_metadata_mutex);
		if (!dynamic_examples_count)
			scan_shaders_dir_internal();
		if (!presets_loaded)
			load_user_presets_internal();
		mapped = 1 + user_preset_count; /* Custom File preserves unknown paths. */
		for (size_t i = 0; i < dynamic_examples_count; i++) {
			if (dynamic_examples[i].file && strcmp(dynamic_examples[i].file, path) == 0) {
				mapped = shader_example_preset_id(dynamic_examples[i].id);
				break;
			}
		}
		for (int i = 0; i < user_preset_count; i++) {
			if (user_preset_paths[i] && strcmp(user_preset_paths[i], path) == 0) {
				mapped = i + 1;
				break;
			}
		}
		pthread_mutex_unlock(&global_metadata_mutex);
	}
	obs_data_set_int(settings, "preset", mapped);
	obs_data_set_int(settings, "preset_schema", 2);
}

/* Scan the user presets directory and populate in-memory lists */
/* Helper to extract tags from shader file (looks for @tags values) */
static char *extract_tags(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	char line[1024];
	char *tags = NULL;
	int lines_read = 0;
	while (fgets(line, sizeof(line), f) && lines_read < 50) {
		lines_read++;
		char *p = strstr(line, "@tags");
		if (p) {
			char *start = strchr(p, ':');
			if (start) {
				start++;
				while (*start && isspace(*start))
					start++;
				char *end = start + strlen(start) - 1;
				while (end > start && isspace(*end))
					*end-- = '\0';
				if (*start)
					tags = bstrdup(start);
				break;
			}
		}
	}
	fclose(f);
	return tags;
}

static void load_user_presets_internal(void)
{
	/* free previous */
	if (user_preset_names) {
		for (int i = 0; i < user_preset_count; i++) {
			bfree(user_preset_names[i]);
			bfree(user_preset_paths[i]);
			if (user_preset_tags && user_preset_tags[i])
				bfree(user_preset_tags[i]);
		}
		bfree(user_preset_names);
		bfree(user_preset_paths);
		if (user_preset_tags)
			bfree(user_preset_tags);
		user_preset_names = NULL;
		user_preset_paths = NULL;
		user_preset_tags = NULL;
		user_preset_count = 0;
	}

	char *dir = get_user_presets_dir();
	if (!dir)
		return;
	ensure_dir_exists(dir);

	DIR *d = opendir(dir);
	if (!d) {
		bfree(dir);
		return;
	}

	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		const char *name = ent->d_name;
		size_t n = strlen(name);
		if (n > 7 && strcmp(name + n - 7, ".effect") == 0) {
            char *full = bmalloc(strlen(dir) + 1 + n + 1);
            snprintf(full, strlen(dir) + 1 + n + 1, "%s/%s", dir, name);
            char display[PATH_MAX];
            if (!hs_shader_display_label_from_file(display, sizeof(display), name, full)) {
                bfree(full);
                continue;
            }
            char *label = bstrdup(display);

			char *tags = extract_tags(full);

			user_preset_names = brealloc(user_preset_names, sizeof(char *) * (user_preset_count + 1));
			user_preset_paths = brealloc(user_preset_paths, sizeof(char *) * (user_preset_count + 1));
			user_preset_tags = brealloc(user_preset_tags, sizeof(char *) * (user_preset_count + 1));

			user_preset_names[user_preset_count] = label;
			user_preset_paths[user_preset_count] = full;
			user_preset_tags[user_preset_count] = tags; /* can be NULL */
			user_preset_count++;
		}
	}
	closedir(d);
	bfree(dir);
	presets_loaded = true;
}

void load_user_presets(void)
{
	pthread_mutex_lock(&global_metadata_mutex);
	load_user_presets_internal();
	pthread_mutex_unlock(&global_metadata_mutex);
}

/* Save current shader file into user presets directory (auto-named if needed) */
static bool save_current_shader_as_preset(struct hypershader_data *s, const char *name)
{
	if (!s)
		return false;
	if (!s->shader_path || !s->shader_path[0]) {
		blog(LOG_WARNING, "[HyperShader] Cannot save: current shader has no backing file path");
		return false;
	}

	char *dir = get_user_presets_dir();
	if (!dir)
		return false;
	ensure_dir_exists(dir);

	char safe[256];
	if (name && name[0]) {
		/* make a simple safe filename (no slashes) */
		size_t i2 = 0;
		for (size_t i = 0; i < strlen(name) && i2 + 1 < sizeof(safe); ++i) {
			char c = name[i];
			if (c == '/' || c == '\\')
				safe[i2++] = '_';
			else
				safe[i2++] = c;
		}
		safe[i2] = '\0';
	} else {
		time_t t = time(NULL);
		snprintf(safe, sizeof(safe), "preset_%ld", (long)t);
	}

	char dst[PATH_MAX];
	snprintf(dst, sizeof(dst), "%s/%s.effect", dir, safe);

	/* copy file with comprehensive error handling */
	FILE *fin = fopen(s->shader_path, "rb");
	if (!fin) {
		blog(LOG_ERROR, "[HyperShader] ┌─ Save failed");
		blog(LOG_ERROR, "[HyperShader] ├─ Source : %s", s->shader_path);
		blog(LOG_ERROR, "[HyperShader] └─ Detail : %s", strerror(errno));
		bfree(dir);
		return false;
	}

	FILE *fout = fopen(dst, "wb");
	if (!fout) {
		blog(LOG_ERROR, "[HyperShader] ┌─ Save failed");
		blog(LOG_ERROR, "[HyperShader] ├─ Target : %s", dst);
		blog(LOG_ERROR, "[HyperShader] └─ Detail : %s", strerror(errno));
		fclose(fin);
		bfree(dir);
		return false;
	}

	char buf[4096];
	size_t r;
	bool success = true;

	while ((r = fread(buf, 1, sizeof(buf), fin)) > 0) {
		size_t written = fwrite(buf, 1, r, fout);
		if (written != r) {
			blog(LOG_ERROR, "[HyperShader] Write failed: expected %zu, wrote %zu (%s)", r, written,
			     strerror(errno));
			success = false;
			break;
		}
	}

	if (ferror(fin)) {
		blog(LOG_ERROR, "[HyperShader] Read error: %s", strerror(errno));
		success = false;
	}

	fclose(fin);
	if (fclose(fout) != 0) {
		blog(LOG_ERROR, "[HyperShader] Failed to close destination: %s", strerror(errno));
		success = false;
	}

	if (!success) {
		bfree(dir);
		return false;
	}

	bfree(dir);
	load_user_presets();
	blog(LOG_INFO, "[HyperShader] Saved current shader as preset: %s", dst);
	return true;
}

static bool preset_changed(struct hypershader_data *s, obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	/* Note: 's' added to signature to support sync_ui_visibility */
	UNUSED_PARAMETER(p);
	obs_property_t *file_prop = obs_properties_get(props, "shader_file");

	if (!settings) {
		if (file_prop)
			obs_property_set_visible(file_prop, false);
		return true;
	}

	pthread_mutex_lock(&global_metadata_mutex);

	/* reload user presets at change time so new saves appear immediately */
	if (!presets_loaded)
		load_user_presets_internal();

	int64_t preset = obs_data_get_int(settings, "preset");
	/* Explicit changes are already using the current picker schema. */
	obs_data_set_int(settings, "preset_schema", 2);

	/* Ensure examples are scanned */
	if (!dynamic_examples_count)
		scan_shaders_dir_internal();

	int custom_index = 1 + user_preset_count;

	/* Visibility Logic Delegated to Sync */
	sync_ui_visibility(s, props, settings);

	char *path = NULL;

	for (size_t i = 0; i < dynamic_examples_count; ++i) {
		if (shader_example_preset_id(dynamic_examples[i].id) == preset && dynamic_examples[i].file) {
			path = bstrdup(dynamic_examples[i].file);
			break;
		}
	}
	if (!path && preset >= 1 && preset < custom_index) {
		/* User preset values retain the legacy 1-based offsets. */
		const char *upath = user_preset_paths[preset - 1];
		if (upath)
			path = bstrdup(upath);
	}

	if (path) {
		obs_data_set_string(settings, "shader_file", path);
		bfree(path);
	} else if (preset == -1) {
		obs_data_set_string(settings, "shader_file", "");
	}

	pthread_mutex_unlock(&global_metadata_mutex);

	return true;
}

/* Deferred property refresh callback - scheduled on UI thread to avoid re-entrancy */
static void deferred_properties_refresh(void *param)
{
	obs_source_t *source = param;
	if (source) {
		blog(LOG_INFO, "[HyperShader] Deferred property refresh executing");
		obs_source_update_properties(source);
	}
}

/* Helper that does the actual work for preset_changed */
static bool preset_changed_impl(struct hypershader_data *s, obs_properties_t *props, obs_property_t *p,
				obs_data_t *settings)
{
	return preset_changed(s, props, p, settings);
}

/* OBS property dropdowns cannot host native cascading QMenus. Use a
 * read-only category filter above the unchanged preset ID selector instead.
 * Catalog data is immutable while global_metadata_mutex is held. */
static bool shader_category_available(const char *category)
{
    if (!category || !*category)
        return false;
    if (strcmp(category, "all") == 0)
        return true;
    for (size_t i = 0; i < dynamic_examples_count; ++i) {
        if (hs_shader_category_matches(category, dynamic_examples[i].id,
                                       dynamic_examples[i].label, false))
            return true;
    }
    for (int i = 0; i < user_preset_count; ++i) {
        if (hs_shader_category_matches(category, user_preset_paths[i],
                                       user_preset_names[i], true))
            return true;
    }
    return false;
}

static void shader_category_append(obs_property_t *list, const char *id)
{
    if (!list || !shader_category_available(id))
        return;
    for (size_t i = 0; i < obs_property_list_item_count(list); ++i) {
        if (strcmp(obs_property_list_item_string(list, i), id) == 0)
            return;
    }
    char label[160];
    if (hs_shader_category_display_name(id, label, sizeof(label)))
        obs_property_list_add_string(list, label, id);
}

/* Type-appropriate navigation before All Shaders; discovered folders create
 * their own categories automatically, including new third-party collections. */
static void shader_category_fill_options(obs_property_t *list, const char *preferred)
{
    shader_category_append(list, preferred);
    shader_category_append(list, "all");
    shader_category_append(list, "source");
    shader_category_append(list, "filter");
    shader_category_append(list, "transition");
    for (size_t i = 0; i < dynamic_examples_count; ++i) {
        char id[160];
        if (hs_shader_category_for(dynamic_examples[i].id,
                                   dynamic_examples[i].label, false,
                                   id, sizeof(id)) &&
            strncmp(id, "collection:", 11) == 0)
            shader_category_append(list, id);
    }
    shader_category_append(list, "user");
}

static int64_t shader_ui_preset_for(const char *path)
{
    if (!path || !*path) return -1;
    for (size_t i = 0; i < dynamic_examples_count; ++i)
        if (dynamic_examples[i].file &&
            strcmp(path, dynamic_examples[i].file) == 0)
            return shader_example_preset_id(dynamic_examples[i].id);
    for (int i = 0; i < user_preset_count; i++)
        if (user_preset_paths[i] && strcmp(path, user_preset_paths[i]) == 0)
            return 1 + i;
    return 1 + user_preset_count; /* current Custom File */
}

static void shader_picker_fill(obs_property_t *picker,
                               const char *category, obs_data_t *settings)
{
    if (!picker)
        return;
    obs_property_list_clear(picker);
    obs_property_list_add_int(picker, "Choose a shader...", -1);
    const char *selected_path = settings
        ? obs_data_get_string(settings, "shader_file") : NULL;
    const int64_t selected_id = shader_ui_preset_for(selected_path);
    bool selected_present = false;
    for (size_t i = 0; i < dynamic_examples_count; ++i) {
        const struct example_desc *ex = &dynamic_examples[i];
        if (!hs_shader_category_matches(category, ex->id, ex->label, false))
            continue;
        const int64_t id = shader_example_preset_id(ex->id);
        char label[PATH_MAX];
        if (!hs_shader_category_item_label(category, ex->label, label, sizeof(label)))
            continue;
        obs_property_list_add_int(picker, label, (long long)id);
        if (id == selected_id)
            selected_present = true;
    }
    for (int i = 0; i < user_preset_count; ++i) {
        if (!hs_shader_category_matches(category, user_preset_paths[i],
                                       user_preset_names[i], true))
            continue;
        char label[PATH_MAX];
        if (!hs_shader_category_item_label(category, user_preset_names[i],
                                           label, sizeof(label)))
            continue;
        obs_property_list_add_int(picker, label, (long long)(1 + i));
        if (selected_id == 1 + i)
            selected_present = true;
    }
    /* Navigating a category must NOT change the shader already rendering.
     * Keep its real ID in the list if it belongs outside the category. */
    if (!selected_present && selected_path && *selected_path &&
        selected_id != 1 + user_preset_count) {
        const char *original_label = NULL;
        for (size_t i = 0; i < dynamic_examples_count; i++) {
            if (shader_example_preset_id(dynamic_examples[i].id) == selected_id) {
                original_label = dynamic_examples[i].label;
                break;
            }
        }
        if (!original_label && selected_id > 0 &&
            selected_id <= user_preset_count)
            original_label = user_preset_names[selected_id - 1];
        if (original_label) {
            char shown[PATH_MAX + 24];
            snprintf(shown, sizeof(shown), "Current / %s", original_label);
            obs_property_list_insert_int(picker, 1, shown, (long long)selected_id);
        }
    }
    obs_property_list_add_int(picker, obs_module_text("CustomFile"),
                              (long long)(1 + user_preset_count));
}

static bool shader_category_changed2(void *priv, obs_properties_t *props,
                                      obs_property_t *p, obs_data_t *settings)
{
    (void)p;
    struct hypershader_data *s = priv;
    bool release = false;
    if (!settings && s && s->context) {
        settings = obs_source_get_settings(s->context);
        release = settings != NULL;
    }
    if (!settings)
        return false;

    pthread_mutex_lock(&global_metadata_mutex);
    scan_shaders_dir_internal();
    if (!presets_loaded)
        load_user_presets_internal();
    const char *category = obs_data_get_string(settings, "shader_category");
    if (!shader_category_available(category)) {
        category = "all";
        obs_data_set_string(settings, "shader_category", category);
    }
    obs_property_t *categories = obs_properties_get(props, "shader_category");
    if (categories) {
        obs_property_list_clear(categories);
        shader_category_fill_options(categories, category);
    }
    shader_picker_fill(obs_properties_get(props, "preset"), category, settings);
    pthread_mutex_unlock(&global_metadata_mutex);
    if (release)
        obs_data_release(settings);
    return true; /* refresh the two dropdowns without reloading the shader */
}

static bool preset_changed2(void *priv, obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	struct hypershader_data *s = priv;

	/* Guard against re-entrant calls during property construction */
	if (s && atomic_load_explicit(&s->properties_building, memory_order_seq_cst)) {
		blog(LOG_DEBUG, "[HyperShader] preset_changed2: blocked during property construction");
		return false;
	}

	/* Path 1: settings is NULL - called from filter_properties context */
	if (!settings) {
		if (!s || !s->context) {
			blog(LOG_WARNING, "[HyperShader] preset_changed2: no settings and no valid context");
			return false;
		}
		settings = obs_source_get_settings(s->context);
		if (!settings) {
			blog(LOG_WARNING, "[HyperShader] preset_changed2: failed to get settings from context");
			return false;
		}

		preset_changed_impl(s, props, p, settings);

		/* FIX: Do NOT call obs_source_update() here.
		   Returning true is sufficient for the properties window to refresh.
		   If the shader path changed, filter_update will be called by OBS. */

		obs_data_release(settings);
		return true;
	}

	/* Path 2: Normal callback - settings provided by OBS */
	preset_changed_impl(s, props, p, settings);

	if (s && s->context) {
		/* If a channel's "type" was changed, toggle visibility of file/source fields for that channel */
		if (p && strstr(obs_property_name(p), "_type")) {
			int ch = -1;
			if (sscanf(obs_property_name(p), "channel%d_type", &ch) == 1 && ch >= 0 && ch < CHANNEL_COUNT) {
				char key_file[32];
				char key_src[32];
				snprintf(key_file, sizeof(key_file), "channel%d_file", ch);
				snprintf(key_src, sizeof(key_src), "channel%d_source", ch);
				int mode = (int)obs_data_get_int(settings, obs_property_name(p));
				obs_property_set_visible(obs_properties_get(props, key_file), mode == 1);
				obs_property_set_visible(obs_properties_get(props, key_src), mode == 2);
			}
		}

		/* Store old path BEFORE filter_update changes it */
		char *old_path = s->shader_path ? bstrdup(s->shader_path) : NULL;

		/* Update the filter (this loads new shader if path changed) */
		obs_source_update(s->context, settings);

		/* Check if shader actually changed */
		bool shader_changed = false;
		if (old_path && s->shader_path) {
			shader_changed = (strcmp(old_path, s->shader_path) != 0);
		} else if (!old_path && s->shader_path) {
			shader_changed = true;
		} else if (old_path && !s->shader_path) {
			shader_changed = true;
		}

		if (old_path)
			bfree(old_path);

		/* NOTE: Property refresh handling
           When shader changes, OBS needs to rebuild the properties UI to show new
           shader-specific parameters. We cannot call obs_source_update_properties()
           from within a property callback (causes use-after-free). Instead, we set
           a flag that video_render checks and triggers the refresh from a safe context.
           
           The correct approach: set needs_properties_refresh flag!
           video_render will detect this and call obs_source_update_properties safely. */
		if (shader_changed) {
			blog(LOG_INFO, "[HyperShader] Shader changed, scheduling property refresh");
			atomic_store_explicit(&s->needs_properties_refresh, true, memory_order_seq_cst);
		}
	}

	return true;
}

static void set_property_visible_if_present(obs_properties_t *props, const char *name, bool visible)
{
	obs_property_t *property = props ? obs_properties_get(props, name) : NULL;
	if (property)
		obs_property_set_visible(property, visible);
}

/* Centralized UI Visibility Management */
static void sync_input_visibility(struct hypershader_data *s, obs_properties_t *props, obs_data_t *settings)
{
	if (!settings || !props)
		return;
	bool audio_required = false;
	bool sight_required = false;
	bool channels[CHANNEL_COUNT] = {0};
	bool mediapipe_enabled = false;
	bool mediapipe_has_modes = false;
	if (s) {
		pthread_mutex_lock(&s->mutex);
		if (s->metadata_rc) {
			audio_required = s->metadata_rc->capabilities.audio_required;
			sight_required = s->metadata_rc->capabilities.sight_required ||
				s->metadata_rc->capabilities.shared_analysis_required;
			mediapipe_enabled = s->metadata_rc->capabilities.mediapipe_enabled;
			mediapipe_has_modes = s->metadata_rc->capabilities.mediapipe_has_modes;
		}
		memcpy(channels, s->channel_required, sizeof(channels));
		pthread_mutex_unlock(&s->mutex);
	}
	for (int i = 0; i < CHANNEL_COUNT; i++) {
		char key[64];
		snprintf(key, sizeof(key), "channel%d_group", i);
		bool channel_needed = channels[i] &&
			(!s || s->kind != HYPER_KIND_TRANSITION || i >= 2);
		set_property_visible_if_present(props, key, channel_needed);

		char type_key[64];
		snprintf(type_key, sizeof(type_key), "channel%d_type", i);
		int ch_type = (int)obs_data_get_int(settings, type_key);
		if (channel_needed && ch_type == 3)
			audio_required = true;
		char file_key[64], src_key[64];
		snprintf(file_key, sizeof(file_key), "channel%d_file", i);
		snprintf(src_key, sizeof(src_key), "channel%d_source", i);
        obs_property_t *channel_group = obs_properties_get(props, key);
        obs_properties_t *inputs = channel_group
            ? obs_property_group_content(channel_group) : NULL;
        set_property_visible_if_present(inputs, file_key, channel_needed && ch_type == 1);
        set_property_visible_if_present(inputs, src_key, channel_needed && ch_type == 2);
	}

	/* User modulation can request native signals even if the effect did not
	 * declare them itself. Never silently enable capture or select a source. */
	const char *mod_shader = obs_data_get_string(settings, "perf_mod_shader");
	const char *selected_shader = obs_data_get_string(settings, "shader_file");
	if (mod_shader && *mod_shader && selected_shader && strcmp(mod_shader, selected_shader) == 0 &&
	    *obs_data_get_string(settings, "perf_mod_param")) {
		int driver = (int)obs_data_get_int(settings, "perf_mod_source");
		if (driver >= 2 && driver <= 5)
			audio_required = true;
		if (driver == 6 || driver == 7)
			sight_required = true;
	}
	set_property_visible_if_present(props, "enable_sound", audio_required);
	set_property_visible_if_present(props, "audio_source", audio_required);
	set_property_visible_if_present(props, "sight_use_tracking", sight_required);
	set_property_visible_if_present(props, "sight_tracking_channel", sight_required);
	set_property_visible_if_present(props, "mp_controlnet_model",
				       !obs_data_get_bool(settings, "sight_use_tracking") &&
				       mediapipe_enabled && mediapipe_has_modes);
}

static void sync_ui_visibility(struct hypershader_data *s, obs_properties_t *props, obs_data_t *settings)
{
	if (!settings || !props)
		return;
	int custom_index = 1 + user_preset_count;
	int64_t current_preset = obs_data_get_int(settings, "preset");
	bool is_custom = (current_preset == custom_index);
	bool audio_required = false;
	if (s) {
		pthread_mutex_lock(&s->mutex);
		if (s->metadata_rc)
			audio_required = s->metadata_rc->capabilities.audio_required;
		pthread_mutex_unlock(&s->mutex);
	}
	set_property_visible_if_present(props, "global_overrides_group", true);
	set_property_visible_if_present(props, "preset", true);
	set_property_visible_if_present(props, "shader_file", is_custom);
	set_property_visible_if_present(props, "shader_params_group", true);

	obs_property_t *prop_ptr = obs_properties_first(props);
	while (prop_ptr) {
		const char *name = obs_property_name(prop_ptr);
		if (name && strncmp(name, "param_", 6) == 0) {
			bool is_audio = false;
			const char *real_name = name + 6;
			if (s) {
				pthread_mutex_lock(&s->mutex);
				if (s->metadata_rc) {
					for (size_t m = 0; m < s->metadata_rc->meta.num; m++) {
						if (s->metadata_rc->meta.array[m].name &&
						    strcmp(s->metadata_rc->meta.array[m].name, real_name) == 0) {
							is_audio = s->metadata_rc->meta.array[m].audio_tag != NULL;
							break;
						}
					}
				}
				pthread_mutex_unlock(&s->mutex);
			}
			obs_property_set_visible(prop_ptr, is_audio ? audio_required : true);
		} else if (name && strncmp(name, "group_", 6) == 0) {
			obs_property_set_visible(prop_ptr, true);
		}
		obs_property_next(&prop_ptr);
	}

	sync_input_visibility(s, props, settings);
}

static const char *get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "Hypershader";
}

/* Refresh button callback — request properties rebuild so lists get repopulated */
bool refresh_sources_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	(void)props;
	(void)property;
	(void)data;
	blog(LOG_INFO, "[HyperShader] Global refresh sources clicked");
	/* Returning true causes properties to be refreshed */
	return true;
}

/* Callback for when the shader file property changes */
bool shader_file_changed(void *priv, obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	(void)priv;
	(void)props;
	(void)p;
	(void)settings;
	blog(LOG_INFO, "[HyperShader] Shader file changed in UI - triggering property refresh");
	return true; /* Rebuild properties to show new params */
}
/* Manual refresh button */
bool refresh_properties_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	(void)props;
	(void)property;
	(void)data;
	blog(LOG_INFO, "[HyperShader] Manual property refresh clicked");
	return true;
}

bool analyze_image_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	(void)props;
	(void)property;
	struct hypershader_data *s = (struct hypershader_data *)data;
	if (s) {
		pthread_mutex_lock(&s->mp_mutex);
		s->mp_analyze_trigger = true;
		pthread_mutex_unlock(&s->mp_mutex);
		blog(LOG_INFO, "[HyperShader] Reference Analysis Triggered");
	}
	return false; /* No UI refresh needed */
}

/* Helper: set a numeric effect parameter using the correct typed setter
 * (int vs float) based on the shader parameter info. This avoids warnings
 * when a shader declares e.g. "uniform int hs_frame_index;" but the host tries to
 * set it with a float setter. */
static void set_param_numeric(gs_eparam_t *param, double val)
{
	if (!param)
		return;
	struct gs_effect_param_info pinfo;
	gs_effect_get_param_info(param, &pinfo);
	if (pinfo.type == GS_SHADER_PARAM_INT) {
		gs_effect_set_int(param, (int)val);
	} else {
		gs_effect_set_float(param, (float)val);
	}
}

/* Helper to apply defaults robustly for known types */
void apply_parameter_default(gs_eparam_t *param, struct shader_param_meta *m)
{
	if (!param || !m || !m->has_default)
		return;

	struct gs_effect_param_info info;
	gs_effect_get_param_info(param, &info);

	/* Handle different types */
	if (info.type == GS_SHADER_PARAM_FLOAT) {
		gs_effect_set_float(param, m->default_val);
	} else if (info.type == GS_SHADER_PARAM_INT) {
		gs_effect_set_int(param, (int)m->default_val);
	} else if (info.type == GS_SHADER_PARAM_BOOL) {
		gs_effect_set_bool(param, (bool)m->default_val);
	} else if (info.type == GS_SHADER_PARAM_VEC4) {
		if (m->default_val_str) {
			struct vec4 v;
			if (sscanf(m->default_val_str, "%f,%f,%f,%f", &v.x, &v.y, &v.z, &v.w) == 4)
				gs_effect_set_vec4(param, &v);
		}
	} else if (info.type == GS_SHADER_PARAM_VEC3) {
		if (m->default_val_str) {
			struct vec3 v;
			if (sscanf(m->default_val_str, "%f,%f,%f", &v.x, &v.y, &v.z) == 3) {
				gs_effect_set_vec3(param, &v);
			}
		}
	}
}

/* Helper: Copy a file to /tmp with its basename for preview */
static bool copy_file_to_tmp(const char *src_path, char *dst_buf, size_t dst_size)
{
	if (!src_path || !dst_buf || dst_size == 0)
		return false;

	FILE *fout = NULL;

#ifdef _WIN32
	char temp_dir[MAX_PATH];
	char temp_path[MAX_PATH];
	DWORD temp_len = GetTempPathA((DWORD)sizeof(temp_dir), temp_dir);
	if (temp_len == 0 || temp_len >= sizeof(temp_dir))
		return false;
	if (!GetTempFileNameA(temp_dir, "hsh", 0, temp_path))
		return false;
	if (strlen(temp_path) >= dst_size) {
		DeleteFileA(temp_path);
		return false;
	}
	strncpy(dst_buf, temp_path, dst_size);
	dst_buf[dst_size - 1] = '\0';
	fout = os_fopen(dst_buf, "wb");
	if (!fout) {
		DeleteFileA(temp_path);
		return false;
	}
#else
	const char *temp_env = getenv("TMPDIR");
	const char *temp_dir = temp_env ? temp_env : "/tmp";
	struct dstr temp_tmpl = {0};
	dstr_printf(&temp_tmpl, "%s/hypershader_XXXXXX", temp_dir);
	if (temp_tmpl.len >= dst_size) {
		blog(LOG_ERROR, "[HyperShader] Temporary file path exceeds buffer size.");
		dstr_free(&temp_tmpl);
		return false;
	}

	int fd = mkstemp(temp_tmpl.array);
	if (fd < 0) {
		dstr_free(&temp_tmpl);
		return false;
	}
	strncpy(dst_buf, temp_tmpl.array, dst_size);
	dst_buf[dst_size - 1] = '\0';
	dstr_free(&temp_tmpl);
	fout = fdopen(fd, "wb");
	if (!fout) {
		close(fd);
		os_unlink(dst_buf);
		return false;
	}
#endif

	FILE *fin = os_fopen(src_path, "rb");
	if (!fin) {
		fclose(fout);
		os_unlink(dst_buf);
		return false;
	}

	bool ok = true;
	char buf[4096];
	size_t read_count;
	while ((read_count = fread(buf, 1, sizeof(buf), fin)) > 0) {
		if (fwrite(buf, 1, read_count, fout) != read_count) {
			ok = false;
			break;
		}
	}
	if (ferror(fin))
		ok = false;

	fclose(fin);
	if (fclose(fout) != 0)
		ok = false;

	if (!ok)
		os_unlink(dst_buf);
	return ok;
}

bool channel_preview_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	(void)props;
	const char *pname = obs_property_name(property);
	struct hypershader_data *s = data;
	if (!pname || !s)
		return false;

	/* Channel preview (channelN_preview) */
	int ch = -1;
	if (sscanf(pname, "channel%d_preview", &ch) == 1) {
		char key_info[64];
		snprintf(key_info, sizeof(key_info), "channel%d_info", ch);
		obs_property_t *pinfo = obs_properties_get(props, key_info);

		/* Prefer OBS source info when available */
		if (s->channel_types[ch] == 2 && s->channel_source_names[ch] && s->channel_source_names[ch][0]) {
			obs_source_t *src = hs_source_selection_resolve(s->channel_source_names[ch], s->channel_source_uuids[ch]);
			if (src) {
				uint32_t sw = obs_source_get_width(src);
				uint32_t sh = obs_source_get_height(src);
				char buf[256];
				snprintf(buf, sizeof(buf), "Source '%s' (%ux%u)", obs_source_get_name(src), sw, sh);
				if (pinfo)
					obs_property_set_long_description(pinfo, buf);
				blog(LOG_INFO, "[HyperShader] Channel %d preview: %s", ch, buf);
				obs_source_release(src);
				return false;
			} else {
				if (pinfo)
					obs_property_set_long_description(pinfo, "Source not found");
				blog(LOG_WARNING, "[HyperShader] Channel %d preview: source '%s' not found", ch,
				     s->channel_source_names[ch]);
				return false;
			}
		}

		if (s->channel_types[ch] == 1 && s->channel_files[ch]) {
			/* Copy file to /tmp so the user can inspect it as a 'preview' */
			char dst[256];
			if (copy_file_to_tmp(s->channel_files[ch], dst, sizeof(dst))) {
				char buf[512];
				snprintf(buf, sizeof(buf), "Saved preview to: %s", dst);
				if (pinfo)
					obs_property_set_long_description(pinfo, buf);
				blog(LOG_INFO, "[HyperShader] Channel %d preview saved: %s", ch, dst);
			} else {
				if (pinfo)
					obs_property_set_long_description(pinfo, "Failed to save preview");
				blog(LOG_WARNING, "[HyperShader] Channel %d preview failed to save file: %s", ch,
				     s->channel_files[ch] ? s->channel_files[ch] : "(null)");
			}
			return false;
		}

		if (pinfo)
			obs_property_set_long_description(pinfo, "(none)");
		return false;
	}

	/* Note: Removed input preview handler - inputs system eliminated (use channel preview) */

	return false;
}

/* Import the selected file and apply it as the current shader */
bool confirm_add_shader_file_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	(void)props;
	(void)property;
	struct hypershader_data *s = data;
	if (!s || !s->context)
		return false;

	obs_data_t *settings = obs_source_get_settings(s->context);
	if (!settings)
		return false;

	const char *path = obs_data_get_string(settings, "add_shader_file");
	if (!path || !path[0]) {
		blog(LOG_WARNING, "[HyperShader] No shader file specified to import");
		obs_data_release(settings);
		return false;
	}

	/* Update shader path settings */
	obs_data_set_string(settings, "shader_file", path);
	obs_data_set_int(settings, "preset", hyper_ui_custom_preset_index()); /* Custom */

	obs_source_update(s->context, settings);
	blog(LOG_INFO, "[HyperShader] Imported shader: %s", path);

	obs_data_release(settings);
	return true; /* Refresh to update UI */
}

bool create_from_raw_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	(void)props;
	(void)property;
	struct hypershader_data *s = data;
	if (!s || !s->context)
		return false;

	obs_data_t *settings = obs_source_get_settings(s->context);
	if (!settings)
		return false;

	const char *code = obs_data_get_string(settings, "raw_shader_code");
	if (!code || !code[0]) {
		blog(LOG_WARNING, "[HyperShader] No raw shader code provided");
		obs_data_release(settings);
		return false;
	}

	/* Determine filename */
	const char *name = obs_data_get_string(settings, "save_preset_name");
	char filename[64];
	if (name && name[0]) {
		snprintf(filename, sizeof(filename), "%s.effect", name);
	} else {
		snprintf(filename, sizeof(filename), "raw_shader_%d.effect", (int)time(NULL));
	}

	char *presets_dir = shaders_dir_from_known();
	if (!presets_dir) {
		blog(LOG_ERROR, "[HyperShader] ┌─ Raw save failed");
		blog(LOG_ERROR, "[HyperShader] └─ Action: shader folder unavailable");
		obs_data_release(settings);
		return false;
	}
	struct dstr full_path = {0};
	dstr_printf(&full_path, "%s/%s", presets_dir, filename);
	bfree(presets_dir);

	/* Write to file */
	if (os_quick_write_utf8_file(full_path.array, code, strlen(code), false)) {
		blog(LOG_INFO, "[HyperShader] Saved raw shader to: %s", full_path.array);
		scan_shaders_dir();

		/* Load it */
		obs_data_set_string(settings, "shader_file", full_path.array);
		pthread_mutex_lock(&global_metadata_mutex);
		int64_t preset_id = shader_ui_preset_for(full_path.array);
		pthread_mutex_unlock(&global_metadata_mutex);
		obs_data_set_int(settings, "preset", preset_id);
		obs_source_update(s->context, settings);

		/* Clear name field to prevent accidental overwrite next time */
		obs_data_set_string(settings, "save_preset_name", "");
	} else {
		blog(LOG_ERROR, "[HyperShader] ┌─ Raw save failed");
		blog(LOG_ERROR, "[HyperShader] ├─ Target : %s", full_path.array);
		blog(LOG_ERROR, "[HyperShader] └─ Action: check disk permissions");
	}

	dstr_free(&full_path);
	obs_data_release(settings);
	return true;
}

bool open_shader_dock_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	UNUSED_PARAMETER(data);

#ifdef HAVE_SHADER_BUILDER_DOCK
	shader_builder_dock_show();
	return true;
#else
	blog(LOG_WARNING,
	     "[HyperShader] Shader Builder dock is not built. Reconfigure with ENABLE_QT=ON and ENABLE_FRONTEND_API=ON.");
	return false;
#endif
}

/* Helper to strip lines starting with a prefix */
static void str_strip_lines_starting_with(char *str, const char *prefix)
{
	if (!str || !prefix)
		return;
	size_t plen = strlen(prefix);
	char *read = str;
	char *write = str;
	while (*read) {
		char *line_start = read;
		char *line_end = strchr(read, '\n');
		size_t line_len = line_end ? (size_t)(line_end - read) : strlen(read);

		/* Check if line starts with prefix (skip whitespace) */
		char *p = line_start;
		while (isspace(*p) && p < (line_end ? line_end : read + line_len))
			p++;

		if (strncmp(p, prefix, plen) == 0) {
			/* Skip this line */
			read += line_len + (line_end ? 1 : 0);
		} else {
			/* Keep this line */
			if (read != write) {
				memmove(write, read, line_len + (line_end ? 1 : 0));
			}
			write += line_len + (line_end ? 1 : 0);
			read += line_len + (line_end ? 1 : 0);
		}
		if (!line_end)
			break;
	}
	*write = '\0';
}

/* Helper to generate ISF JSON header from metadata */
static void write_isf_header(FILE *f, const shader_meta_t *meta)
{
	fprintf(f, "/*\n");
	fprintf(f, "{\n");
	fprintf(f, "  \"DESCRIPTION\": \"Exported from HyperShader\",\n");
	fprintf(f, "  \"ISFVSN\": \"2\",\n");
	fprintf(f, "  \"INPUTS\": [\n");

	if (meta) {
		for (size_t i = 0; i < meta->num; i++) {
			struct shader_param_meta *m = &meta->array[i];
			fprintf(f, "    {\n");
			fprintf(f, "      \"NAME\": \"%s\",\n", m->name);
			fprintf(f, "      \"TYPE\": \"float\",\n"); /* Assuming float for sliders */
			if (m->has_min)
				fprintf(f, "      \"MIN\": %f,\n", m->min);
			if (m->has_max)
				fprintf(f, "      \"MAX\": %f,\n", m->max);
			if (m->has_default)
				fprintf(f, "      \"DEFAULT\": %f,\n", m->default_val);
			if (m->label)
				fprintf(f, "      \"LABEL\": \"%s\",\n", m->label);
			fprintf(f, "    }%s\n", (i < meta->num - 1) ? "," : "");
		}
	}

	fprintf(f, "  ]\n");
	fprintf(f, "}\n");
	fprintf(f, "*/\n\n");
}

bool export_shader_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	(void)props;
	(void)property;
	struct hypershader_data *s = data;
	if (!s || !s->context || !s->shader_path)
		return false;

	/* Get export format */
	obs_data_t *settings = obs_source_get_settings(s->context);
	int format = 0;
	if (settings) {
		format = (int)obs_data_get_int(settings, "export_format");
		obs_data_release(settings);
	}

	char *path = s->shader_path;
	blog(LOG_INFO, "[HyperShader] Exporting shader: %s (Format: %d)", path, format);

	FILE *f = fopen(path, "r");
	if (!f) {
		blog(LOG_ERROR, "[HyperShader] ┌─ Export failed");
		blog(LOG_ERROR, "[HyperShader] ├─ Source : %s", path);
		blog(LOG_ERROR, "[HyperShader] └─ Action: source file missing or unreadable");
		return false;
	}

	/* Read original shader into memory */
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	if (size <= 0) {
		fclose(f);
		return false;
	}
	fseek(f, 0, SEEK_SET);
	char *buf = bmalloc(size + 1);
	if (!buf) {
		fclose(f);
		return false;
	}
	size_t read_bytes = fread(buf, 1, size, f);
	buf[read_bytes] = '\0';
	fclose(f);

	/* Generate exported filename */
	char export_path[PATH_MAX];
	char *shaders_dir = shaders_dir_from_known();
	if (!shaders_dir) {
		bfree(buf);
		return false;
	}

	snprintf(export_path, sizeof(export_path), "%s/exports", shaders_dir);

	/* Cross-platform: ensure directory exists */
	if (!os_mkdirs(export_path)) {
		/* If creation fails and the directory does not already exist,
           bail out cleanly to avoid confusing errors further down. */
		blog(LOG_ERROR, "[HyperShader] Failed to create export directory: %s", export_path);
		bfree(buf);
		bfree(shaders_dir);
		return false;
	}

	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	char filename[128];
	const char *ext = ".effect";
	if (format == 1)
		ext = ".glsl";
	if (format == 2)
		ext = ".fs";

	snprintf(filename, sizeof(filename), "export_%04d%02d%02d_%02d%02d%02d%s", t->tm_year + 1900, t->tm_mon + 1,
		 t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, ext);

	char full_export_path[PATH_MAX + 256];
	snprintf(full_export_path, sizeof(full_export_path), "%s/%s", export_path, filename);
	bfree(shaders_dir);

	FILE *f_out = fopen(full_export_path, "w");
	if (!f_out) {
		blog(LOG_ERROR, "[HyperShader] ┌─ Export failed");
		blog(LOG_ERROR, "[HyperShader] ├─ Target : %s", full_export_path);
		blog(LOG_ERROR, "[HyperShader] └─ Action: check export directory permissions");
		bfree(buf);
		return false;
	}

	/* Process based on format */
	if (format == 0) {
		/* OBS Effect - Identity export with header */
		fprintf(f_out, "// --- HYPERSHADER EXPORT (OBS Effect) ---\n");
		fprintf(f_out, "// Date: %s", asctime(t));
		fprintf(f_out, "// Original: %s\n\n", path);
		fprintf(f_out, "%s", buf);
	} else if (format == 1) {
		/* Shadertoy GLSL - Clean up OBS specifics */
		fprintf(f_out, "// --- HYPERSHADER EXPORT (GLSL) ---\n");
		fprintf(f_out, "// Date: %s", asctime(t));

		/* Clean the buffer */
		str_strip_lines_starting_with(buf, "#include");
		str_strip_lines_starting_with(buf, "SHADERTOY_MAINIMAGE_WRAPPER");

		fprintf(f_out, "%s", buf);
	} else if (format == 2) {
		/* ISF - Add JSON header and wrapper */
		pthread_mutex_lock(&s->mutex);
		if (s->metadata_rc)
			write_isf_header(f_out, &s->metadata_rc->meta);
		pthread_mutex_unlock(&s->mutex);

		fprintf(f_out, "// --- HYPERSHADER EXPORT (ISF) ---\n");

		/* Clean the buffer */
		str_strip_lines_starting_with(buf, "#include");
		str_strip_lines_starting_with(buf, "SHADERTOY_MAINIMAGE_WRAPPER");

		fprintf(f_out, "%s\n\n", buf);

		/* Add ISF main wrapper */
		fprintf(f_out, "void main() {\n");
		fprintf(f_out, "    vec4 color = vec4(0.0);\n");
		fprintf(f_out, "    hs_shade_pixel(color, gl_FragCoord.xy);\n");
		fprintf(f_out, "    gl_FragColor = color;\n");
		fprintf(f_out, "}\n");
	}

	fclose(f_out);
	bfree(buf);

	blog(LOG_INFO, "[HyperShader] Shader exported successfully to: %s", full_export_path);
	/* No need to scan_examples_dir for exports folder */
	return true;
}

bool save_current_preset_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	(void)props;
	(void)property;
	struct hypershader_data *s = data;
	if (!s || !s->context)
		return false;

	obs_data_t *settings = obs_source_get_settings(s->context);
	if (!settings)
		return false;

	const char *name = obs_data_get_string(settings, "save_preset_name");
	if (!name || !name[0])
		name = NULL; /* auto-name if empty */

	bool ok = save_current_shader_as_preset(s, name);
	if (ok) {
		/* refresh properties so the new preset appears in the dropdown */
		atomic_store_explicit(&s->needs_properties_refresh, true, memory_order_seq_cst);
	}

	obs_data_release(settings);
	return ok;
}

/* Simple, defensive scanner to extract Shadertoy "inputs" from a JSON-like file.
 * We don't depend on an external JSON library here: this extracts at most
 * MAX_SHADER_INPUTS occurrences of objects that contain "name" and "src".
	 * The format we support (loose) is: { "inputs": [ { "name":"hs_input0", "src":"example_path" , "type":"texture" }, other entries ] }
 * This is intentionally forgiving: it's not a full JSON parser. It's suitable
 * for initial import and will be improved later when a JSON lib is available.
 */
static const char *token_str(const char *js, const jsmntok_t *t)
{
	size_t len = (size_t)(t->end - t->start);
	char *buf = bmalloc(len + 1);
	memcpy(buf, js + t->start, len);
	buf[len] = '\0';
	return buf; /* caller should bfree() */
}

static int token_eq_str(const char *js, const jsmntok_t *t, const char *s)
{
	size_t len = (size_t)(t->end - t->start);
	return (strlen(s) == len && strncmp(js + t->start, s, len) == 0);
}

static int parse_shadertoy_inputs(const char *text, struct hypershader_data *s)
{
	if (!text || !s)
		return 0;
	jsmn_parser parser;
	jsmn_init(&parser);
	int max_tokens = 8192; // Increased from 1024 to handle more complex/nested JSON structures
	jsmntok_t *tokens = bmalloc(sizeof(jsmntok_t) * max_tokens);
	int ntok = jsmn_parse(&parser, text, strlen(text), tokens, max_tokens);
	if (ntok < 0 || ntok >= max_tokens) { // Check for truncation or error
		bfree(tokens);
		return 0;
	}

	int found_inputs_array = 0;

	// Iterate through tokens to find the "inputs" key and its corresponding array
	for (int i = 0; i < ntok; i++) {
		// Look for a string token that is "inputs"
		if (tokens[i].type == JSMN_STRING && token_eq_str(text, &tokens[i], "inputs")) {
			// Check if the next token is an array
			if (i + 1 < ntok && tokens[i + 1].type == JSMN_ARRAY) {
				jsmntok_t arr_token = tokens[i + 1];
				// Iterate through the elements of the "inputs" array
				// The loop condition should ensure we stay within the bounds of the array token
				for (int j = i + 2; j < ntok && tokens[j].start < arr_token.end; j++) {
					// Each element of the array should be an object
					if (tokens[j].type == JSMN_OBJECT) {
						char *name = NULL;
						char *src = NULL;
						char *type = NULL;

						// Iterate through the key-value pairs of the current input object
						// The loop condition should ensure we stay within the bounds of the current object token
						for (int k = j + 1; k < ntok && tokens[k].start < tokens[j].end; k++) {
							if (tokens[k].type == JSMN_STRING && k + 1 < ntok) {
								if (token_eq_str(text, &tokens[k], "name")) {
									name = (char *)token_str(text, &tokens[k + 1]);
								} else if (token_eq_str(text, &tokens[k], "src")) {
									src = (char *)token_str(text, &tokens[k + 1]);
								} else if (token_eq_str(text, &tokens[k], "type")) {
									type = (char *)token_str(text, &tokens[k + 1]);
								}
								k++; // Advance k past the value token
							}
						}

						if (name && src) {
							int idx = -1;

							if (strncmp(name, "hs_input", 8) == 0) {
								const char *num_str = name + 8;
								char *endptr = NULL;
								long ch = strtol(num_str, &endptr, 10);
								if (endptr != num_str && ch >= 0 &&
								    ch < CHANNEL_COUNT) {
									idx = (int)ch;
								}
							}

							if (idx < 0) {
								idx = found_inputs_array; // Use a running count if no explicit channel
							}

							if (idx < CHANNEL_COUNT) {
								bfree(s->channel_files[idx]);
								s->channel_files[idx] = src;
								s->channel_types[idx] =
									(type && strcmp(type, "video") == 0) ? 2 : 1;
								found_inputs_array++;
							} else {
								bfree(src); // Not used if channel_count exceeded
							}
						}
						bfree(name);
						bfree(type);
						src = NULL; // Clear src pointer after it's either assigned or freed
					}
				}
				// Once "inputs" array is processed, we can break as we usually expect only one such array
				break;
			}
		}
	}
	bfree(tokens);
	return found_inputs_array;
}

/* Load button handler: read file path from current settings, parse inputs and update settings */
bool load_shadertoy_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	(void)props;
	(void)property;
	struct hypershader_data *s = data;
	if (!s || !s->context)
		return false;

	obs_data_t *settings = obs_source_get_settings(s->context);
	if (!settings)
		return false;

	const char *path = obs_data_get_string(settings, "shadertoy_import_file");
	if (!path || !path[0]) {
		blog(LOG_WARNING, "[HyperShader] No import file specified");
		obs_data_release(settings);
		return false;
	}

	FILE *f = fopen(path, "rb");
	if (!f) {
		blog(LOG_ERROR, "[HyperShader] ┌─ Import failed");
		blog(LOG_ERROR, "[HyperShader] ├─ File : %s", path);
		blog(LOG_ERROR, "[HyperShader] └─ Action: unable to open file");
		obs_data_release(settings);
		return false;
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = bzalloc((size_t)sz + 1);
	if (buf) {
		size_t r = fread(buf, 1, (size_t)sz, f);
		buf[r] = '\0';
		int parsed = parse_shadertoy_inputs(buf, s);
		blog(LOG_INFO, "[HyperShader] Parsed %d inputs from %s", parsed, path);

		/* parse_shadertoy_inputs now writes directly to s->channel_type and s->channel_file,
         * so we just need to mirror these to settings for persistence */
		for (int i = 0; i < CHANNEL_COUNT; i++) {
			char key_type[32];
			char key_file[32];
			char key_src[32];
			snprintf(key_type, sizeof(key_type), "channel%d_type", i);
			snprintf(key_file, sizeof(key_file), "channel%d_file", i);
			snprintf(key_src, sizeof(key_src), "channel%d_source", i);

			obs_data_set_int(settings, key_type, s->channel_types[i]);
			if (s->channel_files[i])
				obs_data_set_string(settings, key_file, s->channel_files[i]);
			else
				obs_data_set_string(settings, key_file, "");
			obs_data_set_string(settings, key_src, "");
		}

		/* Apply updated settings to the live source to trigger filter_update and UI refresh */
		// obs_source_update(s->context, settings); // Removed to prevent re-entrant deadlock

		bfree(buf);
	}
	fclose(f);
	obs_data_release(settings);
	/* returning true indicates the properties may need refresh */
	return true;
}

#ifdef HAVE_LIBCURL
/* Helper used by libcurl to accumulate response data */
typedef struct {
	char *data;
	size_t len;
} shadertoy_buf_t;

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	size_t realsz = size * nmemb;
	shadertoy_buf_t *b = userdata;
	size_t old = b->len;
	b->data = brealloc(b->data, old + realsz + 1);
	memcpy(b->data + old, ptr, realsz);
	b->len = old + realsz;
	b->data[b->len] = '\0';
	return realsz;
}

bool load_shadertoy_url_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	(void)props;
	(void)property;
	struct hypershader_data *s = data;
	if (!s || !s->context)
		return false;

	obs_data_t *settings = obs_source_get_settings(s->context);
	if (!settings)
		return false;

	const char *url = obs_data_get_string(settings, "shadertoy_import_url");
	if (!url || !url[0]) {
		blog(LOG_WARNING, "[HyperShader] ┌─ URL import ignored");
		blog(LOG_WARNING, "[HyperShader] └─ Action: URL is empty");
		obs_data_release(settings);
		return false;
	}

	/* libcurl must be available and linked; otherwise this symbol is not compiled */
	CURL *curl = curl_easy_init();
	if (!curl) {
		blog(LOG_ERROR, "[HyperShader] libcurl not initialized");
		obs_data_release(settings);
		return false;
	}

	shadertoy_buf_t buf = {NULL, 0};

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		blog(LOG_ERROR, "[HyperShader] ┌─ URL import failed");
		blog(LOG_ERROR, "[HyperShader] └─ Detail: %s", curl_easy_strerror(res));
		if (buf.data)
			bfree(buf.data);
		curl_easy_cleanup(curl);
		obs_data_release(settings);
		return false;
	}

	/* Parse the downloaded buffer directly first, assuming it might be pure JSON */
	int parsed = 0;
	if (buf.data) {
		parsed = parse_shadertoy_inputs(buf.data, s);
		if (parsed > 0) {
			blog(LOG_INFO, "[HyperShader] Parsed %d inputs directly from URL content", parsed);
		} else {
			blog(LOG_INFO, "[HyperShader] No inputs found by direct JSON parse. Trying HTML patterns.");

			// Fallback: If no inputs were parsed, try to extract JSON fragments from HTML
			// (Shadertoy pages often embed shader JSON inside HTML)
			const char *patterns[] = {
				"\"inputs\":", "var gShader =", "var mShader =", NULL}; // Modified to target value
			for (int p_idx = 0; patterns[p_idx]; p_idx++) {
				char *p = strstr(buf.data, patterns[p_idx]);
				if (!p)
					continue;

				char *json_fragment = NULL;

				if (strcmp(patterns[p_idx], "\"inputs\":") == 0) {
					// Found "inputs": - expect an array directly after it
					char *arr_start = strchr(p + strlen("\"inputs\":"), '[');
					if (!arr_start)
						continue;

					int depth = 0;
					char *q = arr_start;
					while (*q) {
						if (*q == '[')
							depth++;
						else if (*q == ']') {
							depth--;
							if (depth == 0)
								break;
						}
						q++;
					}
					if (*q == ']') {
						size_t arrlen = (size_t)(q - arr_start + 1);
							json_fragment = bmalloc(arrlen + 32); // Max length for {"inputs": array}
						snprintf(json_fragment, arrlen + 32, "{\"inputs\":%.*s}", (int)arrlen,
							 arr_start);
					}
				} else {
					// Found "var gShader =" or "var mShader =" - expect an object after it
					char *obj_start = strchr(p, '{');
					if (!obj_start)
						continue;

					int depth = 0;
					char *q = obj_start;
					while (*q) {
						if (*q == '{')
							depth++;
						else if (*q == '}') {
							depth--;
							if (depth == 0)
								break;
						}
						q++;
					}
					if (*q == '}') {
						size_t objlen = (size_t)(q - obj_start + 1);
						json_fragment = bmalloc(objlen + 1);
						memcpy(json_fragment, obj_start, objlen);
						json_fragment[objlen] = '\0';
					}
				}

				if (json_fragment) {
					int p_res = parse_shadertoy_inputs(json_fragment, s);
					bfree(json_fragment);
					if (p_res > 0) {
						blog(LOG_INFO, "[HyperShader] Parsed %d inputs from HTML pattern '%s'",
						     p_res, patterns[p_idx]);
						parsed = p_res;
						break;
					}
				}
			}
		}

		if (parsed <= 0) {
			blog(LOG_WARNING,
			     "[HyperShader] No inputs parsed from URL %s; the page may not expose JSON inputs directly",
			     url);
		}

		/* parse_shadertoy_inputs now writes directly to channels */
		for (int i = 0; i < CHANNEL_COUNT; i++) {
			char key_type[32];
			char key_file[32];
			snprintf(key_type, sizeof(key_type), "channel%d_type", i);
			snprintf(key_file, sizeof(key_file), "channel%d_file", i);
			obs_data_set_int(settings, key_type, s->channel_types[i]);
			if (s->channel_files[i])
				obs_data_set_string(settings, key_file, s->channel_files[i]);
			else
				obs_data_set_string(settings, key_file, "");
		}
		// obs_source_update(s->context, settings); // Removed to prevent re-entrant deadlock
		bfree(buf.data);
	}

	curl_easy_cleanup(curl);
	obs_data_release(settings);
	return true;
}

/* Allow the user to paste raw Shadertoy JSON into a text area and import it directly */
bool load_shadertoy_json_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	(void)props;
	(void)property;
	struct hypershader_data *s = data;
	if (!s || !s->context)
		return false;

	obs_data_t *settings = obs_source_get_settings(s->context);
	if (!settings)
		return false;

	const char *json = obs_data_get_string(settings, "shadertoy_import_json");
	if (!json || !json[0]) {
		blog(LOG_WARNING, "[HyperShader] ┌─ Paste import ignored");
		blog(LOG_WARNING, "[HyperShader] └─ Action: JSON payload is empty");
		obs_data_release(settings);
		return false;
	}

	/* Parse provided JSON directly */
	int parsed = parse_shadertoy_inputs(json, s);
	blog(LOG_INFO, "[HyperShader] Parsed %d inputs from pasted JSON", parsed);

	/* parse_shadertoy_inputs now writes directly to channels */
	for (int i = 0; i < CHANNEL_COUNT; i++) {
		char key_type[32];
		char key_file[32];
		snprintf(key_type, sizeof(key_type), "channel%d_type", i);
		snprintf(key_file, sizeof(key_file), "channel%d_file", i);
		obs_data_set_int(settings, key_type, s->channel_types[i]);
		if (s->channel_files[i])
			obs_data_set_string(settings, key_file, s->channel_files[i]);
		else
			obs_data_set_string(settings, key_file, "");
	}
	// obs_source_update(s->context, settings); // Removed to prevent re-entrant deadlock
	obs_data_release(settings);
	return true;
}
#else
bool load_shadertoy_url_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	(void)props;
	(void)property;
	(void)data;
	blog(LOG_WARNING, "[HyperShader] URL import not available (libcurl missing at build)");
	return false;
}
#endif

bool preview_sound_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	(void)props;
	(void)property;
	struct hypershader_data *s = data;
	if (!s)
		return false;

	if (s->sound_preview_running) {
		blog(LOG_INFO, "[HyperShader] Sound preview already running");
		return false;
	}

	s->sound_preview_running = true; /* set BEFORE create so filter_destroy always joins */
	if (pthread_create(&s->sound_preview_thread, NULL, sound_preview_thread_fn, s) != 0) {
		s->sound_preview_running = false;
		blog(LOG_ERROR, "[HyperShader] ┌─ Preview failed");
		blog(LOG_ERROR, "[HyperShader] └─ Action: unable to start sound preview thread");
		return false;
	}

	return false;
}

bool example_selection_changed2(void *priv, obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	(void)p;
	struct hypershader_data *s = priv;
	obs_data_t *live = settings;
	obs_data_t *tmp = NULL;

	if (s && s->context) {
		tmp = obs_source_get_settings(s->context);
		if (tmp)
			live = tmp;
	}

	/* Internal/not user-facing: legacy key used only by deprecated example callbacks. */
	const char *sel = obs_data_get_string(live, "example_list");
	const char *info = "(no description)";
	pthread_mutex_lock(&global_metadata_mutex);
	if (sel && sel[0]) {
		/* first check dynamic_examples (scanned) */
		for (size_t i = 0; i < dynamic_examples_count; ++i) {
			if (strcmp(sel, dynamic_examples[i].id) == 0) {
				info = dynamic_examples[i].desc;
				break;
			}
		}
	}

	obs_property_t *pinfo = obs_properties_get(props, "example_info");
	if (pinfo)
		obs_property_set_long_description(pinfo, info);
	pthread_mutex_unlock(&global_metadata_mutex);

	if (tmp)
		obs_data_release(tmp);

	return false;
}

bool load_example_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	(void)props;
	(void)property;
	struct hypershader_data *s = data;
	if (!s || !s->context)
		return false;

	obs_data_t *settings = obs_source_get_settings(s->context);
	if (!settings)
		return false;

	/* Internal/not user-facing: legacy key used only by deprecated example callbacks. */
	const char *sel = obs_data_get_string(settings, "example_list");
	if (!sel || !sel[0]) {
		blog(LOG_WARNING, "[HyperShader] No example selected");
		obs_data_release(settings);
		return false;
	}

	char *file = NULL;
	pthread_mutex_lock(&global_metadata_mutex);
	/* check dynamic list first */
	for (size_t i = 0; i < dynamic_examples_count; ++i) {
		if (strcmp(sel, dynamic_examples[i].id) == 0) {
			file = bstrdup(dynamic_examples[i].file);
			break;
		}
	}
	pthread_mutex_unlock(&global_metadata_mutex);
	if (!file) {
		blog(LOG_WARNING, "[HyperShader] Selected example not found");
		obs_data_release(settings);
		return false;
	}

	/* Resolve file path using obs_module_file so installed data is found */
	char *path = obs_module_file(file);
	if (!path) {
		blog(LOG_ERROR, "[HyperShader] ┌─ Example load failed");
		blog(LOG_ERROR, "[HyperShader] └─ File : %s", file);
		bfree(file);
		obs_data_release(settings);
		return false;
	}

	obs_data_set_string(settings, "shader_file", path);
	// obs_source_update(s->context, settings); // Removed to prevent re-entrant deadlock
	blog(LOG_INFO, "[HyperShader] Loaded example shader: %s", file);
	bfree(file);

	bfree(path);
	obs_data_release(settings);
	return true; /* indicate properties may refresh */
}

bool reset_time_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	struct hypershader_data *s = data;
	if (s)
		s->time_accumulator = 0.0f;
        s->shader_frame_count = 0.0f;
	blog(LOG_INFO, "[HyperShader] Time reset");
	return false;
}

/* Probe callback used during mix enumeration — does nothing but exists so we can test connects */

bool log_audio_mixes_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	(void)props;
	(void)property;
	struct hypershader_data *s = data;

	if (s->probe_running) {
		blog(LOG_INFO, "[HyperShader] Audio mix probe already in progress");
		return false;
	}

	s->probe_running = true; /* set BEFORE create so filter_destroy always joins */
	if (pthread_create(&s->probe_thread, NULL, probe_audio_mixes_thread, s) == 0) {
		blog(LOG_INFO, "[HyperShader] Started audio mix probe in background");
	} else {
		s->probe_running = false;
		blog(LOG_ERROR, "[HyperShader] Failed to start audio mix probe thread");
	}

	return false;
}

bool channel_type_changed(void *priv, obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	const char *name = obs_property_name(p);

	/* name is like "channel0_type" */
	if (!name || !settings)
		return false;

	int idx = -1;
	if (sscanf(name, "channel%d_type", &idx) != 1 || idx < 0 || idx >= CHANNEL_COUNT) {
		return false;
	}

	obs_properties_t *root = props;
	while (obs_properties_get_parent(root))
		root = obs_properties_get_parent(root);
	sync_input_visibility(priv, root, settings);

	return true;
}

bool pg_append_code(obs_properties_t *props, obs_property_t *p, void *data, const char *snippet)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	struct hypershader_data *s = data;
	if (!s || !s->context)
		return false;
	obs_data_t *settings = obs_source_get_settings(s->context);
	const char *code = obs_data_get_string(settings, "raw_shader_code");
	struct dstr new_code = {0};
	dstr_copy(&new_code, code ? code : "");
	if (!dstr_is_empty(&new_code))
		dstr_cat(&new_code, "\n");
	dstr_cat(&new_code, snippet);
	obs_data_set_string(settings, "raw_shader_code", new_code.array);
	dstr_free(&new_code);
	// obs_source_update(s->context, settings); // Removed to prevent re-entrant deadlock
	obs_data_release(settings);
	return true;
}

bool pg_insert_retro_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	return pg_append_code(props, p, data, "#include \"stdlib/retro.glsl\"");
}

/* Code snippets for insertion */
static const char *pg_get_snippet(int id)
{
	switch (id) {
	case 1: /* Basic Template */
		return "// Basic HyperShader Template\n"
		       "#include \"shadertoy_compat.glsl\"\n\n"
		       "void hs_shade_pixel(out vec4 fragColor, in vec2 fragCoord) {\n"
		       "    vec2 uv = fragCoord / hs_resolution.xy;\n"
		       "    \n"
		       "    // Your shader code here\n"
		       "    vec3 col = vec3(uv, 0.5 + 0.5 * sin(hs_time));\n"
		       "    \n"
		       "    fragColor = vec4(col, 1.0);\n"
		       "}\n";

	/* Uniforms */
	case 10:
		return "float t = hs_time; // Time in seconds";
	case 11:
		return "vec2 uv = fragCoord / hs_resolution.xy; // Normalized UV";
	case 12:
		return "vec2 mouse = hs_pointer.xy / hs_resolution.xy; // Normalized mouse";
	case 13:
		return "vec4 tex = texture(hs_input0, uv); // Sample texture";
	case 14:
		return "float level = audio_level; // Audio RMS 0.0-1.0";
	case 15:
		return "float progress = hs_transition_progress; // Transition 0.0-1.0";

	/* Retro */
	case 20:
		return "#include \"stdlib/retro.glsl\"\n"
		       "vec2 curved_uv = retro_curve(uv, 0.2); // Barrel distortion";
	case 21:
		return "#include \"stdlib/retro.glsl\"\n"
		       "float sl = retro_scanlines(uv, 0.3, 2.0); // Scanlines\n"
		       "col *= sl;";
	case 22:
		return "#include \"stdlib/retro.glsl\"\n"
		       "vec3 mask = retro_maskAperture(fragCoord, 1.0);\n"
		       "col *= mask;";
	case 23:
		return "#include \"stdlib/retro.glsl\"\n"
		       "vec3 yiq = rgb2yiq(col);\n"
		       "// Modify yiq here\n"
		       "col = yiq2rgb(yiq);";

	/* Colors */
	case 30:
		return "#include \"stdlib/colors.glsl\"\n"
		       "vec3 hsv = rgb2hsv(col);\n"
		       "hsv.x += 0.1; // Shift hue\n"
		       "col = hsv2rgb(hsv);";
	case 31:
		return "#include \"stdlib/colors.glsl\"\n"
		       "col = ACESFilm(col * 1.5); // Tonemap with exposure";
	case 32:
		return "#include \"stdlib/colors.glsl\"\n"
		       "col = blend_screen(col, vec3(0.1, 0.05, 0.2)); // Screen blend";

	/* Filters */
	case 40:
		return "#include \"stdlib/filters.glsl\"\n"
		       "float v = vignette(uv, 0.5, 0.8);\n"
		       "col *= v;";
	case 41:
		return "#include \"stdlib/filters.glsl\"\n"
		       "col += filmGrain(uv, hs_time, 0.05); // Add grain";
	case 42:
		return "#include \"stdlib/filters.glsl\"\n"
		       "col = chromaticAberration(hs_input0, uv, 0.01);";

	/* Dynamics Examples */
	case 50:
		return "// Spring-damper audio reactive parameter\n"
		       "// freq=4.0 (response speed), damp=0.7 (bounciness)\n"
		       "uniform float uBass; // @audio \"bass\" @dynamics \"spring\" freq=4.0 damp=0.7";
	case 51:
		return "// Smooth critically-damped (no bounce)\n"
		       "uniform float uLevel; // @audio \"level\" @dynamics \"smooth\" freq=8.0";
	case 52:
		return "// Bouncy underdamped spring\n"
		       "uniform float uPeak; // @audio \"peak\" @dynamics \"spring\" freq=6.0 damp=0.3";

	/* PS1 Retro Effects */
	case 70:
		return "#include \"stdlib/retro.glsl\"\n"
		       "// PS1-style UV jitter (vertex wobble)\n"
		       "vec2 ps1UV = ps1_jitter(uv, 256.0); // 256 = PS1 resolution";
	case 71:
		return "#include \"stdlib/retro.glsl\"\n"
		       "// PS1 jitter with animated wobble\n"
		       "vec2 ps1UV = ps1_jitter_animated(uv, 256.0, hs_time, 0.5);";
	case 72:
		return "#include \"stdlib/retro.glsl\"\n"
		       "// PS1 15-bit color depth (32 levels)\n"
		       "col = ps1_colorDepth(col, 32.0);";
	case 73:
		return "#include \"stdlib/retro.glsl\"\n"
		       "// PS1 complete filter\n"
		       "col = ps1_filter(image, uv, hs_resolution.xy, 256.0, 32.0, hs_time);";
	case 74:
		return "#include \"stdlib/retro.glsl\"\n"
		       "// VHS tracking distortion\n"
		       "uv = vhs_tracking(uv, hs_time, 0.5);";

	default:
		return NULL;
	}
}

bool pg_insert_func_changed(void *priv, obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(priv);
	(void)props;
	(void)p;

	int id = (int)obs_data_get_int(settings, "pg_insert_func");
	if (id == 0)
		return false; /* "Select to Insert" - do nothing */

	const char *snippet = pg_get_snippet(id);
	if (!snippet)
		return false;

	/* Append snippet to code */
	const char *code = obs_data_get_string(settings, "raw_shader_code");
	struct dstr new_code = {0};
	dstr_copy(&new_code, code ? code : "");
	if (!dstr_is_empty(&new_code))
		dstr_cat(&new_code, "\n\n");
	dstr_cat(&new_code, snippet);
	obs_data_set_string(settings, "raw_shader_code", new_code.array);
	dstr_free(&new_code);

	/* Reset dropdown to "Select" */
	obs_data_set_int(settings, "pg_insert_func", 0);

	/* Update source to persist change */
	// if (s && s->context) {
	//     obs_source_update(s->context, settings);
	// } // Removed to prevent re-entrant deadlock

	return true; /* Refresh properties to show updated code */
}

void hyper_ui_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "ui_page", "general");
	obs_data_set_default_int(settings, "preset", -1);
    obs_data_set_default_string(settings, "shader_category", "filter");
	obs_data_set_default_int(settings, "preset_schema", 2);
	obs_data_set_default_int(settings, "input_mode", INPUT_MODE_SOURCE);
	obs_data_set_default_double(settings, "speed", 1.0);
    obs_data_set_default_double(settings, "compose_base_opacity", 1.0);
    obs_data_set_default_double(settings, "compose_effect_opacity", 1.0);
    obs_data_set_default_int(settings, "compose_blend_mode", 0);
	obs_data_set_default_bool(settings, "enable_sound", false);
	obs_data_set_default_int(settings, "sound_mix", 0);
	obs_data_set_default_int(settings, "audio_mix", 0);
	obs_data_set_default_int(settings, "audio_update_rate", 20);
	obs_data_set_default_double(settings, "audio_smoothing", 0.25);
	obs_data_set_default_int(settings, "audio_fft_size", 1024);
	obs_data_set_default_int(settings, "audio_window_type", AUDIO_WIN_HANN);
	obs_data_set_default_int(settings, "mp_controlnet_model", MP_GRAPH_FACE_MESH);
	obs_data_set_default_bool(settings, "sight_use_tracking", false);
	obs_data_set_default_string(settings, "sight_tracking_channel", "face");
	obs_data_set_default_bool(settings, "raw_edit_mode", false);
	obs_data_set_default_string(settings, "shader_file", "");
	obs_data_set_default_int(settings, "source_width", HYPER_SOURCE_WIDTH_DEFAULT);
	obs_data_set_default_int(settings, "source_height", HYPER_SOURCE_HEIGHT_DEFAULT);
}

void hyper_ui_get_source_defaults(obs_data_t *settings)
{
    hyper_ui_get_defaults(settings);
    obs_data_set_default_string(settings, "shader_category", "source");
}

void hyper_ui_get_transition_defaults(obs_data_t *settings)
{
    hyper_ui_get_defaults(settings);
    /* Real two-scene effects are preferred by the transition kind.
     * All Shaders remains available for advanced users. */
    obs_data_set_default_string(settings, "shader_category", "transition");
}

/* OBS invokes source-info properties with data=NULL in its Add UI.
 * Preserve the registration's kind instead of defaulting every picker to
 * filter ordering when there is no live instance yet. */
static obs_properties_t *hyper_ui_get_properties_for_kind(
    void *data, enum hypershader_instance_kind requested_kind)
{
    struct hypershader_data *s = data;
    const enum hypershader_instance_kind effective_kind =
        s ? s->kind : requested_kind;
    const char *preferred_role = effective_kind == HYPER_KIND_SOURCE
        ? "source" : effective_kind == HYPER_KIND_TRANSITION
        ? "transition" : "filter";
    /* s can be NULL when OBS asks for type-level properties. */

	/* Set guard flag to prevent re-entrant callbacks during construction */
	if (s) {
		atomic_store_explicit(&s->properties_building, true, memory_order_seq_cst);
	}

	obs_properties_t *props = obs_properties_create();

	blog(LOG_INFO, "[Hypershader] filter_properties START (data=%p)", data);

	/* Choose an effect first, then show its optional file path. */
	pthread_mutex_lock(&global_metadata_mutex);

	if (!presets_loaded)
		load_user_presets_internal();

    /* A reopened properties panel must reflect the actual installed folder,
     * even in a headless/non-Qt build or if an OS watch was unavailable. */
    scan_shaders_dir_internal();

    const char *initial_category = preferred_role;
    if (!shader_category_available(initial_category))
        initial_category = "all";
    obs_data_t *current = s && s->context
        ? obs_source_get_settings(s->context) : NULL;
    if (current) {
        const char *saved = obs_data_get_string(current, "shader_category");
        /* Only an explicitly selected category overrides the plugin-kind
         * default. A source's default string is not a user selection. */
        if (obs_data_has_user_value(current, "shader_category") && saved &&
            shader_category_available(saved))
            initial_category = saved;
    }
    obs_property_t *category = obs_properties_add_list(
        props, "shader_category", "Category",
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    shader_category_fill_options(category, initial_category);
    obs_property_set_long_description(
        category, "Show a collection or shader type. Changing this does not"
                  " replace the shader currently running.");
    obs_property_set_modified_callback2(category, shader_category_changed2, data);

    obs_property_t *preset = obs_properties_add_list(
        props, "preset", obs_module_text("ShaderPreset"),
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_set_long_description(
        preset, "Select a shader within the chosen category, or choose"
                " Custom File to load your own .effect.");
    shader_picker_fill(preset, initial_category, current);
    obs_property_set_modified_callback2(preset, preset_changed2, data);
    if (current)
        obs_data_release(current);

	pthread_mutex_unlock(&global_metadata_mutex);

	properties_add_header(props);

	/* External data bindings come first; artistic controls follow. */
	properties_add_audio_settings(props, s);
	properties_add_sight_settings(props, s);
	properties_add_advanced_settings(props, s);
	properties_add_input_settings(props, s);

	obs_properties_add_button2(props, "open_shader_builder", obs_module_text("OpenShaderBuilder"),
				   open_shader_dock_clicked, s);

	/* Dynamic Shader Parameters (Uniforms) - delegated to properties builder */
	properties_add_shader_params(props, s);

	/* Global playback/compositing is separate from shader-local parameters. */
    properties_add_global_overrides(props, s);

	/* Source dimensions (only for standalone source instances) */
	if (s && s->kind == HYPER_KIND_SOURCE) {
		obs_property_t *sw = obs_properties_add_int(props, "source_width", obs_module_text("SourceWidth"),
							    HYPER_SOURCE_WIDTH_MIN, HYPER_SOURCE_WIDTH_MAX, 64);
		obs_property_set_long_description(sw, "Canvas width for standalone source mode (64-7680).\n"
						      "Changing this requires removing and re-adding the source.");
		obs_property_t *sh = obs_properties_add_int(props, "source_height", obs_module_text("SourceHeight"),
							    HYPER_SOURCE_HEIGHT_MIN, HYPER_SOURCE_HEIGHT_MAX, 64);
		obs_property_set_long_description(sh, "Canvas height for standalone source mode (64-4320).\n"
						      "Changing this requires removing and re-adding the source.");
	}

	/* Match the current shader on the FIRST opening, not just after a
	 * preset callback. This is read-only: never update source settings or
	 * refresh the properties tree while building it. */
    obs_data_t *current_settings = s && s->context ? obs_source_get_settings(s->context) : NULL;
    const char *shader_file = current_settings
        ? obs_data_get_string(current_settings, "shader_file") : NULL;
    if ((!shader_file || !*shader_file) && s)
        shader_file = s->shader_path;
    properties_add_shader_information(props, shader_file);
    if (current_settings) {
        sync_ui_visibility(s, props, current_settings);
		obs_data_release(current_settings);
	} else {
		set_property_visible_if_present(props, "shader_file", false);
		set_property_visible_if_present(props, "enable_sound", false);
		set_property_visible_if_present(props, "audio_source", false);
		set_property_visible_if_present(props, "sight_use_tracking", false);
		set_property_visible_if_present(props, "sight_tracking_channel", false);
		set_property_visible_if_present(props, "mp_controlnet_model", false);
	}

	blog(LOG_INFO, "[Hypershader] filter_properties END (data=%p)", data);

	/* Clear guard flag - properties construction complete */
	if (s) {
		atomic_store_explicit(&s->properties_building, false, memory_order_seq_cst);
	}

	return props;
}

obs_properties_t *hyper_ui_get_properties(void *data)
{
    return hyper_ui_get_properties_for_kind(data, HYPER_KIND_FILTER);
}

obs_properties_t *hyper_ui_get_source_properties(void *data)
{
    return hyper_ui_get_properties_for_kind(data, HYPER_KIND_SOURCE);
}

obs_properties_t *hyper_ui_get_transition_properties(void *data)
{
    return hyper_ui_get_properties_for_kind(data, HYPER_KIND_TRANSITION);
}

static void free_dynamic_examples_internal(void)
{
	if (!dynamic_examples)
		return;
	for (size_t i = 0; i < dynamic_examples_count; ++i) {
		bfree((void *)dynamic_examples[i].id);
		bfree((void *)dynamic_examples[i].label);
		bfree((void *)dynamic_examples[i].file);
		bfree((void *)dynamic_examples[i].desc);
	}
	bfree(dynamic_examples);
	dynamic_examples = NULL;
	dynamic_examples_count = 0;
}

void free_dynamic_examples(void)
{
	pthread_mutex_lock(&global_metadata_mutex);
	free_dynamic_examples_internal();
	pthread_mutex_unlock(&global_metadata_mutex);
}

/* Only the installed shader library is indexed. This deliberately skips
 * platform copies, test fixtures, preview caches and quarantined assets. */
static void scan_installed_effect_directory(const char *shaders_dir, const char *relative)
{
	char directory[PATH_MAX];
	int len = relative[0] ? snprintf(directory, sizeof(directory), "%s/%s", shaders_dir, relative)
			      : snprintf(directory, sizeof(directory), "%s", shaders_dir);
	if (len < 0 || (size_t)len >= sizeof(directory))
		return;
	DIR *d = opendir(directory);
	if (!d)
		return;
	struct dirent *entry;
	while ((entry = readdir(d)) != NULL) {
		const char *name = entry->d_name;
		size_t n = strlen(name);
		if (n <= 7 || strcmp(name + n - 7, ".effect") != 0)
			continue;

		char path[PATH_MAX], id[PATH_MAX];
		int path_len = snprintf(path, sizeof(path), "%s/%s", directory, name);
		int id_len = relative[0] ? snprintf(id, sizeof(id), "%s/%s", relative, name)
					 : snprintf(id, sizeof(id), "%s", name);
		if (path_len < 0 || (size_t)path_len >= sizeof(path) ||
		    id_len < 0 || (size_t)id_len >= sizeof(id))
			continue;
		struct stat st;
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
			continue;
#ifndef _WIN32
		struct stat link_st;
		if (lstat(path, &link_st) != 0 || S_ISLNK(link_st.st_mode))
			continue;
#endif
		struct example_desc ex = {0};
		ex.id = bstrdup(id);
		ex.file = bstrdup(path);
		/* Classification is a display convention, not a filename move.
		 * Keep ex.id stable for saved preset values and ex.file for scenes. */
		char label[PATH_MAX];
        if (!hs_shader_display_label_from_file(label, sizeof(label), id, path)) {
			bfree((void *)ex.id);
			bfree((void *)ex.file);
			continue;
		}
		ex.label = bstrdup(label);
		ex.desc = bstrdup("");
		dynamic_examples = brealloc(dynamic_examples,
					   sizeof(*dynamic_examples) * (dynamic_examples_count + 1));
		dynamic_examples[dynamic_examples_count++] = ex;
	}
	closedir(d);
}

static bool scan_shaders_dir_internal(void)
{
	free_dynamic_examples_internal();
	char *shaders_dir = shaders_dir_from_known();
	if (!shaders_dir) {
		blog(LOG_WARNING, "[HyperShader] Could not determine installed shaders directory");
		return false;
	}

	/* Include directly installed shaders as well as the first-party library. */
	scan_installed_effect_directory(shaders_dir, "");
	scan_installed_effect_directory(shaders_dir, "root");

	char root[PATH_MAX];
	int len = snprintf(root, sizeof(root), "%s/root", shaders_dir);
	if (len > 0 && (size_t)len < sizeof(root)) {
		DIR *collections = opendir(root);
		if (collections) {
			struct dirent *entry;
			while ((entry = readdir(collections)) != NULL) {
				if (!hs_shader_catalog_collection_allowed(entry->d_name))
					continue;
				char folder[PATH_MAX], relative[PATH_MAX];
				int f = snprintf(folder, sizeof(folder), "%s/%s", root, entry->d_name);
				int r = snprintf(relative, sizeof(relative), "root/%s", entry->d_name);
				if (f < 0 || (size_t)f >= sizeof(folder) || r < 0 ||
				    (size_t)r >= sizeof(relative))
					continue;
				struct stat st;
				if (stat(folder, &st) != 0 || !S_ISDIR(st.st_mode))
					continue;
#ifndef _WIN32
				struct stat link_st;
				if (lstat(folder, &link_st) != 0 || S_ISLNK(link_st.st_mode))
					continue;
#endif
				scan_installed_effect_directory(shaders_dir, relative);
			}
			closedir(collections);
		}
	}
	if (dynamic_examples_count > 1)
		qsort(dynamic_examples, dynamic_examples_count, sizeof(*dynamic_examples),
		      compare_verified_examples);
	/* A true hash collision must not present two indistinguishable choices. */
	for (size_t i = 0; i < dynamic_examples_count; i++) {
		for (size_t j = 0; j < i; j++) {
			if (shader_example_preset_id(dynamic_examples[i].id) ==
			    shader_example_preset_id(dynamic_examples[j].id))
				blog(LOG_ERROR, "[HyperShader] Picker ID collision: %s and %s",
				     dynamic_examples[i].id, dynamic_examples[j].id);
		}
	}
	blog(LOG_INFO, "[HyperShader] Scanned %zu installed effect shaders", dynamic_examples_count);
	bfree(shaders_dir);
	return dynamic_examples_count > 0;
}

bool scan_shaders_dir(void)
{
	pthread_mutex_lock(&global_metadata_mutex);
	bool ok = scan_shaders_dir_internal();
	pthread_mutex_unlock(&global_metadata_mutex);
	return ok;
}

char *shaders_dir_from_known(void)
{
	const char *obs_data_path = obs_get_module_data_path(obs_current_module());
	if (obs_data_path && *obs_data_path) {
		char candidate[PATH_MAX];
		snprintf(candidate, sizeof(candidate), "%s/shaders", obs_data_path);
		struct stat st;
		if (stat(candidate, &st) == 0 && S_ISDIR(st.st_mode))
			return bstrdup(candidate);
		return NULL;
	}

	const char *module_file = obs_get_module_file_name(obs_current_module());
	if (module_file && module_file[0]) {
		char module_dir[PATH_MAX];
		strncpy(module_dir, module_file, sizeof(module_dir) - 1);
		module_dir[sizeof(module_dir) - 1] = '\0';

		char *slash = strrchr(module_dir, '/');
		if (slash) {
			*slash = '\0';
			if (strlen(module_dir) + strlen("/hypershader/shaders") < PATH_MAX) {
				char candidate[PATH_MAX];
				snprintf(candidate, sizeof(candidate), "%s/hypershader/shaders", module_dir);
				struct stat st;
				if (stat(candidate, &st) == 0 && S_ISDIR(st.st_mode))
					return bstrdup(candidate);
			}
		}

		const char *lib_segment = strstr(module_file, "/lib/");
		if (lib_segment) {
			size_t root_len = (size_t)(lib_segment - module_file);
			const char *resource_suffix = "/share/obs/obs-plugins/hypershader/shaders";
			if (root_len + strlen(resource_suffix) < PATH_MAX) {
				char candidate[PATH_MAX];
				memcpy(candidate, module_file, root_len);
				candidate[root_len] = '\0';
				strcat(candidate, resource_suffix);
				struct stat st;
				if (stat(candidate, &st) == 0 && S_ISDIR(st.st_mode))
					return bstrdup(candidate);
			}
		}
	}

	char *home = getenv("HOME");
	if (home && home[0]) {
		char candidate[PATH_MAX];
		snprintf(candidate, sizeof(candidate), "%s/.local/share/obs/obs-plugins/hypershader/shaders", home);
		blog(LOG_INFO, "[HyperShader] Checking: %s", candidate);
		struct stat st;
		if (stat(candidate, &st) == 0 && S_ISDIR(st.st_mode)) {
			blog(LOG_INFO, "[HyperShader] Found shaders dir: %s", candidate);
			return bstrdup(candidate);
		}

		snprintf(candidate, sizeof(candidate), "%s/.config/obs-studio/plugins/hypershader/data/shaders",
			 home);
		blog(LOG_INFO, "[HyperShader] Checking: %s", candidate);
		if (stat(candidate, &st) == 0 && S_ISDIR(st.st_mode)) {
			blog(LOG_INFO, "[HyperShader] Found shaders dir: %s", candidate);
			return bstrdup(candidate);
		}
	}

	if (module_file && module_file[0]) {
		char root[PATH_MAX];
		strncpy(root, module_file, sizeof(root) - 1);
		root[sizeof(root) - 1] = '\0';

		for (int i = 0; i < 3; i++) {
			char *root_slash = strrchr(root, '/');
			if (!root_slash)
				break;
			*root_slash = '\0';
		}

		if (strlen(root) + strlen("/data/shaders") < PATH_MAX) {
			char candidate[PATH_MAX];
			snprintf(candidate, sizeof(candidate), "%s/data/shaders", root);
			struct stat st;
			if (stat(candidate, &st) == 0 && S_ISDIR(st.st_mode))
				return bstrdup(candidate);
		}
	}

	/* Try standard installed location first */
	char *p = obs_module_file("shaders");
	if (p) {
		struct stat st;
		if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
			return p;
		}
		bfree(p);
	}

	/* Fallback 1: Try finding a known file */
	p = obs_module_file("shaders/template.effect");
	if (p) {
		char *slash = strrchr(p, '/');
		if (slash) {
			*slash = '\0';
			return p; /* caller frees */
		}
		bfree(p);
	}

	/* Fallback 2: Development environment path relative to module data */
	/* Often modules are in build/rundir/../../plugins/hypershader/data */
	p = obs_module_file("../../plugins/hypershader/data/shaders");
	if (p) {
		struct stat st;
		if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
			return p;
		}
		bfree(p);
	}

	return NULL;
}

void hyper_ui_init(void)
{
	/* Deferred initialization: Scan only when first requested by UI */
	blog(LOG_INFO, "[HyperShader] UI initialized (shader scanning deferred)");
}

void hyper_ui_free(void)
{
	pthread_mutex_lock(&global_metadata_mutex);
	free_dynamic_examples_internal();

	// Free user presets
	if (user_preset_names) {
		for (int i = 0; i < user_preset_count; i++) {
			bfree(user_preset_names[i]);
			bfree(user_preset_paths[i]);
			if (user_preset_tags && user_preset_tags[i])
				bfree(user_preset_tags[i]);
		}
		bfree(user_preset_names);
		bfree(user_preset_paths);
		if (user_preset_tags)
			bfree(user_preset_tags);
		user_preset_names = NULL;
		user_preset_paths = NULL;
		user_preset_tags = NULL;
		user_preset_count = 0;
	}
	presets_loaded = false;
	pthread_mutex_unlock(&global_metadata_mutex);
}

char *hyper_ui_get_user_presets_dir(void)
{
	return get_user_presets_dir();
}
