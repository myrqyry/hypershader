#include <obs-module.h>
#include <util/platform.h>
#include <util/darray.h>
#include <util/dstr.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "hypershader_data.h"
#include "../platform_compat.h"

/* From shader_preprocess.c */
extern char *hs_resolve_data_file(const char *path);
extern char *resolve_include_path(const char *include_name, const char *current_file_path);

/* Maximum string lengths for metadata parsing (security/robustness) */
#define META_MAX_LABEL_LEN 256
#define META_MAX_AUDIO_TAG_LEN 64
#define META_MAX_LIST_OPTIONS_LEN 512
#define META_MAX_GROUP_LEN 128
#define META_MAX_VISIBLE_IF_LEN 192
#define META_MAX_DEFAULT_STR_LEN 256
#define META_MAX_TEXT_VALUE_LEN 1024
#define META_MAX_NAME_LEN 128

/* Helper: Parse a quoted string safely with length limits
 * Returns allocated string or NULL. Caller must bfree. */
static char *parse_quoted_string_safe(const char *start, size_t max_len)
{
	if (!start)
		return NULL;

	char *q1 = strchr(start, '"');
	if (!q1)
		return NULL;

	char *q2 = strchr(q1 + 1, '"');
	if (!q2)
		return NULL;

	size_t len = q2 - q1 - 1;
	if (len == 0)
		return NULL;

	if (len > max_len) {
		blog(LOG_WARNING, "[HyperShader] String too long (%zu > %zu), truncating", len, max_len);
		len = max_len;
	}

	char *result = bmalloc(len + 1);
	if (!result)
		return NULL;

	strncpy(result, q1 + 1, len);
	result[len] = '\0';

	for (char *p = result; *p; p++) {
		if (*p < 32 || *p == 127)
			*p = '_';
	}

	return result;
}

void free_shader_metadata(shader_meta_t *da)
{
	if (!da)
		return;

	for (size_t i = 0; i < da->num; i++) {
		bfree(da->array[i].name);
		bfree(da->array[i].label);
		if (da->array[i].audio_tag) {
			bfree(da->array[i].audio_tag);
			da->array[i].audio_tag = NULL;
		}
		if (da->array[i].list_options) {
			bfree(da->array[i].list_options);
			da->array[i].list_options = NULL;
		}
		if (da->array[i].group) {
			bfree(da->array[i].group);
			da->array[i].group = NULL;
		}
		bfree(da->array[i].visible_if);
		da->array[i].visible_if = NULL;
		if (da->array[i].default_val_str) {
			bfree(da->array[i].default_val_str);
			da->array[i].default_val_str = NULL;
		}

		bfree(da->array[i].text_value);
		bfree(da->array[i].text_font);
		if (da->array[i].text_texture) {
			obs_enter_graphics();
			gs_texture_destroy(da->array[i].text_texture);
			obs_leave_graphics();
			da->array[i].text_texture = NULL;
		}
	}
	da_free(*da);
}

/* Metadata include discovery must agree with the shader loader: a quoted
 * #include inside a line/block comment is documentation, not a dependency. */
static char *metadata_include_directive(char *line, bool *in_block_comment)
{
    char *p = line;
    while (*p) {
        if (*in_block_comment) {
            char *end = strstr(p, "*/");
            if (!end)
                return NULL;
            *in_block_comment = false;
            p = end + 2;
        }
        while (*p == ' ' || *p == '\t' || *p == '\r')
            ++p;
        if (p[0] == '/' && p[1] == '/')
            return NULL;
        if (p[0] == '/' && p[1] == '*') {
            *in_block_comment = true;
            p += 2;
            continue;
        }
        if (strncmp(p, "#include", 8) == 0 &&
            (p[8] == ' ' || p[8] == '\t' || p[8] == '"'))
            return p;
        return NULL;
    }
    return NULL;
}

/* Only executable shader references should expose optional texture slots.
 * The old strstr(line, "iChannel0") counted stdlib uniform declarations,
 * comments, and GLSL metadata includes; every Shabon source then falsely
 * claimed active channels even though the shader never samples them.
 * Track block comments across lines and match full identifiers only.
 * Included helper FUNCTIONS still count if they actually reference channels. */
static void metadata_scan_channel_use(const char *line, bool *in_block_comment,
                                      bool *channel_usage)
{
    if (!channel_usage || !line || !in_block_comment)
        return;
    char code[1024];
    size_t out = 0;
    bool quoted = false;
    for (const char *p = line; *p && out < sizeof(code) - 1;) {
        if (*in_block_comment) {
            if (p[0] == '*' && p[1] == '/') {
                *in_block_comment = false;
                p += 2;
            } else {
                p++;
            }
            continue;
        }
        if (!quoted && p[0] == '/' && p[1] == '*') {
            *in_block_comment = true;
            p += 2;
            continue;
        }
        if (!quoted && p[0] == '/' && p[1] == '/')
            break;
        if (*p == '"' && (p == line || p[-1] != '\\')) {
            quoted = !quoted;
            p++;
            continue;
        }
        if (quoted) {
            p++;
            continue;
        }
        code[out++] = *p++;
    }
    code[out] = '\0';
    const char *active = code;
    while (isspace((unsigned char)*active))
        active++;
    /* Uniform declaration or preprocessor text doesn't consume a texture. */
    if (*active == '#' ||
        (strncmp(active, "uniform", 7) == 0 &&
         isspace((unsigned char)active[7])))
        return;
    for (const char *cursor = active; *cursor; cursor++) {
        if (strncmp(cursor, "iChannel", 8) != 0 ||
            cursor[8] < '0' || cursor[8] > '3')
            continue;
        const unsigned char preceding = (cursor == active) ? 0 : (unsigned char)cursor[-1];
        const unsigned char following = (unsigned char)cursor[9];
        if ((isalnum(preceding) || preceding == '_') ||
            (isalnum(following) || following == '_'))
            continue;
        channel_usage[cursor[8] - '0'] = true;
    }
}

void parse_shader_metadata_internal(const char *path, shader_meta_t *da, int depth, bool *channel_usage,
				    struct shader_capabilities *capabilities, char **include_guard,
				    size_t *guard_count, size_t max_guard)
{
	if (depth > 5)
		return;

	char *resolved_path = NULL;
	if (!path || !path[0])
		return;

	if (path[0] != '/' && path[0] != '\\') {
		resolved_path = hs_resolve_data_file(path);
		if (!resolved_path) {
			blog(LOG_WARNING, "[HyperShader] ┌─ Metadata parse skipped");
			blog(LOG_WARNING, "[HyperShader] ├─ Path : %s", path);
			blog(LOG_WARNING, "[HyperShader] └─ Action: could not resolve data path");
			return;
		}
		path = resolved_path;
	}

	char canonical_path[PATH_MAX];
	if (hs_realpath(path, canonical_path) == NULL) {
		strncpy(canonical_path, path, PATH_MAX - 1);
		canonical_path[PATH_MAX - 1] = '\0';
	}

	for (size_t i = 0; i < *guard_count; i++) {
		if (include_guard[i] && strcmp(include_guard[i], canonical_path) == 0) {
			blog(LOG_DEBUG, "[HyperShader] Skipping already included file: %s", canonical_path);
			if (resolved_path)
				bfree(resolved_path);
			return;
		}
	}

	if (*guard_count < max_guard) {
		include_guard[*guard_count] = bstrdup(canonical_path);
		(*guard_count)++;
	} else {
		blog(LOG_WARNING, "[HyperShader] ┌─ Metadata parse skipped");
		blog(LOG_WARNING, "[HyperShader] ├─ File : %s", canonical_path);
		blog(LOG_WARNING, "[HyperShader] └─ Action: include guard limit reached");
		if (resolved_path)
			bfree(resolved_path);
		return;
	}

	blog(LOG_DEBUG, "[HyperShader] Parsing metadata from: %s", canonical_path);
	FILE *f = fopen(canonical_path, "r");
	if (!f) {
		blog(LOG_WARNING, "[HyperShader] ┌─ Metadata parse failed");
		blog(LOG_WARNING, "[HyperShader] ├─ File : %s", canonical_path);
		blog(LOG_WARNING, "[HyperShader] └─ Action: file unreadable");
		if (resolved_path)
			bfree(resolved_path);
		return;
	}

	size_t initial_count = da->num;
	char line[1024];
	char last_comment[1024] = "";
	bool in_include_block_comment = false;
    bool in_channel_block_comment = false;

	while (fgets(line, sizeof(line), f)) {
        metadata_scan_channel_use(line, &in_channel_block_comment, channel_usage);

		char *inc = metadata_include_directive(line, &in_include_block_comment);
		if (inc) {
			char *q1 = strchr(inc, '"');
			if (q1) {
				char *q2 = strchr(q1 + 1, '"');
				if (q2) {
					size_t l = q2 - q1 - 1;
					char *inc_file = bmalloc(l + 1);
					strncpy(inc_file, q1 + 1, l);
					inc_file[l] = '\0';

					char *full_inc = resolve_include_path(inc_file, canonical_path);

					if (full_inc) {
						parse_shader_metadata_internal(full_inc, da, depth + 1, channel_usage, capabilities,
									       include_guard, guard_count, max_guard);
						bfree(full_inc);
					}
					bfree(inc_file);
				}
			}
		}

		char *comment_ptr = strstr(line, "//");
		if (capabilities && comment_ptr) {
			if (strstr(comment_ptr, "@audio"))
				capabilities->audio_required = true;
			if (strstr(comment_ptr, "@mediapipe"))
				capabilities->mediapipe_enabled = true;
			if (strstr(comment_ptr, "@mediapipe_modes"))
				capabilities->mediapipe_has_modes = true;
		}
		if (comment_ptr && strstr(comment_ptr, "@")) {
			if (last_comment[0] != '\0') {
				size_t current_len = strlen(last_comment);
				if (current_len < sizeof(last_comment) - 2) {
					strcat(last_comment, " ");
				}
			}
			size_t space_left = sizeof(last_comment) - strlen(last_comment) - 1;
			strncat(last_comment, comment_ptr + 2, space_left);
			last_comment[sizeof(last_comment) - 1] = '\0';
		}

		char *uni = strstr(line, "uniform");
		if (!uni) {
			bool empty = true;
			for (char *lp = line; *lp && *lp != '\n' && *lp != '\r'; lp++) {
				if (!isspace((unsigned char)*lp)) {
					empty = false;
					break;
				}
			}
			if (!empty && !comment_ptr)
				last_comment[0] = '\0';
			continue;
		}

		const char *comment = last_comment;
		if (comment[0] == '\0')
			continue;

		char *p = uni + 7;
		while (*p && isspace((unsigned char)*p))
			p++;
		while (*p && !isspace((unsigned char)*p))
			p++;
		while (*p && isspace((unsigned char)*p))
			p++;

		char *name_start = p;
		while (*p && *p != ';' && !isspace((unsigned char)*p) && *p != '[' && *p != '=')
			p++;
		size_t namelen = p - name_start;
		if (namelen == 0)
			continue;

		if (namelen > META_MAX_NAME_LEN) {
			blog(LOG_WARNING, "[HyperShader] Uniform name too long (%zu > %d), skipping", namelen,
			     META_MAX_NAME_LEN);
			continue;
		}

		char *name = bmalloc(namelen + 1);
		if (!name) {
			blog(LOG_ERROR, "[HyperShader] Failed to allocate uniform name");
			continue;
		}
		strncpy(name, name_start, namelen);
		name[namelen] = '\0';

		struct shader_param_meta m = {0};
		m.name = name;
		m.min = 0.0f;
		m.max = 1.0f;
		m.step = 0.01f;
		m.has_min = false;
		m.has_max = false;
		m.has_step = false;
		m.audio_gain = 1.0f;
		m.audio_smooth = 0.5f;

		char *c = (char *)comment;
		while (*c) {
			if (*c == '@') {
				if (strncmp(c, "@min", 4) == 0) {
					if (sscanf(c + 4, "%f", &m.min) == 1)
						m.has_min = true;
				} else if (strncmp(c, "@max", 4) == 0) {
					if (sscanf(c + 4, "%f", &m.max) == 1)
						m.has_max = true;
				} else if (strncmp(c, "@step", 5) == 0) {
					if (sscanf(c + 5, "%f", &m.step) == 1)
						m.has_step = true;
				} else if (strncmp(c, "@gain", 5) == 0)
					sscanf(c + 5, "%f", &m.audio_gain);
				else if (strncmp(c, "@smooth", 7) == 0)
					sscanf(c + 7, "%f", &m.audio_smooth);
				else if (strncmp(c, "@label", 6) == 0) {
					if (m.label)
						bfree(m.label);
					m.label = parse_quoted_string_safe(c + 6, META_MAX_LABEL_LEN);
				} else if (strncmp(c, "@audio", 6) == 0) {
					if (capabilities)
						capabilities->audio_required = true;
					if (m.audio_tag)
						bfree(m.audio_tag);
					m.audio_tag = parse_quoted_string_safe(c + 6, META_MAX_AUDIO_TAG_LEN);
				} else if (strncmp(c, "@list", 5) == 0) {
					if (m.list_options)
						bfree(m.list_options);
					m.list_options = parse_quoted_string_safe(c + 5, META_MAX_LIST_OPTIONS_LEN);
				} else if (strncmp(c, "@default", 8) == 0) {
					if (m.default_val_str)
						bfree(m.default_val_str);
					/* A numeric @default may be followed by a quoted @label.
					 * Never consume that unrelated label as a string default. */
					const char *value = c + 8;
					while (*value && isspace((unsigned char)*value))
						value++;
					char *str_default = (*value == '"')
						? parse_quoted_string_safe(value, META_MAX_DEFAULT_STR_LEN)
						: NULL;
					if (str_default) {
						m.default_val_str = str_default;
						m.has_default = true;
                    } else if (strncmp(value, "false", 5) == 0 &&
                               !isalnum((unsigned char)value[5]) && value[5] != '_') {
                        m.default_val = 0.0f;
                        m.has_default = true;
                    } else if (strncmp(value, "true", 4) == 0 &&
                               !isalnum((unsigned char)value[4]) && value[4] != '_') {
                        m.default_val = 1.0f;
                        m.has_default = true;
                    } else {
                        if (sscanf(value, "%f", &m.default_val) == 1)
                            m.has_default = true;
                    }
                } else if (strncmp(c, "@show_if", 8) == 0) {
					bfree(m.visible_if);
					m.visible_if = parse_quoted_string_safe(c + 8, META_MAX_VISIBLE_IF_LEN);
                } else if (strncmp(c, "@group", 6) == 0) {
					if (m.group)
						bfree(m.group);
					m.group = parse_quoted_string_safe(c + 6, META_MAX_GROUP_LEN);
				} else if (strncmp(c, "@dynamics", 9) == 0) {
					char *q1 = strchr(c + 9, '"');
					if (q1) {
						char *q2 = strchr(q1 + 1, '"');
						if (q2) {
							size_t l = q2 - q1 - 1;
							char type_str[32] = {0};
							if (l < sizeof(type_str)) {
								strncpy(type_str, q1 + 1, l);
								type_str[l] = '\0';
								if (strcmp(type_str, "spring") == 0)
									m.dynamics_type = DYNAMICS_SPRING;
								else if (strcmp(type_str, "smooth") == 0)
									m.dynamics_type = DYNAMICS_SMOOTH_DAMP;
								else if (strcmp(type_str, "exp") == 0)
									m.dynamics_type = DYNAMICS_EXPONENTIAL;
							}
						}
					}
					char *freq_ptr = strstr(c, "freq=");
					if (freq_ptr)
						sscanf(freq_ptr + 5, "%f", &m.dynamics_freq);
					char *damp_ptr = strstr(c, "damp=");
					if (damp_ptr)
						sscanf(damp_ptr + 5, "%f", &m.dynamics_damp);
					if (m.dynamics_freq <= 0.0f)
						m.dynamics_freq = 4.0f;
					if (m.dynamics_damp <= 0.0f)
						m.dynamics_damp = 0.7f;
				} else if (strncmp(c, "@text", 5) == 0) {
					m.is_text = true;
					if (m.text_value)
						bfree(m.text_value);
					m.text_value = parse_quoted_string_safe(c + 5, META_MAX_TEXT_VALUE_LEN);
					char *size_ptr = strstr(c, "size=");
					if (size_ptr)
						sscanf(size_ptr + 5, "%f", &m.text_size);
					if (m.text_size <= 0.0f)
						m.text_size = 32.0f;
				}
			}
			c++;
		}
		blog(LOG_DEBUG, "[HyperShader] Parsed metadata: name='%s' label='%s' has_default=%d def='%s'", m.name,
		     m.label ? m.label : "(null)", m.has_default, m.default_val_str ? m.default_val_str : "(scalar)");

		if (da->num + 1 > da->capacity) {
			size_t new_cap = da->capacity ? da->capacity * 2 : 16;
			da->array = brealloc(da->array, sizeof(struct shader_param_meta) * new_cap);
			da->capacity = new_cap;
		}
		da->array[da->num++] = m;

		last_comment[0] = '\0';
	}
	fclose(f);
	blog(LOG_DEBUG, "[HyperShader] Finished parsing %s: found %zu new entries", canonical_path,
	     da->num - initial_count);
	if (resolved_path)
		bfree(resolved_path);
}

void parse_shader_metadata(const char *path, shader_meta_t *da, bool *channel_usage,
				   struct shader_capabilities *capabilities)
{
#define MAX_INCLUDE_GUARD 64
	char *include_guard[MAX_INCLUDE_GUARD];
	for (size_t i = 0; i < MAX_INCLUDE_GUARD; i++)
		include_guard[i] = NULL;
	size_t guard_count = 0;

	parse_shader_metadata_internal(path, da, 0, channel_usage, capabilities, include_guard, &guard_count,
				       MAX_INCLUDE_GUARD);

	for (size_t i = 0; i < guard_count; i++) {
		bfree(include_guard[i]);
	}
}
