#define _GNU_SOURCE
#include <obs-module.h>
#include "effect-hotkeys.h"

#include <graphics/graphics.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <pthread.h>
#include <math.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <float.h>
#include <errno.h>
#include <ctype.h>

#ifdef _WIN32
#include <io.h>
#ifndef PATH_MAX
#define PATH_MAX 260
#endif
#else
#include <unistd.h>
#endif

static inline int hs_access_exists(const char *path)
{
#if defined(_MSC_VER)
	return _access(path, 0);
#else
	return access(path, F_OK);
#endif
}

#ifdef __linux__
#include <X11/Xlib.h>
#endif
/* Audio I/O */
#include <media-io/audio-io.h>

/* Vendored lightweight kissfft wrapper */
#include "kiss_fftr.h"
#include "jsmn.h"
#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif
#include <graphics/image-file.h>
#include "string_utils.h"
#include "platform_compat.h"

#include "hypershader_data.h"
#include "source-selection.h"
#include "interop/sight_client.h"
#include "interop/sight_subject_stats.h"
#include "interop/sight_gpu.h"
#include "interop/readback_fence.h"
#include "interop/sight_flow_step.h"
#include "shader/graph-budget.h"
#include <inttypes.h>

/* Reset logical state without reallocating compatible GPU surfaces. */
static void graph_invalidate_history(struct hypershader_data *s)
{
    s->graph_write_idx = 0;
    s->graph_frame = 0;
    s->graph_flow_sequence = 0;
    s->graph_flow_announced = false;
    s->graph_valid = false;
}

/* Called only by the graphics owner, before a real draw. Requests coalesce
 * while hidden or disabled; no UI callback takes a graphics lock. */
static void consume_history_reset(struct hypershader_data *s)
{
    if (!atomic_exchange_explicit(&s->history_reset_requested, false, memory_order_acq_rel))
        return;
    graph_invalidate_history(s);
    s->history_idx = 0;
    s->history_needs_seed = true;
}

static void request_history_reset_proc(void *data, calldata_t *args)
{
    UNUSED_PARAMETER(args);
    struct hypershader_data *s = data;
    if (s)
        atomic_store_explicit(&s->history_reset_requested, true, memory_order_release);
}

static bool history_reset_pressed(void *data, obs_hotkey_pair_id id, obs_hotkey_t *key, bool pressed)
{
    UNUSED_PARAMETER(id);
    UNUSED_PARAMETER(key);
    struct hypershader_data *s = data;
    if (!pressed || !s)
        return false;
    atomic_store_explicit(&s->history_reset_requested, true, memory_order_release);
    return true;
}

static bool history_cancel_pressed(void *data, obs_hotkey_pair_id id, obs_hotkey_t *key, bool pressed)
{
    UNUSED_PARAMETER(id);
    UNUSED_PARAMETER(key);
    struct hypershader_data *s = data;
    return pressed && s && atomic_exchange_explicit(&s->history_reset_requested, false, memory_order_acq_rel);
}

/* Clear all graph histories after resize, shader switch or teardown.
 * Caller must be inside the OBS graphics context. */
static void graph_reset(struct hypershader_data *s)
{
	for (int buffer = 0; buffer < HS_GRAPH_MAX_PASSES; buffer++) {
		for (int frame = 0; frame < 2; frame++) {
			if (s->graph_history[buffer][frame]) {
				gs_texrender_destroy(s->graph_history[buffer][frame]);
				s->graph_history[buffer][frame] = NULL;
			}
		}
	}
    graph_invalidate_history(s);
	s->graph_width = s->graph_height = 0;
}

/* Release only the graph's render targets. Shared effect, channels and
 * non-graph assets have independent lifetimes. */
static void graph_release_targets(struct hypershader_data *s)
{
	graph_reset(s);
	gs_texrender_t **targets[] = {&s->feedback_input, &s->output_composite_fx,
				       &s->output_composite_base};
	for (unsigned i = 0; i < 3; i++) {
		if (*targets[i]) {
			gs_texrender_destroy(*targets[i]);
			*targets[i] = NULL;
		}
	}
}

/* Read immutable resource descriptors once, on successful shader replacement.
 * Never use mutable controls to allocate resources on the render path. */
static bool graph_read_descriptor(gs_effect_t *effect, const char *name, int fallback, int *out)
{
	*out = fallback;
	gs_eparam_t *param = gs_effect_get_param_by_name(effect, name);
	if (!param)
		return true;
	struct gs_effect_param_info info;
	gs_effect_get_param_info(param, &info);
	/* OBS 32's effect parser stores integer defaults as C long (8 bytes
	 * on LP64, 4 on Windows). Accept both encodings without truncation. */
	const size_t size = gs_effect_get_default_val_size(param);
	if (info.type != GS_SHADER_PARAM_INT || (size != sizeof(int) && size != sizeof(long)))
		return false;
	void *value = gs_effect_get_default_val(param);
	if (!value)
		return false;
	long decoded;
	if (size == sizeof(long)) {
		memcpy(&decoded, value, sizeof(long));
	} else {
		int narrow;
		memcpy(&narrow, value, sizeof(int));
		decoded = narrow;
	}
	bfree(value);
	if (decoded < INT_MIN || decoded > INT_MAX)
		return false;
	*out = (int)decoded;
	return true;
}

static bool graph_param_type(gs_eparam_t *param, enum gs_shader_param_type type, bool required)
{
	if (!param)
		return !required;
	struct gs_effect_param_info info;
	gs_effect_get_param_info(param, &info);
	return info.type == type;
}

static bool graph_valid_id(const char *id, size_t len)
{
	if (!len || len > 24 || id[0] < 'a' || id[0] > 'z')
		return false;
	for (size_t i = 1; i < len; ++i)
		if (!(id[i] >= 'a' && id[i] <= 'z') &&
		    !(id[i] >= '0' && id[i] <= '9') && id[i] != '_')
			return false;
	/* Prevent a resource name from colliding with another's companions. */
	const char *suffixes[] = {"_order", "_previous", "_resolution", "_divisor", "_format", "_transient", "_last_use"};
	for (unsigned i = 0; i < 7; ++i) {
		size_t n = strlen(suffixes[i]);
		if (len >= n && !memcmp(id + len - n, suffixes[i], n))
			return false;
	}
	return true;
}

/* Build and validate the immutable plan from compiled uniforms, not the
 * text parser or mutable OBS settings. Named plans reject before swap. */
static bool graph_configure(gs_effect_t *effect, struct hs_graph_plan *plan,
			    enum hypershader_instance_kind kind)
{
	memset(plan, 0, sizeof(*plan));
	const size_t nparams = gs_effect_get_num_params(effect);
	unsigned occupied = 0;
	for (size_t i = 0; i < nparams; ++i) {
		struct gs_effect_param_info info;
		gs_effect_get_param_info(gs_effect_get_param_by_idx(effect, i), &info);
		if (!info.name || strncmp(info.name, "hs_pass_", 8))
			continue;
		plan->named = plan->enabled = true;
		size_t n = strlen(info.name);
		if (n < 15 || strcmp(info.name + n - 6, "_order"))
			continue;
		const size_t id_len = n - 8 - 6;
		int order;
		if (!graph_valid_id(info.name + 8, id_len) ||
		    !graph_read_descriptor(effect, info.name, -1, &order) ||
		    order < 0 || order >= HS_GRAPH_MAX_PASSES || (occupied & (1U << order)))
			return false;
		occupied |= 1U << order;
		snprintf(plan->resource[order], sizeof(plan->resource[order]), "hs_pass_%.*s", (int)id_len, info.name + 8);
		snprintf(plan->technique[order], sizeof(plan->technique[order]), "Pass_%.*s", (int)id_len, info.name + 8);
		++plan->count;
	}
	if (plan->named) {
		if (kind == HYPER_KIND_TRANSITION || !plan->count ||
		    occupied != (1U << plan->count) - 1 ||
		    gs_effect_get_technique(effect, "BufferA") ||
		    gs_effect_get_technique(effect, "BufferB"))
			return false;
		/* Reject misspelled or orphaned reserved resource uniforms. */
		const char *suffixes[] = {"", "_order", "_previous", "_resolution", "_divisor", "_format", "_transient", "_last_use"};
		for (size_t i = 0; i < nparams; ++i) {
			struct gs_effect_param_info info;
			gs_effect_get_param_info(gs_effect_get_param_by_idx(effect, i), &info);
			if (!info.name || strncmp(info.name, "hs_pass_", 8))
				continue;
			bool known = false;
			for (unsigned j = 0; j < plan->count; ++j)
				for (unsigned k = 0; k < 8; ++k) {
					char name[80];
					snprintf(name, sizeof(name), "%s%s", plan->resource[j], suffixes[k]);
					known |= strcmp(name, info.name) == 0;
				}
			if (!known)
				return false;
		}
	} else {
		plan->enabled = gs_effect_get_technique(effect, "BufferA") &&
			gs_effect_get_technique(effect, "BufferB") &&
			gs_effect_get_technique(effect, "Draw") &&
			gs_effect_get_param_by_name(effect, "hs_buffer_a") &&
			gs_effect_get_param_by_name(effect, "hs_buffer_b") &&
			gs_effect_get_param_by_name(effect, "hs_buffer_a_previous") &&
			gs_effect_get_param_by_name(effect, "hs_buffer_b_previous");
		if (!plan->enabled)
			return true;
		plan->count = 2;
		strcpy(plan->resource[0], "hs_buffer_a");
		strcpy(plan->resource[1], "hs_buffer_b");
		strcpy(plan->technique[0], "BufferA");
		strcpy(plan->technique[1], "BufferB");
	}
	int budget;
	if (!gs_effect_get_technique(effect, "Draw") ||
	    !graph_read_descriptor(effect, "hs_graph_budget_mib", HS_GRAPH_MAX_MIB, &budget) ||
	    budget < 1 || budget > HS_GRAPH_MAX_MIB)
		return false;
	plan->budget_mib = (uint32_t)budget;
	for (unsigned i = 0; i < plan->count; ++i) {
		char name[80];
		int divisor, format;
		snprintf(name, sizeof(name), "%s_divisor", plan->resource[i]);
		if (!graph_read_descriptor(effect, name, 1, &divisor) ||
		    (divisor != 1 && divisor != 2 && divisor != 4 && divisor != 8))
			goto bad_descriptor;
		snprintf(name, sizeof(name), "%s_format", plan->resource[i]);
		if (!graph_read_descriptor(effect, name, 0, &format) || (format != 0 && format != 1))
			goto bad_descriptor;
		plan->divisor[i] = (uint32_t)divisor;
		plan->float16[i] = format == 1;
        int transient=0, last_use=(int)plan->count;
        if (plan->named) {
            snprintf(name,sizeof(name),"%s_transient",plan->resource[i]);
            if (!graph_read_descriptor(effect,name,0,&transient) ||
                (transient!=0 && transient!=1))
                goto bad_descriptor;
            snprintf(name,sizeof(name),"%s_last_use",plan->resource[i]);
            if ((!transient && gs_effect_get_param_by_name(effect,name)) ||
                !graph_read_descriptor(effect,name,(int)plan->count,&last_use) ||
                last_use<(int)i || last_use>(int)plan->count)
                goto bad_descriptor;
        }
        plan->transient[i]=transient!=0;
        plan->last_use[i]=(unsigned)last_use;

		plan->current[i] = gs_effect_get_param_by_name(effect, plan->resource[i]);
		snprintf(name, sizeof(name), "%s_previous", plan->resource[i]);
		plan->previous[i] = gs_effect_get_param_by_name(effect, name);
		snprintf(name, sizeof(name), "%s_resolution", plan->resource[i]);
		plan->resolution[i] = gs_effect_get_param_by_name(effect, name);
		if (!gs_effect_get_technique(effect, plan->technique[i]) ||
		    !graph_param_type(plan->current[i], GS_SHADER_PARAM_TEXTURE, true) ||
		    (plan->transient[i] ? plan->previous[i]!=NULL :
             !graph_param_type(plan->previous[i], GS_SHADER_PARAM_TEXTURE, true)) ||
		    !graph_param_type(plan->resolution[i], GS_SHADER_PARAM_VEC3, false))
			goto bad_descriptor;
		continue;
bad_descriptor:
		if (!plan->named)
			blog(LOG_ERROR, "[hypershader] Invalid graph buffer %c descriptor", 'A' + i);
		return false;
	}
	plan->output_resolution = gs_effect_get_param_by_name(effect, "hs_graph_output_resolution");
	return graph_param_type(plan->output_resolution, GS_SHADER_PARAM_VEC3, false) &&
        hs_graph_plan_slots(plan->count,plan->divisor,plan->float16,plan->transient,plan->last_use,plan->owner);
}

static void graph_set_resolution(gs_eparam_t *param, uint32_t width, uint32_t height)
{
	if (param) {
		struct vec3 resolution;
		vec3_set(&resolution, (float)width, (float)height,
			 height ? (float)width / (float)height : 1.0f);
		gs_effect_set_vec3(param, &resolution);
	}
}

static void graph_bind_resolutions(struct hypershader_data *s, uint32_t width, uint32_t height)
{
	for (unsigned i = 0; i < s->graph.count; ++i)
		graph_set_resolution(s->graph.resolution[i],
				     hs_graph_extent(width, s->graph.divisor[i]),
				     hs_graph_extent(height, s->graph.divisor[i]));
	graph_set_resolution(s->graph.output_resolution, width, height);
}

/* Helper to convert float array to vec4 */
static inline void vec4_from_float4(struct vec4 *dst, float *src)
{
	if (dst && src) {
		dst->x = src[0];
		dst->y = src[1];
		dst->z = src[2];
		dst->w = src[3];
	}
}

#include "audio/audio_processor.h"
#include "hyper_audio.h"
#include "hyper_ui.h"
#include "filter_callbacks.h"
#include "scene/scene_bridge.h"
#include "dev/shader_watcher.h"

#include "mediapipe/mp_wrapper.h"

char *hs_resolve_data_file(const char *path);

/* Task data for UI thread */
static void handle_hot_reload_update_task(void *param)
{
	obs_source_t *context = param;
	if (!context)
		return;

	obs_data_t *settings = obs_source_get_settings(context);
	if (!settings)
		return;

	obs_source_update(context, settings);
	obs_data_release(settings);
	obs_source_release(context);
}

/* Deferred property refresh callback - scheduled on UI thread to avoid re-entrancy.
 * Caller must have incremented refcount via obs_source_get_ref before queuing. */
static void deferred_properties_refresh(void *param)
{
	obs_source_t *source = param;
	if (source) {
		blog(LOG_INFO, "[hypershader] Deferred property refresh executing");
		obs_source_update_properties(source);
		obs_source_release(source);
	}
}

/* Check dirty flag and refresh after callback stack unwinds (Rule 5-safe path).
 * Must be called from graphics/render thread only - delegates to UI thread. */
static void check_and_refresh_ui(struct hypershader_data *s)
{
	if (!s || !s->context)
		return;

	bool expected = true;
	/* Use atomic exchange to ensure we only refresh once per update */
	if (atomic_compare_exchange_strong(&s->needs_properties_refresh, &expected, false)) {
		/* Defer to UI thread - never call obs_source_update_properties from render path */
		obs_source_get_ref(s->context);
		obs_queue_task(OBS_TASK_UI, deferred_properties_refresh, s->context, false);
	}
}

static void schedule_property_refresh(struct hypershader_data *s)
{
	/* Set the atomic flag directly - the queue is no longer needed */
	atomic_store(&s->needs_properties_refresh, true);
}

/* Return normalized global cursor position in [0,1] for X and Y, or false if unsupported */
static bool get_global_cursor_pos(struct hypershader_data *s, float *out_x, float *out_y)
{
#ifdef __linux__
	Display *dpy = s->x11_display;
	if (!dpy)
		return false;

	Window root = DefaultRootWindow(dpy);
	Window ret_root, ret_child;
	int root_x, root_y, win_x, win_y;
	unsigned int mask;
	Bool ok = XQueryPointer(dpy, root, &ret_root, &ret_child, &root_x, &root_y, &win_x, &win_y, &mask);
	if (!ok) {
		return false;
	}

	int screen = DefaultScreen(dpy);
	int sw = DisplayWidth(dpy, screen);
	int sh = DisplayHeight(dpy, screen);

	if (sw > 0 && sh > 0) {
		*out_x = (float)root_x / (float)sw;
		*out_y = (float)root_y / (float)sh;
		return true;
	}
	return false;
#else
	(void)s;
	(void)out_x;
	(void)out_y;
	return false;
#endif
}

/* ===== Audio feature computation (small DFT-based 8-band) ===== */

/* Compute RMS and 8 band energies from buffer (mono) */

/* ============================================================================
 * REFCOUNTED METADATA MANAGEMENT
 * Thread-safe metadata lifecycle for property refresh without leaks
 * ============================================================================ */

/* Forward declaration for free_shader_metadata */

static struct shader_meta_rc *metadata_rc_create(void)
{
	struct shader_meta_rc *rc = bzalloc(sizeof(*rc));
	if (!rc)
		return NULL;
	atomic_store(&rc->refcount, 1);
	memset(&rc->meta, 0, sizeof(rc->meta));
	blog(LOG_DEBUG, "[hypershader] Created metadata_rc %p (refcount=1)", rc);
	return rc;
}

void metadata_rc_retain(struct shader_meta_rc *rc)
{
	if (rc) {
		int old = atomic_fetch_add(&rc->refcount, 1);
		blog(LOG_DEBUG, "[hypershader] Retained metadata_rc %p (refcount %d -> %d)", (void *)rc, old, old + 1);
	}
}

void metadata_rc_release(struct shader_meta_rc **rc_ptr)
{
	if (!rc_ptr || !*rc_ptr)
		return;

	struct shader_meta_rc *rc = *rc_ptr;
	int old_count = atomic_fetch_sub(&rc->refcount, 1);

	blog(LOG_DEBUG, "[hypershader] Released metadata_rc %p (refcount %d -> %d)", (void *)rc, old_count,
	     old_count - 1);

	if (old_count == 1) {
		/* Last reference - safe to free */
		blog(LOG_DEBUG, "[hypershader] Freeing metadata_rc %p (refcount=0)", (void *)rc);
		free_shader_metadata(&rc->meta);
		bfree(rc);
	}

	*rc_ptr = NULL;
}

/* ============================================================================
 * INCLUDE GUARD TRACKING
 * Prevents circular includes in shader preprocessor
 * ============================================================================ */

static void include_guard_init(struct include_guard *guard)
{
	guard->paths = NULL;
	guard->count = 0;
	guard->capacity = 0;
}

static void include_guard_free(struct include_guard *guard)
{
	if (guard->paths) {
		for (size_t i = 0; i < guard->count; i++) {
			bfree(guard->paths[i]);
		}
		bfree(guard->paths);
	}
	memset(guard, 0, sizeof(*guard));
}

static bool include_guard_contains(struct include_guard *guard, const char *path)
{
	for (size_t i = 0; i < guard->count; i++) {
		if (strcmp(guard->paths[i], path) == 0)
			return true;
	}
	return false;
}

static void include_guard_add(struct include_guard *guard, const char *path)
{
	if (guard->count >= guard->capacity) {
		size_t new_cap = guard->capacity == 0 ? 8 : guard->capacity * 2;
		guard->paths = brealloc(guard->paths, sizeof(char *) * new_cap);
		guard->capacity = new_cap;
	}
	guard->paths[guard->count++] = bstrdup(path);
}

/* ============================================================================
 * GLSL TO OBS TRANSLATION
 * OBS doesn't have a C preprocessor, so #define macros don't work.
 * We perform the GLSL->HLSL type/function translations ourselves.
 * ============================================================================ */

/* Safer string replacement using dstr would be ideal, but let's make this manual version robust first */
static char *replace_word(const char *source, const char *find, const char *replace)
{
	if (!source || !find || !replace)
		return NULL;

	struct dstr buffer = {0};
	size_t find_len = strlen(find);
	const char *p = source;
	const char *next_match;

	while ((next_match = strstr(p, find)) != NULL) {
		/* Check word boundary */
		bool start_ok = (next_match == source) ||
				(!isalnum((unsigned char)*(next_match - 1)) && *(next_match - 1) != '_');
		bool end_ok = !isalnum((unsigned char)*(next_match + find_len)) && *(next_match + find_len) != '_';

		if (start_ok && end_ok) {
			/* Append everything before match */
			dstr_ncat(&buffer, p, next_match - p);
			/* Append replacement */
			dstr_cat(&buffer, replace);
			/* Advance */
			p = next_match + find_len;
		} else {
			/* Append everything up to and including the "false alarm" match start */
			dstr_ncat(&buffer, p, (next_match - p) + 1);
			p = next_match + 1;
		}
	}

	/* Append remainder */
	dstr_cat(&buffer, p);

	if (!buffer.array)
		return bstrdup("");

	return buffer.array; /* Caller must bfree */
}

/* Perform GLSL -> OBS HLSL translations */
/* Translate code tokens without rewriting C preprocessor directives.
 * Translating '#define vec2 float2' into '#define float2 float2' caused
 * self-referential definitions and crashed the OBS effect compiler. */
static char *translate_glsl_to_obs(char *source)
{
    if (!source)
        return NULL;

    static const char *translations[][2] = {
        {"vec2", "float2"}, {"vec3", "float3"}, {"vec4", "float4"},
        {"ivec2", "int2"}, {"ivec3", "int3"}, {"ivec4", "int4"},
        {"mat2", "float2x2"}, {"mat3", "float3x3"}, {"mat4", "float4x4"},
        {"sampler2D", "texture2d"},
        {"fract", "frac"}, {"mix", "lerp"}, {"mod", "fmod"},
        {NULL, NULL}
    };
    struct dstr output = {0};
    const char *line = source;
    while (*line) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        const char *trimmed = line;
        while (trimmed < line + length && (*trimmed == ' ' || *trimmed == '\t'))
            ++trimmed;

        if (trimmed < line + length && *trimmed == '#') {
            dstr_ncat(&output, line, length);
        } else {
            char *current = bmalloc(length + 1);
            memcpy(current, line, length);
            current[length] = '\0';
            for (int i = 0; translations[i][0]; ++i) {
                char *translated = replace_word(current, translations[i][0], translations[i][1]);
                if (!translated) {
                    bfree(current);
                    dstr_free(&output);
                    return NULL;
                }
                bfree(current);
                current = translated;
            }
            dstr_cat(&output, current);
            bfree(current);
        }
        dstr_cat(&output, "\n");
        line = end ? end + 1 : line + length;
    }
    return output.array ? output.array : bstrdup("");
}

/* Boilerplate expansion for SHADERTOY_MAINIMAGE_WRAPPER / HYPERSHADER_PIXEL_WRAPPER.
 * OBS effect compiler does not expand C preprocessor macros, so we emit the
 * PSMain entry point and technique block directly. */
static const char *WRAPPER_EXPANSION = "float4 PSMain(VertData v_in) : TARGET {\n"
				       "    float2 fragCoord = v_in.uv * hs_resolution.xy;\n"
				       "    float4 fragColor;\n"
				       "    hs_shade_pixel(fragColor, fragCoord);\n"
				       "    return fragColor;\n"
				       "}\n"
				       "technique Draw {\n"
				       "    pass {\n"
				       "        vertex_shader = VSDefault(v_in);\n"
				       "        pixel_shader  = PSMain(v_in);\n"
				       "    }\n"
				       "}\n";

/* Match a directive only at the beginning of an active source line.
 * Comments (including multiline comments) and quoted strings must never
 * inject dependencies into the shader graph. */
static const char *effect_active_line_start(const char *line, size_t len, bool *in_block_comment)
{
    const char *p = line;
    const char *end = line + len;
    while (p < end) {
        if (*in_block_comment) {
            const char *close = NULL;
            for (const char *it = p; it + 1 < end; ++it) {
                if (it[0] == '*' && it[1] == '/') {
                    close = it + 2;
                    break;
                }
            }
            if (!close)
                return NULL;
            *in_block_comment = false;
            p = close;
        }
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r'))
            ++p;
        if (p >= end || (p + 1 < end && p[0] == '/' && p[1] == '/'))
            return NULL;
        if (p + 1 < end && p[0] == '/' && p[1] == '*') {
            *in_block_comment = true;
            p += 2;
            continue;
        }
        return p;
    }
    return NULL;
}

static const char *effect_include_directive(const char *line, size_t len, bool *in_block_comment)
{
    const char *p = effect_active_line_start(line, len, in_block_comment);
    const char *end = line + len;
    return p && end - p >= 8 && strncmp(p, "#include", 8) == 0 &&
        (p + 8 == end || p[8] == ' ' || p[8] == '\t' || p[8] == '"') ? p : NULL;
}

static bool shader_has_include_directive(const char *source)
{
    if (!source)
        return false;
    bool block = false;
    const char *line = source;
    while (*line) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        if (effect_include_directive(line, len, &block))
            return true;
        line = end ? end + 1 : line + len;
    }
    return false;
}

/* Only a complete macro-invocation line can introduce the PSMain and Draw
 * technique. Replacing tokens inside comments or #define source produced
 * duplicate shader code and an invalid OBS effect on real GPUs. */
static char *expand_pixel_wrapper_statements(const char *source)
{
    if (!source)
        return NULL;
    struct dstr out = {0};
    unsigned replacements = 0;
    bool in_block_comment = false;
    const char *line = source;
    while (*line) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        const char *start = effect_active_line_start(line, len, &in_block_comment);
        if (!start)
            start = line + len;
        const char *tail = NULL;
        const char *a = "HYPERSHADER_PIXEL_WRAPPER";
        const char *b = "SHADERTOY_MAINIMAGE_WRAPPER";
        size_t a_len = strlen(a), b_len = strlen(b);
        if (line + len - start >= (ptrdiff_t)a_len && strncmp(start, a, a_len) == 0)
            tail = start + a_len;
        else if (line + len - start >= (ptrdiff_t)b_len && strncmp(start, b, b_len) == 0)
            tail = start + b_len;
        bool invocation = tail != NULL;
        if (invocation) {
            while (tail < line + len && (*tail == ' ' || *tail == '\t' || *tail == '\r'))
                ++tail;
            if (tail < line + len && *tail == ';')
                ++tail;
            while (tail < line + len && (*tail == ' ' || *tail == '\t' || *tail == '\r'))
                ++tail;
            if (tail != line + len && !(line + len - tail >= 2 && tail[0] == '/' && tail[1] == '/'))
                invocation = false;
        }
        if (invocation) {
            if (++replacements > 1) {
                blog(LOG_ERROR, "[hypershader] More than one hs_shade_pixel wrapper invocation");
                dstr_free(&out);
                return NULL;
            }
            dstr_cat(&out, WRAPPER_EXPANSION);
        } else {
            dstr_ncat(&out, line, len);
            dstr_cat(&out, "\n");
        }
        line = end ? end + 1 : line + len;
    }
    return out.array ? out.array : bstrdup("");
}

/* ============================================================================
 * SHADER PREPROCESSOR
 * Resolves #include directives that OBS effect system doesn't support natively
 * ============================================================================ */

/* Forward declaration for recursive processing with include guard */
static char *preprocess_shader_with_guard(const char *path, int depth, struct include_guard *guard);

static bool hs_stat_dir(const char *path)
{
	struct stat st;
	return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static char *hs_plugin_data_path(void)
{
	const char *obs_data_path = obs_get_module_data_path(obs_current_module());
	if (hs_stat_dir(obs_data_path))
		return bstrdup(obs_data_path);

	const char *module_file = obs_get_module_file_name(obs_current_module());
	if (module_file && module_file[0]) {
		char module_dir[PATH_MAX];
		strncpy(module_dir, module_file, sizeof(module_dir) - 1);
		module_dir[sizeof(module_dir) - 1] = '\0';

		char *slash = strrchr(module_dir, '/');
		if (slash) {
			*slash = '\0';
			if (strlen(module_dir) + strlen("/hypershader") < PATH_MAX) {
				char candidate[PATH_MAX];
				snprintf(candidate, sizeof(candidate), "%s/hypershader", module_dir);
				if (hs_stat_dir(candidate))
					return bstrdup(candidate);
			}
		}

		const char *lib_segment = strstr(module_file, "/lib/");
		if (lib_segment) {
			size_t root_len = (size_t)(lib_segment - module_file);
			const char *resource_suffix = "/share/obs/obs-plugins/hypershader";
			if (root_len + strlen(resource_suffix) < PATH_MAX) {
				char candidate[PATH_MAX];
				memcpy(candidate, module_file, root_len);
				candidate[root_len] = '\0';
				strcat(candidate, resource_suffix);
				if (hs_stat_dir(candidate))
					return bstrdup(candidate);
			}
		}
	}

	char *home = getenv("HOME");
	if (home && home[0]) {
		char candidate[PATH_MAX];
		snprintf(candidate, sizeof(candidate), "%s/.local/share/obs/obs-plugins/hypershader", home);
		if (hs_stat_dir(candidate))
			return bstrdup(candidate);

		snprintf(candidate, sizeof(candidate), "%s/.config/obs-studio/plugins/hypershader/data", home);
		if (hs_stat_dir(candidate))
			return bstrdup(candidate);

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

			if (strlen(root) + strlen("/data") < PATH_MAX) {
				char candidate[PATH_MAX];
				snprintf(candidate, sizeof(candidate), "%s/data", root);
				if (hs_stat_dir(candidate))
					return bstrdup(candidate);
			}
		}
	}

	return NULL;
}

static bool hs_path_in_data_dir(const char *path, const char *data_path)
{
	if (!path || !data_path)
		return false;

	size_t data_path_len = strlen(data_path);
	while (data_path_len > 0 && data_path[data_path_len - 1] == '/')
		data_path_len--;

	return strncmp(path, data_path, data_path_len) == 0 &&
	       (path[data_path_len] == '\0' || path[data_path_len] == '/');
}

/* Managed editor revisions are intentionally distinct from installed assets.
 * Only UUID-named .effect files in this per-user directory are accepted. */
static char *hs_managed_shader_dir(void)
{
	obs_module_t *module = obs_get_module("hypershader");
	return module ? obs_module_get_config_path(module, "builder-applied") : NULL;
}

static bool hs_path_in_managed_dir(const char *path)
{
	char *dir = hs_managed_shader_dir();
	char canonical_dir[PATH_MAX], canonical_file[PATH_MAX];
	bool allowed = dir && hs_realpath(dir, canonical_dir) && path &&
		       hs_realpath(path, canonical_file) && hs_path_in_data_dir(canonical_file, canonical_dir);
	bfree(dir);
	return allowed;
}

static char *hs_managed_shader_path(const char *path)
{
	static const char prefix[] = "managed/builder-applied/";
	if (!path || strncmp(path, prefix, sizeof(prefix) - 1) != 0)
		return NULL;
	const char *name = path + sizeof(prefix) - 1;
	/* The dock uses UUIDs; refuse separators, traversal, and arbitrary files. */
	if (strlen(name) != 43 || strcmp(name + 36, ".effect") != 0)
		return NULL;
	for (size_t i = 0; i < 36; i++) {
		char c = name[i];
		if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f') && c != '-')
			return NULL;
	}
	char *dir = hs_managed_shader_dir();
	if (!dir)
		return NULL;
	struct dstr full = {0};
	dstr_printf(&full, "%s/%s", dir, name);
	bfree(dir);
	if (!hs_path_in_managed_dir(full.array)) {
		dstr_free(&full);
		return NULL;
	}
	return full.array;
}

char *hs_resolve_data_file(const char *path)
{
	if (!path || !path[0])
		return NULL;

	if (strncmp(path, "managed/", 8) == 0)
		return hs_managed_shader_path(path);

	if (path[0] == '/' || path[0] == '\\') {
		if (hs_access_exists(path) == 0)
			return bstrdup(path);
		return NULL;
	}

	char *data_path = hs_plugin_data_path();
	if (data_path) {
		size_t data_len = strlen(data_path);
		size_t path_len = strlen(path);
		if (data_len < PATH_MAX && path_len < PATH_MAX && data_len + 1 + path_len < PATH_MAX) {
			char candidate[PATH_MAX];
			memcpy(candidate, data_path, data_len);
			candidate[data_len] = '/';
			memcpy(candidate + data_len + 1, path, path_len + 1);
			if (hs_access_exists(candidate) == 0) {
				bfree(data_path);
				return bstrdup(candidate);
			}
		}
		bfree(data_path);
	}

	return obs_module_file(path);
}

/* Validate shader file path for security (prevents directory traversal) */
static bool validate_shader_path_with_log(const char *path, const char *caller)
{
	if (!path || !path[0]) {
		blog(LOG_ERROR, "[hypershader] %s: Empty or NULL path provided", caller);
		return false;
	}

	/* Check path length */
	if (strlen(path) > PATH_MAX - 1) {
		blog(LOG_ERROR, "[hypershader] %s: Path too long: %s", caller, path);
		return false;
	}

	/* Check for null bytes (security) */
	if (strchr(path, '\0') != NULL && strlen(path) != (size_t)(strchr(path, '\0') - path)) {
		blog(LOG_ERROR, "[hypershader] %s: Null byte detected in path: %s", caller, path);
		return false;
	}

	/* Get plugin data path for security validation */
	char *data_path = hs_plugin_data_path();

	/* Canonicalize path and validate it's within plugin data directory */
	char canonical[PATH_MAX];
	if (hs_realpath(path, canonical) == NULL) {
		/* If realpath fails (file doesn't exist yet), try to resolve relative to data directory */
		if (path[0] != '/' && data_path) {
			char resolved_path[PATH_MAX];
			snprintf(resolved_path, sizeof(resolved_path), "%s/%s", data_path, path);
			if (hs_realpath(resolved_path, canonical) == NULL) {
				bfree(data_path);
				goto basic_checks;
			}
		} else if (path[0] == '/') {
			/* Absolute path that doesn't exist — reject, don't fall through to permissive checks */
			blog(LOG_ERROR, "[hypershader] %s: Absolute path does not exist: %s", caller, path);
			bfree(data_path);
			return false;
		} else {
			bfree(data_path);
			goto basic_checks;
		}
	}

	/* Security: ensure resolved path is within data directory */
	if (hs_path_in_data_dir(canonical, data_path) || hs_path_in_managed_dir(canonical)) {
		bfree(data_path);
		goto file_checks;
	} else {
		blog(LOG_ERROR, "[hypershader] SECURITY: %s: Path outside data directory rejected: %s", caller,
		     canonical);
		bfree(data_path);
		return false;
	}

basic_checks:
	/* Fallback: Check for directory traversal attempts in string */
	if (strstr(path, "..") != NULL) {
		blog(LOG_ERROR, "[hypershader] %s: Directory traversal detected in path: %s", caller, path);
		return false;
	}

file_checks:
	; /* C17 requires a statement before a declaration after a label. */
	/* Check file extension (basic validation) */
	const char *ext = strrchr(path, '.');
	if (!ext || (strcmp(ext, ".effect") != 0 && strcmp(ext, ".glsl") != 0 && strcmp(ext, ".frag") != 0)) {
		blog(LOG_WARNING, "[hypershader] %s: Unusual file extension in path: %s", caller, path);
		/* Allow but warn - some shaders might not have standard extensions */
	}

	blog(LOG_DEBUG, "[hypershader] %s: Path validation passed: %s", caller, path);
	return true;
}

/* Read entire file into memory (caller must bfree) */
#define HS_MAX_SHADER_FILE_SIZE (4 * 1024 * 1024)
static char *read_file_contents(const char *path)
{
	/* Get plugin data path for security validation */
	char *data_path = hs_plugin_data_path();

	/* Both installed shaders and isolated user revisions are valid roots,
	 * but every absolute path must pass canonical security validation. */
	bool is_absolute_allowed = path[0] == '/' &&
		(hs_path_in_data_dir(path, data_path) || hs_path_in_managed_dir(path));

	if (!validate_shader_path_with_log(path, "read_file_contents")) {
		bfree(data_path);
		return NULL;
	}
	bfree(data_path);

	/* Resolve relative path to absolute path using plugin data directory */
	char *resolved_path = NULL;
	if (path[0] != '/' || is_absolute_allowed) {
		if (path[0] != '/') {
			/* Relative path */
			resolved_path = hs_resolve_data_file(path);
			if (!resolved_path) {
				blog(LOG_ERROR, "[hypershader] ┌─ Path resolve failed");
				blog(LOG_ERROR, "[hypershader] └─ Path : %s", path);
				return NULL;
			}
		} else {
			/* Absolute path within data directory - use as-is */
			resolved_path = bstrdup(path);
		}
	} else {
		/* Absolute path outside data directory - reject for security */
		blog(LOG_ERROR, "[hypershader] SECURITY: Absolute path outside data directory rejected: %s", path);
		return NULL;
	}

	FILE *f = fopen(resolved_path, "rb");
	if (!f) {
		blog(LOG_ERROR, "[hypershader] ┌─ File open failed");
		blog(LOG_ERROR, "[hypershader] ├─ File : %s", resolved_path);
		blog(LOG_ERROR, "[hypershader] └─ Detail: %s", strerror(errno));
		bfree(resolved_path);
		return NULL;
	}

	/* Get file size using fstat (more robust than ftell) */
	int fd = fileno(f);
	struct stat st;
	if (fstat(fd, &st) != 0) {
		blog(LOG_ERROR, "[hypershader] ┌─ File stat failed");
		blog(LOG_ERROR, "[hypershader] └─ Detail: %s", strerror(errno));
		fclose(f);
		bfree(resolved_path);
		return NULL;
	}

	off_t size = st.st_size;
	if (size <= 0) {
		blog(LOG_WARNING, "[hypershader] File size out of range: %ld bytes", (long)size);
		fclose(f);
		bfree(resolved_path);
		return NULL;
	}
	if (size > HS_MAX_SHADER_FILE_SIZE) {
		blog(LOG_WARNING, "[hypershader] File too large: %ld bytes (max %d)", (long)size,
		     HS_MAX_SHADER_FILE_SIZE);
		fclose(f);
		bfree(resolved_path);
		return NULL;
	}

	char *buf = bmalloc(size + 1);
	if (!buf) {
		blog(LOG_ERROR, "[hypershader] Memory allocation failed for file buffer");
		fclose(f);
		bfree(resolved_path);
		return NULL;
	}

	size_t read = fread(buf, 1, size, f);
	fclose(f);

	if (read != (size_t)size) {
		blog(LOG_WARNING, "[hypershader] Partial read: expected %zu, got %zu", (size_t)size, read);
	}

	buf[read] = '\0';
	bfree(resolved_path);
	return buf;
}

/* Get directory from file path (caller must bfree) */
static char *get_dir_from_path(const char *path)
{
	if (!path)
		return NULL;
	char *dir = bstrdup(path);
	char *slash = strrchr(dir, '/');
	if (slash) {
		*slash = '\0';
	} else {
		bfree(dir);
		return bstrdup(".");
	}
	return dir;
}

/* Normalize path (resolve .. and . components) - simple version */
static char *normalize_path(const char *base_dir, const char *include_path)
{
	if (!base_dir || !include_path)
		return NULL;

	/* Build combined path */
	size_t combined_len = strlen(base_dir) + 1 + strlen(include_path) + 1;
	if (combined_len > PATH_MAX)
		return NULL;

	char *combined = bmalloc(combined_len);
	snprintf(combined, combined_len, "%s/%s", base_dir, include_path);

	/* Use realpath if available for proper normalization */
	char *resolved = bmalloc(PATH_MAX);
	if (hs_realpath(combined, resolved) == NULL) {
		/* realpath failed (e.g. file doesn't exist yet), return combined path */
		bfree(resolved);
		return combined;
	}

	bfree(combined);
	return resolved;
}

/* Resolve include path with proper search order and security validation */
char *resolve_include_path(const char *include_name, const char *current_file_path)
{
	char resolved_path[PATH_MAX];
	char canonical[PATH_MAX];

	if (!include_name)
		return NULL;

	/* P1 FIX: Normalize relative paths: ../../stdlib/X.glsl -> stdlib/X.glsl */
	const char *cleaned_include = include_name;
	while (strncmp(cleaned_include, "../", 3) == 0 || strncmp(cleaned_include, "..\\", 3) == 0) {
		cleaned_include += 3;
	}

	/* Try stdlib directory first (most common and reliable) */
	char stdlib_path_prio[PATH_MAX];
	/* If it already starts with stdlib/, use as is, otherwise prepend */
	if (strncmp(cleaned_include, "stdlib/", 7) == 0) {
		snprintf(stdlib_path_prio, sizeof(stdlib_path_prio), "%s", cleaned_include);
	} else {
		snprintf(stdlib_path_prio, sizeof(stdlib_path_prio), "stdlib/%s", cleaned_include);
	}

	char *full_stdlib_path_prio = hs_resolve_data_file(stdlib_path_prio);
	if (full_stdlib_path_prio) {
		if (hs_access_exists(full_stdlib_path_prio) == 0) {
			/* blog(LOG_DEBUG, "[hypershader] Resolved include (stdlib): %s -> %s", include_name, full_stdlib_path); */
			return full_stdlib_path_prio;
		}
		bfree(full_stdlib_path_prio);
	}

	/* 1. Try relative to current file's directory */
	char *data_path = hs_plugin_data_path();

	if (current_file_path) {
		char current_dir[PATH_MAX];
		strncpy(current_dir, current_file_path, PATH_MAX - 1);
		current_dir[PATH_MAX - 1] = '\0';

		char *last_slash = strrchr(current_dir, '/');
		if (last_slash) {
			*last_slash = '\0'; /* Remove filename, keep directory */

			/* Use dstr to avoid snprintf truncation warning */
			struct dstr resolved = {0};
			dstr_printf(&resolved, "%s/%s", current_dir, include_name);

			if (resolved.len > 0 && resolved.len < PATH_MAX) {
				strncpy(resolved_path, resolved.array, sizeof(resolved_path) - 1);
				resolved_path[sizeof(resolved_path) - 1] = '\0';
			} else {
				blog(LOG_WARNING, "[hypershader] Include path too long");
				resolved_path[0] = '\0';
			}
			dstr_free(&resolved);

			/* Canonicalize and validate path */
			if (resolved_path[0] != '\0' && hs_realpath(resolved_path, canonical) != NULL) {
				/* Security: ensure resolved path is within data directory */
				if (hs_path_in_data_dir(canonical, data_path) || hs_path_in_managed_dir(canonical)) {
					bfree(data_path);
					return bstrdup(canonical);
				} else {
					blog(LOG_ERROR, "[hypershader] SECURITY: Include outside data dir rejected: %s",
					     canonical);
				}
			}
		}
	}

	/* 2. Try stdlib directory (implicitly safe - managed by plugin) */
	char stdlib_path[PATH_MAX];
	snprintf(stdlib_path, sizeof(stdlib_path), "stdlib/%s", include_name);
	char *full_stdlib_path = hs_resolve_data_file(stdlib_path);
	if (full_stdlib_path) {
		/* Check if file exists */
		if (hs_access_exists(full_stdlib_path) == 0) {
			bfree(data_path);
			return full_stdlib_path; /* Already allocated by obs_module_file */
		}
		bfree(full_stdlib_path);
	}

	/* 3. Try root data directory (fallback for legacy includes) */
	char *root_data_file = hs_resolve_data_file(include_name);
	if (root_data_file) {
		if (hs_access_exists(root_data_file) == 0) {
			bfree(data_path);
			return root_data_file; /* Already allocated by obs_module_file */
		}
		bfree(root_data_file);
	}

	/* 4. Reject absolute paths - they should go through stdlib */
	if (include_name[0] == '/') {
		blog(LOG_ERROR, "[hypershader] SECURITY: Absolute path includes not allowed: %s", include_name);
	}

	/* Log helpful message for debugging include failures */
	blog(LOG_WARNING, "[hypershader] ┌─ Include missing");
	blog(LOG_WARNING, "[hypershader] ├─ Name : %s", include_name);
	blog(LOG_WARNING, "[hypershader] └─ Search: current dir, stdlib/, data root");

	bfree(data_path);
	return NULL; /* Not found */
}

/* Process shader file and inline all #include directives (with include guard) */
static char *preprocess_shader_with_guard(const char *path, int depth, struct include_guard *guard)
{
	if (depth > 10) {
		blog(LOG_ERROR, "[hypershader] Include depth exceeded (max=10)");
		return NULL;
	}

	char canonical_path[PATH_MAX];
	if (hs_realpath(path, canonical_path) == NULL) {
		strncpy(canonical_path, path, PATH_MAX - 1);
		canonical_path[PATH_MAX - 1] = '\0';
	}

	/* Check for circular include using the guard */
	if (include_guard_contains(guard, canonical_path)) {
		return bstrdup("");
	}

	char *content = read_file_contents(canonical_path);
	if (!content) {
		blog(LOG_WARNING, "[hypershader] ┌─ Shader read failed");
		blog(LOG_WARNING, "[hypershader] ├─ Path : %s", canonical_path);
		blog(LOG_WARNING, "[hypershader] └─ Action: check file access and include paths");
		return NULL;
	}

	include_guard_add(guard, canonical_path);

	/* Dynamic buffer for result */
	size_t result_size = strlen(content) * 2;
	if (result_size < 1024)
		result_size = 1024;
	size_t result_len = 0;
	char *result = bmalloc(result_size);
	result[0] = '\0';

	char *line_start = content;
	char *line_end;
	bool in_block_comment = false;

	while (line_start && *line_start) {
		line_end = strchr(line_start, '\n');
		size_t line_len = line_end ? (size_t)(line_end - line_start) : strlen(line_start);

		const char *inc = effect_include_directive(line_start, line_len, &in_block_comment);
		if (inc) {
			const char *line_limit = line_start + line_len;
			const char *q1 = inc + 8;
			while (q1 < line_limit && (*q1 == ' ' || *q1 == '\t'))
				++q1;
			const char *q2 = (q1 < line_limit && *q1 == '"')
				? memchr(q1 + 1, '"', line_limit - q1 - 1) : NULL;
			if (!q2 || q2 == q1 + 1) {
				blog(LOG_ERROR, "[hypershader] Invalid #include directive in %s", canonical_path);
				bfree(content);
				bfree(result);
				return NULL;
			}
			size_t inc_path_len = (size_t)(q2 - q1 - 1);
			char *inc_filename = bmalloc(inc_path_len + 1);
			memcpy(inc_filename, q1 + 1, inc_path_len);
			inc_filename[inc_path_len] = '\0';
			char *full_path = resolve_include_path(inc_filename, canonical_path);
			char *included = full_path
				? preprocess_shader_with_guard(full_path, depth + 1, guard) : NULL;
			if (full_path)
				bfree(full_path);
			if (!included) {
				blog(LOG_ERROR, "[hypershader] Required #include failed: %s in %s",
				     inc_filename, canonical_path);
				bfree(inc_filename);
				bfree(content);
				bfree(result);
				return NULL;
			}
			bfree(inc_filename);
			size_t inc_len = strlen(included);
			while (result_len + inc_len + 2 >= result_size) {
				result_size *= 2;
				result = brealloc(result, result_size);
			}
			if (result_len > 0 && result[result_len - 1] != '\n')
				result[result_len++] = '\n';
			memcpy(result + result_len, included, inc_len);
			result_len += inc_len;
			if (result_len > 0 && result[result_len - 1] != '\n')
				result[result_len++] = '\n';
			result[result_len] = '\0';
			bfree(included);
			line_start = line_end ? line_end + 1 : NULL;
			continue;
		}

		/* The OBS effect compiler cannot expand multi-line wrapper defines.
		 * Skip exactly the definition and its continuation lines, including
		 * the final non-backslash line (previously leaked a stray brace). */
		if (strncmp(line_start, "#define SHADERTOY_MAINIMAGE_WRAPPER", sizeof("#define SHADERTOY_MAINIMAGE_WRAPPER") - 1) == 0 ||
		    strncmp(line_start, "#define HYPERSHADER_PIXEL_WRAPPER", sizeof("#define HYPERSHADER_PIXEL_WRAPPER") - 1) == 0) {
			bool continued = line_len > 0 && line_start[line_len - 1] == '\\';
			line_start = line_end ? line_end + 1 : NULL;
			while (continued && line_start && *line_start) {
				char *next = strchr(line_start, '\n');
				size_t clen = next ? (size_t)(next - line_start) : strlen(line_start);
				continued = clen > 0 && line_start[clen - 1] == '\\';
				line_start = next ? next + 1 : NULL;
			}
			continue;
		}

		while (result_len + line_len + 2 >= result_size) {
			result_size *= 2;
			result = brealloc(result, result_size);
		}

		memcpy(result + result_len, line_start, line_len);
		result_len += line_len;
		result[result_len++] = '\n';
		result[result_len] = '\0';

		if (line_end) {
			line_start = line_end + 1;
		} else {
			line_start = NULL;
		}
	}

	bfree(content);
	return result;
}

/* Legacy wrapper for backward compatibility */
static char *preprocess_shader_internal(const char *path, int depth)
{
	struct include_guard guard;
	include_guard_init(&guard);
	char *result = preprocess_shader_with_guard(path, depth, &guard);
	include_guard_free(&guard);
	return result;
}

/* Public API: Preprocess shader file (resolves #include) 
 * Returns preprocessed shader source (caller must bfree) */
static char *preprocess_shader(const char *path)
{
	blog(LOG_DEBUG, "[hypershader] Preprocessing shader: %s", path);
	if (!path || !path[0])
		return NULL;

	struct include_guard guard;
	include_guard_init(&guard);

	char *result = preprocess_shader_with_guard(path, 0, &guard);

	if (result) {
		/* Perform GLSL -> OBS HLSL translation (DISABLED DEBUG: Native macros should handle this) */
		/* Perform GLSL -> OBS HLSL translation */
		char *translated = translate_glsl_to_obs(result);
		if (translated) {
			bfree(result);
			result = translated;
		} else {
			blog(LOG_ERROR, "[hypershader] Translation failed, using original source");
		}

		char *expanded = expand_pixel_wrapper_statements(result);
		bfree(result);
		if (!expanded) {
			include_guard_free(&guard);
			return NULL;
		}
		result = expanded;

		size_t len = strlen(result);
		blog(LOG_DEBUG, "[hypershader] Preprocessed shader: %zu bytes (%zu files included)", len,
		     guard.count > 0 ? guard.count - 1 : 0);
		if (len > 100) {
			blog(LOG_DEBUG, "[hypershader] Suffix: %s", result + len - 100);
		} else {
			blog(LOG_DEBUG, "[hypershader] Source: %s", result);
		}
	}

	include_guard_free(&guard);

	return result;
}

/* Create effect from preprocessed shader string */
static gs_effect_t *create_effect_with_includes(const char *path, char **error_buf)
{
	/* Resolve relative paths before reading */
	char *resolved_path = NULL;
	if (path && path[0] != '/' && path[0] != '\\') {
		/* Relative path - resolve against the installed plugin data directory first */
		resolved_path = hs_resolve_data_file(path);
		if (!resolved_path) {
			if (error_buf)
				*error_buf = bstrdup("Failed to resolve shader path");
			return NULL;
		}
		path = resolved_path;
	}

	char *content = read_file_contents(path);
	if (!content) {
		if (error_buf)
			*error_buf = bstrdup("Failed to read shader file");
		if (resolved_path)
			bfree(resolved_path);
		return NULL;
	}

	/* Optimization: If no includes, use native loader which is more robust */
	if (!shader_has_include_directive(content)) {
		bfree(content);
		if (resolved_path)
			bfree(resolved_path);
		return gs_effect_create_from_file(path, error_buf);
	}
	bfree(content);

	char *source = preprocess_shader(path);
	if (!source) {
		if (error_buf) {
			*error_buf = bstrdup("Failed to preprocess shader (check #include paths)");
		}
		if (resolved_path)
			bfree(resolved_path);
		return NULL;
	}

/* DEBUG: Write preprocessed source to config dir for inspection */
#ifdef _DEBUG
	char *debug_path = obs_module_config_path("hypershader_last.effect");
	if (debug_path) {
		FILE *f_debug = fopen(debug_path, "w");
		if (f_debug) {
			fprintf(f_debug, "%s", source);
			fclose(f_debug);
		}
		bfree(debug_path);
	}
#endif

	gs_effect_t *effect = NULL;
	char *temp_path = obs_module_config_path("hypershader_preprocessed.effect");
	if (temp_path) {
		if (os_quick_write_utf8_file(temp_path, source, strlen(source), false)) {
			effect = gs_effect_create_from_file(temp_path, error_buf);
		} else {
			blog(LOG_WARNING, "[hypershader] ┌─ Temp effect write failed");
			blog(LOG_WARNING, "[hypershader] └─ Path : %s", temp_path);
		}
		bfree(temp_path);
	}
	if (!effect) {
		if (error_buf && (!*error_buf || !(*error_buf)[0]))
			*error_buf = bstrdup("Failed to load preprocessed shader from temp file");
	}

	if (error_buf && *error_buf && (*error_buf)[0] != '\0') {
		blog(LOG_WARNING, "[hypershader] ┌─ Shader compile diagnostics");
		blog(LOG_WARNING, "[hypershader] ├─ Path : %s", path);
		blog(LOG_WARNING, "[hypershader] └─ Detail:\n%s", *error_buf);
	}

	bfree(source);
	if (resolved_path)
		bfree(resolved_path);

	return effect;
}

static void log_debug_direct(const char *msg)
{
	blog(LOG_DEBUG, "%s", msg);
	char *debug_path = obs_module_config_path("hypershader_debug.log");
	if (debug_path) {
		FILE *f = fopen(debug_path, "a");
		if (f) {
			fprintf(f, "%s\n", msg);
			fflush(f);
			fclose(f);
		}
		bfree(debug_path);
	}
}

/* Called with the graphics context and, during shader replacement, s->mutex.
 * No scene allocation or GPU texture creation in the frame-render callback. */
static void entity_scene_release(struct hypershader_data *s)
{
    const bool allocated = s->scene != NULL;
    if (s->scene)
        scene_destroy(s->scene);
    s->scene = NULL;
    bfree(s->entity_data);
    s->entity_data = NULL;
    if (s->entity_texture)
        gs_texture_destroy(s->entity_texture);
    s->entity_texture = NULL;
    s->entity_count = 0;
    s->entity_count_max = 0;
    if (allocated)
        blog(LOG_INFO, "[hypershader] EnTT Scene Manager released.");
}

static void entity_scene_configure(struct hypershader_data *s)
{
    if (!s->ep_entityCount || (!s->ep_entityTexture && !s->ep_entities)) {
        entity_scene_release(s);
        return;
    }
    if (!s->scene) {
        s->scene = scene_create();
        s->entity_count_max = 100;
        s->entity_data = bzalloc((size_t)s->entity_count_max * 4 * sizeof(float));
        if (!s->scene || !s->entity_data) {
            entity_scene_release(s);
            blog(LOG_WARNING, "[hypershader] Entity scene allocation failed");
            return;
        }
        s->entity_count = scene_get_entities(s->scene, s->entity_data, s->entity_count_max);
        blog(LOG_INFO, "[hypershader] EnTT Scene Manager initialized.");
    }
    if (s->ep_entityTexture && !s->entity_texture) {
        s->entity_texture = gs_texture_create(s->entity_count_max, 1, GS_RGBA32F, 1, NULL, GS_DYNAMIC);
        if (!s->entity_texture)
            blog(LOG_WARNING, "[hypershader] Entity texture allocation failed");
    } else if (!s->ep_entityTexture && s->entity_texture) {
        gs_texture_destroy(s->entity_texture);
        s->entity_texture = NULL;
    }
}

/* Pending GPU readback has not been handed to the worker yet.
 * Destroy the old staging resource instead of reusing an in-flight PBO. */
static void mp_cancel_pending_readback(struct hypershader_data *s)
{
    if (!s->mp_readback_fence)
        return;
    hs_readback_fence_destroy(s->mp_readback_fence);
    s->mp_readback_fence = NULL;
    s->mp_readback_started_ns = 0;
    if (s->mp_staging_tex) {
        gs_stagesurface_destroy(s->mp_staging_tex);
        s->mp_staging_tex = NULL;
    }
    atomic_store_explicit(&s->mp_frame_buffer_in_use,false,memory_order_release);
}

static void filter_destroy(void *data)
{
	struct hypershader_data *s = data;
	if (!s)
		return;

	hs_data_driver_destroy(s->data_driver);
	s->data_driver = NULL;
	effect_hotkeys_unregister(s->effect_hotkeys);
	effect_hotkeys_unregister(s->history_reset_hotkeys);
	log_debug_direct("filter_destroy: START");

	/* 1. Stop worker thread FIRST */
	if (audio_thread_is_active(&s->audio_thread_mgr)) {
		log_debug_direct("filter_destroy: Stop audio thread");
		audio_thread_stop(&s->audio_thread_mgr, &s->audio_cv);
		log_debug_direct("filter_destroy: Audio thread joined successfully");
	}

	if (s->sound_preview_running) {
		log_debug_direct("filter_destroy: Stop sound preview");
		s->sound_preview_running = false;
		pthread_join(s->sound_preview_thread, NULL);
	}

	if (s->probe_running) {
		log_debug_direct("filter_destroy: Stop mix probe");
		s->probe_running = false;
		pthread_join(s->probe_thread, NULL);
	}

	/* 2. Stop shader load worker BEFORE entering graphics context.
	 * If we join inside obs_enter_graphics(), queued graphics tasks may stall
	 * and race with resource destruction. */
	if (s->shader_load_thread_run) {
		log_debug_direct("filter_destroy: Stop shader load thread");
		pthread_mutex_lock(&s->shader_load_mutex);
		s->shader_load_thread_run = false;
		pthread_cond_signal(&s->shader_load_cond);
		pthread_mutex_unlock(&s->shader_load_mutex);
		pthread_join(s->shader_load_thread, NULL);
		pthread_mutex_destroy(&s->shader_load_mutex);
		pthread_cond_destroy(&s->shader_load_cond);
		log_debug_direct("filter_destroy: Shader load thread joined");
	}

	/* Native MediaPipe sync exists even if Sight mode skipped the worker. */
	if (s->mp_thread_run) {
		log_debug_direct("filter_destroy: Stop MediaPipe thread");
		pthread_mutex_lock(&s->mp_mutex);
		s->mp_thread_run = false;
		pthread_cond_signal(&s->mp_cond);
		pthread_mutex_unlock(&s->mp_mutex);
		pthread_join(s->mp_thread, NULL);
		s->mp_pending_task_active = false;
		log_debug_direct("filter_destroy: MediaPipe thread joined");
	}
	if (s->mp_sync_ready) {
		pthread_mutex_destroy(&s->mp_mutex);
		pthread_cond_destroy(&s->mp_cond);
		s->mp_sync_ready = false;
	}

	/* A Sight receiver is an OBS private source, never a second model. */
	if (s->sight_receiver) {
		obs_source_release(s->sight_receiver);
		s->sight_receiver = NULL;
	}
	/* 3. Disconnect audio BEFORE freeing buffers */
	if (s->audio_connected) {
		log_debug_direct("filter_destroy: Disconnect audio");
		stop_obs_audio_connection(s);
	}
	stop_source_audio_connection(s);

	log_debug_direct("filter_destroy: Cleanup FFT");
	/* hyper_audio_destroy handles all audio resource cleanup (including pthread primitives) */
	hyper_audio_destroy(s);

	log_debug_direct("filter_destroy: Cleanup Graphics");
	/* 4. Graphics resources (enter/leave graphics context) */
	obs_enter_graphics();

	if (s->mp_runner) {
		log_debug_direct("filter_destroy: mp_destroy start");
		mp_destroy(s->mp_runner);
		s->mp_runner = NULL;
		log_debug_direct("filter_destroy: mp_destroy end");
	}

	entity_scene_release(s);
	if (s->mp_mask_tex) {
		gs_texture_destroy(s->mp_mask_tex);
		s->mp_mask_tex = NULL;
	}
	if (s->mp_landmarks_tex) {
		gs_texture_destroy(s->mp_landmarks_tex);
		s->mp_landmarks_tex = NULL;
	}
	if (s->mp_ref_landmarks_tex) {
		gs_texture_destroy(s->mp_ref_landmarks_tex);
		s->mp_ref_landmarks_tex = NULL;
	}
	if (s->sight_matte_tex) gs_texture_destroy(s->sight_matte_tex);
	if (s->sight_depth_tex) gs_texture_destroy(s->sight_depth_tex);
    if (s->sight_text_mask) gs_texture_destroy(s->sight_text_mask);
    if (s->sight_flow_texture) gs_texture_destroy(s->sight_flow_texture);
    if (s->sight_surface_texture) gs_texture_destroy(s->sight_surface_texture);
    hs_sight_upload_rgba32f(&s->sight_text_bounds,0,0,NULL);
    hs_sight_upload_rgba32f(&s->sight_semantic_bounds,0,0,NULL);
    hs_sight_upload_rgba32f(&s->sight_semantic_meta,0,0,NULL);
    if(s->sight_semantic_mask)gs_texture_destroy(s->sight_semantic_mask);
	hs_sight_upload_rgba32f(&s->sight_points_tex, 0, 0, NULL);
	hs_sight_upload_rgba32f(&s->sight_hands_tex, 0, 0, NULL);
	hs_sight_upload_rgba32f(&s->sight_blendshape_tex, 0, 0, NULL);
	hs_sight_upload_rgba32f(&s->sight_transform_tex, 0, 0, NULL);
	if (s->sight_face_surface) {
		hs_face_surface_destroy(s->sight_face_surface);
		s->sight_face_surface = NULL;
	}

	if (s->mp_face_surface) {
		hs_face_surface_destroy(s->mp_face_surface);
		s->mp_face_surface = NULL;
	}
	mp_cancel_pending_readback(s);
	if (s->mp_staging_tex) {
		gs_stagesurface_destroy(s->mp_staging_tex);
		s->mp_staging_tex = NULL;
	}
	if (s->mp_scaler) {
		gs_texrender_destroy(s->mp_scaler);
		s->mp_scaler = NULL;
	}
	if (s->mp_preallocated_frame) {
		bfree(s->mp_preallocated_frame);
		s->mp_preallocated_frame = NULL;
	}
	if (s->effect) {
		free_effect_param_cache(s);
		gs_effect_destroy(s->effect);
		s->effect = NULL;
	}
	for (int i = 0; i < 2; i++) {
		if (s->history[i]) {
			gs_texrender_destroy(s->history[i]);
			s->history[i] = NULL;
		}
	}
	graph_reset(s);
    if (s->output_compositor) {
        gs_effect_destroy(s->output_compositor);
        s->output_compositor = NULL;
    }
    if (s->output_composite_fx) {
        gs_texrender_destroy(s->output_composite_fx);
        s->output_composite_fx = NULL;
    }
    if (s->output_composite_base) {
        gs_texrender_destroy(s->output_composite_base);
        s->output_composite_base = NULL;
    }
    if (s->output_transparent) {
        gs_texture_destroy(s->output_transparent);
        s->output_transparent = NULL;
    }
	if (s->feedback_input) {
		gs_texrender_destroy(s->feedback_input);
		s->feedback_input = NULL;
	}
	for (int i = 0; i < CHANNEL_COUNT; i++) {
		if (s->channel_renders[i]) {
			gs_texrender_destroy(s->channel_renders[i]);
			s->channel_renders[i] = NULL;
		}
	}
	if (s->audio_texture) {
		gs_texture_destroy(s->audio_texture);
		s->audio_texture = NULL;
	}
	if (s->dummy_texture) {
		gs_texture_destroy(s->dummy_texture);
		s->dummy_texture = NULL;
	}

	/* Cleanup hot reload state */
	if (s->hot_reload_state.backup_effect) {
		gs_effect_destroy(s->hot_reload_state.backup_effect);
		s->hot_reload_state.backup_effect = NULL;
	}
	if (s->hot_reload_state.backup_error) {
		bfree(s->hot_reload_state.backup_error);
		s->hot_reload_state.backup_error = NULL;
	}

	for (int i = 0; i < CHANNEL_COUNT; i++) {
		/* Ensure texture/image freed properly */
		if (hs_channel_image_loaded(&s->channel_images[i]) || hs_channel_image_texture(&s->channel_images[i]))
			hs_channel_image_free(&s->channel_images[i]);
	}

#ifdef __linux__
	if (s->x11_display) {
		log_debug_direct("filter_destroy: Closing X11 display");
		Display *dpy = s->x11_display;
		s->x11_display = NULL;
		XCloseDisplay(dpy);
	}
#endif

	obs_leave_graphics();

	/* Note: We do NOT lock mutex again for string cleanup - strings can be freed without mutex protection
       since the graphics thread is done rendering and we're in the destruction phase */

	log_debug_direct("filter_destroy: Cleanup Channels");
	/* 5. Channel cleanup */
	for (int i = 0; i < CHANNEL_COUNT; i++) {
		if (s->channel_sources[i]) {
			obs_weak_source_release(s->channel_sources[i]);
			s->channel_sources[i] = NULL;
		}
		if (s->channel_files[i]) {
			bfree(s->channel_files[i]);
			s->channel_files[i] = NULL;
		}
		if (s->channel_source_names[i]) {
			bfree(s->channel_source_names[i]);
			s->channel_source_names[i] = NULL;
		}
		if (s->channel_source_uuids[i]) {
			bfree(s->channel_source_uuids[i]);
			s->channel_source_uuids[i] = NULL;
		}
	}

	log_debug_direct("filter_destroy: Cleanup Strings");
	/* 6. String resources (safe to free without mutex since rendering is complete) */
	if (s->shader_path) {
		bfree(s->shader_path);
		s->shader_path = NULL;
	}
	bfree(s->performance_compiled_path);

	if (s->shader_error) {
		bfree(s->shader_error);
		s->shader_error = NULL;
	}
	if (s->audio_source) {
		bfree(s->audio_source);
		s->audio_source = NULL;
	}
	if (s->fft_texture_history) {
		bfree(s->fft_texture_history);
		s->fft_texture_history = NULL;
	}
	if (s->data_path) {
		bfree(s->data_path);
		s->data_path = NULL;
	}

	for (int i = 0; i < AUDIO_MAP_SLOTS; i++) {
		if (s->audio_map_name[i]) {
			bfree(s->audio_map_name[i]);
			s->audio_map_name[i] = NULL;
		}
	}

	log_debug_direct("filter_destroy: Cleanup Metadata");

	/* Metadata cleanup logic (Bug C fix: prevent double-free) */
	if (s->metadata_rc) {
		log_debug_direct("filter_destroy: Releasing metadata_rc");
		pthread_mutex_lock(&s->mutex);
		struct shader_meta_rc *rc = s->metadata_rc;
		s->metadata_rc = NULL;
		pthread_mutex_unlock(&s->mutex);
		metadata_rc_release(&rc);
	}


	if (s->meta_audio_values) {
		bfree(s->meta_audio_values);
		s->meta_audio_values = NULL;
	}

	/* Destroy mutexes (note: mutex was unlocked earlier to prevent deadlock) */
	pthread_mutex_destroy(&s->mutex);
	pthread_mutex_destroy(&s->audio_route_lock);

	log_debug_direct("filter_destroy: bfree(s)");
	bfree(s);
	log_debug_direct("filter_destroy: END");
}

/* Channel selection changes and GPU texture ownership share the graphics
 * context lock. No strong reference is retained to another source. */
static void clear_channel_source(struct hypershader_data *s, int ch)
{
	if (s->channel_sources[ch]) {
		obs_weak_source_release(s->channel_sources[ch]);
		s->channel_sources[ch] = NULL;
	}
	if (s->channel_renders[ch]) {
		gs_texrender_destroy(s->channel_renders[ch]);
		s->channel_renders[ch] = NULL;
	}
	s->channel_textures[ch] = NULL;
}

static bool bind_channel_source(struct hypershader_data *s, int ch, const char *uuid, const char *name)
{
	if (ch < 0 || ch >= CHANNEL_COUNT)
		return false;
	obs_source_t *src = hs_source_selection_resolve(name, uuid);
	if (src && (src == s->context || obs_source_get_type(src) == OBS_SOURCE_TYPE_FILTER ||
		    !(obs_source_get_output_flags(src) & OBS_SOURCE_VIDEO))) {
		obs_source_release(src);
		src = NULL;
	}
	if (src && s->channel_sources[ch] &&
	    obs_weak_source_references_source(s->channel_sources[ch], src)) {
		obs_source_release(src);
		return true;
	}
	clear_channel_source(s, ch);
	if (!src)
		return false;
	s->channel_sources[ch] = obs_source_get_weak_source(src);
	const char *found_uuid = obs_source_get_uuid(src);
	bfree(s->channel_source_uuids[ch]);
	s->channel_source_uuids[ch] = found_uuid && *found_uuid ? bstrdup(found_uuid) : NULL;
	obs_source_release(src);
	return s->channel_sources[ch] != NULL;
}

struct channel_tree_check {
	obs_source_t *owner;
	obs_source_t *parent;
	bool cycle;
};

static void channel_check_descendant(obs_source_t *parent, obs_source_t *child, void *data)
{
	struct channel_tree_check *check = data;
	if (parent == check->owner || child == check->owner ||
	    (check->parent && (parent == check->parent || child == check->parent)))
		check->cycle = true;
}

static bool channel_capture_allowed(struct hypershader_data *s, obs_source_t *src)
{
	/* No hidden-source activation or audio mixing side effects. A separate
	 * showing lease contract is needed for independently hidden media. */
	if (!src || obs_source_removed(src) || !obs_source_enabled(src) ||
	    !obs_source_showing(src) || src == s->context)
		return false;
	struct channel_tree_check check = {s->context, NULL, false};
	if (s->kind == HYPER_KIND_FILTER)
		check.parent = obs_filter_get_parent(s->context);
	if (src == check.parent)
		return false;
	obs_source_enum_full_tree(src, channel_check_descendant, &check);
	return !check.cycle;
}

/* Capture the selected source through libobs, not its decoder or raw
 * texture. The render target API restores viewport, projection, matrix
 * and color space on end. Each graph pass samples the same capture.
 * Do not start another capture when a shader technique is active. */
static void capture_live_channels(struct hypershader_data *s)
{
	if (!s->effect || s->channel_capture_active)
		return;
	/* Transition lanes have a distinct A/B input contract. Do not claim
	 * extra capture support there until their native render is qualified. */
	const bool capture_allowed = !gs_get_effect() && s->kind != HYPER_KIND_TRANSITION;
	s->channel_capture_active = true;
	for (int i = 0; i < CHANNEL_COUNT; i++) {
		if (s->channel_types[i] != 2)
			continue;
		s->channel_textures[i] = NULL;
		obs_source_t *src = capture_allowed && s->ep_input[i] && s->channel_sources[i] ?
			obs_weak_source_get_source(s->channel_sources[i]) : NULL;
		if (channel_capture_allowed(s, src)) {
			const uint32_t width = obs_source_get_width(src);
			const uint32_t height = obs_source_get_height(src);
			const enum gs_color_space preferred[] = {GS_CS_SRGB, GS_CS_SRGB_16F, GS_CS_709_EXTENDED};
			const enum gs_color_space space = obs_source_get_color_space(src, 3, preferred);
			const enum gs_color_format format = gs_get_format_from_space(space);
			const uint64_t bpp = format == GS_RGBA16F ? 8 : 4;
			/* Separate conservative per-channel allocation limit. */
			if (width && height && (uint64_t)width * height <= (128 * HS_GRAPH_MIB) / bpp) {
				if (s->channel_renders[i] && gs_texrender_get_format(s->channel_renders[i]) != format) {
					gs_texrender_destroy(s->channel_renders[i]);
					s->channel_renders[i] = NULL;
				}
				if (!s->channel_renders[i])
					s->channel_renders[i] = gs_texrender_create(format, GS_ZS_NONE);
				gs_texrender_t *render = s->channel_renders[i];
				if (render) {
					gs_texrender_reset(render);
					gs_blend_state_push();
					gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
					if (gs_texrender_begin_with_color_space(render, width, height, space)) {
						struct vec4 clear;
						vec4_zero(&clear);
						gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
						gs_ortho(0, (float)width, 0, (float)height, -100, 100);
						obs_source_video_render(src);
						gs_texrender_end(render);
						s->channel_textures[i] = gs_texrender_get_texture(render);
					}
					gs_blend_state_pop();
				}
			}
		}
		if (!s->channel_textures[i] && s->channel_renders[i]) {
			gs_texrender_destroy(s->channel_renders[i]);
			s->channel_renders[i] = NULL;
		}
		if (src)
			obs_source_release(src);
	}
	s->channel_capture_active = false;
}

static void update_text_texture(struct shader_param_meta *m, const char *text)
{
	if (!m)
		return;

	/* Avoid unnecessary updates if text hasn't changed */
	if (m->text_value && text && strcmp(m->text_value, text) == 0 && m->text_texture)
		return;

	/* Update stored text value */
	if (m->text_value)
		bfree(m->text_value);
	m->text_value = text ? bstrdup(text) : NULL;

	obs_enter_graphics();

	if (m->text_texture) {
		gs_texture_destroy(m->text_texture);
		m->text_texture = NULL;
	}

	const int w = 256;
	const int h = 64;
	uint32_t *pixels = bmalloc(w * h * 4);

	/* Simple visualization:
	   Fill texture with a color derived from string hash + stripe pattern.
	   This proves the input works until we add a full font renderer. */
	uint32_t hash = 0;
	if (text) {
		for (const char *p = text; *p; p++)
			hash = hash * 31 + *p;
	}

	uint8_t r = (hash & 0xFF);
	uint8_t g = ((hash >> 8) & 0xFF);
	uint8_t b = ((hash >> 16) & 0xFF);

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			bool pattern = ((x / 10) + (y / 10)) % 2 == 0;
			if (pattern) {
				pixels[y * w + x] = 0xFF000000 | (b << 16) | (g << 8) | r;
			} else {
				pixels[y * w + x] = 0xFF000000;
			}
		}
	}

	const uint8_t *data_ptr = (const uint8_t *)pixels;
	m->text_texture = gs_texture_create(w, h, GS_RGBA, 1, &data_ptr, GS_DYNAMIC);
	bfree(pixels);

	obs_leave_graphics();
}

/* ============================================================================
 * SHADER RESOURCE MANAGEMENT
 * Decoupled loading logic to prevent partial state updates
 * ============================================================================ */

typedef struct {
	gs_effect_t *effect;
	struct shader_meta_rc *rc;
	float *audio_values;
	char *error;
	bool channel_required[CHANNEL_COUNT];
	struct hs_graph_plan graph;
} shader_resource_t;

static void free_shader_resource(shader_resource_t *res)
{
	if (!res)
		return;
	/* Note: We do NOT destroy effect here if it was swapped into 's'.
       However, if ownership was transferred, res->effect would be NULL.
       So unconditional destroy is checked against NULL. */
	if (res->effect) {
		gs_effect_destroy(res->effect);
	}
	if (res->rc)
		metadata_rc_release(&res->rc);
	if (res->audio_values)
		bfree(res->audio_values);
	if (res->error)
		bfree(res->error);
	bfree(res);
}

static shader_resource_t *load_shader_resource(struct hypershader_data *s, const char *path)
{
	/* RAII-style resource tracking - initialize all to NULL for safe cleanup */
	shader_resource_t *res = NULL;
	char *path_dup = NULL;
	char *resolved_path = NULL;
	char *preprocessed_source = NULL;
	char *error_buf = NULL;
	shader_meta_t new_meta = {0};
	struct shader_capabilities new_capabilities = {0};
	bool success = false;

	/* Allocate result structure */
	res = bzalloc(sizeof(*res));
	if (!res)
		return NULL;

	if (!path || !path[0]) {
		res->error = bstrdup("No shader file selected");
		return res;
	}

	path_dup = bstrdup(path);
	if (!path_dup) {
		res->error = bstrdup("Failed to allocate path");
		return res;
	}

	/* Parse into candidate-owned state. A rejected shader must never
	 * mutate the currently loaded resource contract; same-path edits and
	 * changed includes must not reuse stale metadata. */
	parse_shader_metadata(path_dup, &new_meta, res->channel_required, &new_capabilities);

	/* Prepare audio values buffer */
	if (new_meta.num > 0) {
		res->audio_values = bzalloc(sizeof(float) * new_meta.num);
		if (!res->audio_values) {
			res->error = bstrdup("Failed to allocate audio values");
			goto cleanup_error;
		}
	}

	/* 2. Preprocess shader OUTSIDE graphics context (Rule 7: no blocking I/O) */
	if (path_dup && path_dup[0] != '/' && path_dup[0] != '\\') {
		resolved_path = hs_resolve_data_file(path_dup);
		if (!resolved_path) {
			res->error = bstrdup("Failed to resolve shader path");
			goto cleanup_error;
		}
	}

	const char *shader_path = resolved_path ? resolved_path : path_dup;
	preprocessed_source = preprocess_shader(shader_path);
	bool use_native_loader = false;

	if (!preprocessed_source) {
		/* Check if file exists and has no includes (use native loader) */
		char *raw_content = read_file_contents(shader_path);
		if (raw_content) {
			if (strstr(raw_content, "#include") == NULL) {
				use_native_loader = true;
			}
			bfree(raw_content);
		}

		if (!use_native_loader) {
			res->error = bstrdup("Failed to preprocess shader (check #include paths)");
			goto cleanup_error;
		}
	}

	/* DEBUG: Log preprocessed source info */
	{
		if (preprocessed_source) {
			size_t len = strlen(preprocessed_source);
			blog(LOG_INFO, "[hypershader] DEBUG: preprocessed_source len=%zu", len);
			blog(LOG_INFO, "[hypershader] DEBUG: first 100: %.100s", preprocessed_source);
			blog(LOG_INFO, "[hypershader] DEBUG: last 50: %.50s",
			     preprocessed_source + (len > 50 ? len - 50 : 0));
			/* Check for null bytes */
			if (memchr(preprocessed_source, '\0', len)) {
				blog(LOG_ERROR, "[hypershader] DEBUG: FOUND NULL BYTES in preprocessed_source!");
			}
		} else {
			blog(LOG_INFO, "[hypershader] DEBUG: preprocessed_source is NULL");
		}
	}

	/* 3. Effect Creation - the caller explicitly holds the graphics
	 * context lock; merely queuing OBS_TASK_GRAPHICS is insufficient. */
	if (use_native_loader) {
		blog(LOG_INFO, "[hypershader] Using gs_effect_create_from_file: %s", shader_path);
		res->effect = gs_effect_create_from_file(shader_path, &error_buf);
	} else {
		/* Every shader instance owns its compiled source. A shared temporary
		 * filename caused OBS to keep using old shader code after Apply. */
		res->effect = gs_effect_create(preprocessed_source, shader_path, &error_buf);
		blog(LOG_INFO, "[hypershader] Compiled preprocessed shader: path=%s effect=%p error=%s",
		     shader_path, (void *)res->effect, error_buf ? error_buf : "(null)");
	}
	/* graphics lock already held — see note above */

	/* DEBUG: Check if effect was created successfully */
	if (!res->effect) {
		if (error_buf) {
			res->error = bstrdup(error_buf);
			bfree(error_buf);
			error_buf = NULL;
		} else {
			res->error = bstrdup("Unknown shader load error");
		}
		goto cleanup_error;
	}

	/* Resolve parameters on the NEW effect (safe: we're on graphics thread) */
	for (size_t i = 0; i < new_meta.num; i++) {
		if (new_meta.array[i].name) {
			new_meta.array[i].cached_param =
				gs_effect_get_param_by_name(res->effect, new_meta.array[i].name);
		} else {
			new_meta.array[i].cached_param = NULL;
		}
		/* Apply default values to the effect immediately */
		if (new_meta.array[i].has_default && new_meta.array[i].cached_param) {
			apply_parameter_default(new_meta.array[i].cached_param, &new_meta.array[i]);
		}
	}

	/* Validate named slots against the compiled effect before publishing it.
	 * Existing capture/decoding/settings remain the sole resource owner. */
	for (size_t i = 0; i < new_meta.num; ++i) {
		struct shader_param_meta *m = &new_meta.array[i];
		if (!m->input_slot)
			continue;
		struct gs_effect_param_info info = {0};
		if (m->cached_param)
			gs_effect_get_param_info(m->cached_param, &info);
		const int slot = m->input_slot - 1;
		if (slot < 0 || slot >= CHANNEL_COUNT ||
		    (s->kind == HYPER_KIND_TRANSITION && slot < 2) ||
		    !m->cached_param || info.type != GS_SHADER_PARAM_TEXTURE ||
		    !m->name || is_builtin_uniform(m->name) || strncmp(m->name, "hs_", 3) == 0 ||
		    m->is_text || m->audio_tag || m->has_default ||
		    res->channel_required[slot]) {
			res->error = bstrdup("Invalid named texture input: use a unique @input 0..3 on a texture2d uniform; numbered references or reserved transition slots cannot share it");
			goto cleanup_error;
		}
		const char *suffixes[] = {"_resolution", "_time"};
		const enum gs_shader_param_type types[] = {GS_SHADER_PARAM_VEC4, GS_SHADER_PARAM_FLOAT};
		for (unsigned j = 0; j < 2; ++j) {
			char companion[160];
			snprintf(companion, sizeof(companion), "%s%s", m->name, suffixes[j]);
			gs_eparam_t *param = gs_effect_get_param_by_name(res->effect, companion);
			if (!param)
				continue;
			gs_effect_get_param_info(param, &info);
			if (info.type != types[j]) {
				res->error = bstrdup("Invalid named texture companion: resolution must be float4 and time must be float");
				goto cleanup_error;
			}
			for (size_t k = 0; k < new_meta.num; ++k) {
				const struct shader_param_meta *other = &new_meta.array[k];
				if (other->name && strcmp(other->name, companion) == 0 &&
				    (other->input_slot || other->audio_tag || other->is_text || other->has_default)) {
					res->error = bstrdup("Named texture companions are read-only runtime uniforms");
					goto cleanup_error;
				}
			}
		}
		res->channel_required[slot] = true;
	}

	res->graph.valid = graph_configure(res->effect, &res->graph, s->kind);
	if (!res->graph.valid && res->graph.named) {
		res->error = bstrdup("Invalid named pass graph: require contiguous unique orders 0..7, matching Pass_name techniques, typed resources and valid lifetime descriptors; filter or source lane only");
		goto cleanup_error;
	}

	/* 4. Create RC Wrapper */
	res->rc = metadata_rc_create();
	if (!res->rc) {
		res->error = bstrdup("Failed to allocate metadata wrapper");
		goto cleanup_error;
	}

	/* Transfer ownership of metadata to RC wrapper */
	res->rc->meta = new_meta;
	res->rc->capabilities = new_capabilities;
	new_meta = (shader_meta_t){0}; /* Clear to prevent double-free */

	/* Success path */
	success = true;

cleanup_error:
	/* Centralized cleanup */
	if (!success) {
		/* Only free new_meta if we didn't transfer ownership */
		free_shader_metadata(&new_meta);

		/* Free audio_values on error */
		if (res->audio_values) {
			bfree(res->audio_values);
			res->audio_values = NULL;
		}
	}

	/* Always free these temporary allocations */
	if (preprocessed_source)
		bfree(preprocessed_source);
	if (resolved_path)
		bfree(resolved_path);
	if (path_dup)
		bfree(path_dup);
	if (error_buf)
		bfree(error_buf);

	return res;
}
/* TASK executed on graphics thread to upload MediaPipe results */
struct mp_update_task_data {
	struct hypershader_data *s;
    unsigned epoch;
	uint8_t *mask_buffer;
	int mask_width;
	int mask_height;
	float *landmark_data;
	int landmark_count;
	float *ref_landmark_data;
	int ref_landmark_count;
};

struct shader_load_task_data {
	struct hypershader_data *s;
	char *path;
	obs_data_t *settings; /* Copy of settings for applying defaults */
};

extern void *mp_worker_thread(void *data);

/* Sight already owns inference: do not load redundant face ONNX models.
 * Native fallback initializes on OBS's deferred video-filter update. This
 * may incur one-time model loading latency when switching away from Sight. */
static bool hs_ensure_native_mediapipe(struct hypershader_data *s)
{
	if (!s || !s->mp_sync_ready || !s->data_path)
		return false;
	if (s->mp_runner)
		return s->mp_thread_run;
	if (s->mp_init_attempted)
		return false;
	s->mp_init_attempted = true;

	struct dstr path = {0};
	dstr_copy(&path, s->data_path);
	dstr_cat(&path, "/models/mediapipe");
	s->mp_runner = mp_create(true, path.array);
	dstr_free(&path);
	if (!s->mp_runner) {
		blog(LOG_WARNING, "[hypershader] Native MediaPipe unavailable; Sight mode can still work");
		return false;
	}
    /* A Sight-only or unavailable-native instance owns no inference pixel buffer. */
    s->mp_preallocated_frame = bzalloc(MP_FRAME_SIZE);
    if (!s->mp_preallocated_frame) {
        mp_destroy(s->mp_runner);
        s->mp_runner = NULL;
        return false;
    }
	s->mp_thread_run = true; /* Worker checks this immediately after pthread_create. */
	if (pthread_create(&s->mp_thread, NULL, mp_worker_thread, s) != 0) {
		s->mp_thread_run = false;
		mp_destroy(s->mp_runner);
		s->mp_runner = NULL;
        bfree(s->mp_preallocated_frame);
        s->mp_preallocated_frame = NULL;
		blog(LOG_WARNING, "[hypershader] Failed to create native MediaPipe worker thread");
		return false;
	}
	blog(LOG_INFO, "[hypershader] MediaPipe Runner initialized (Backend: %s)", mp_get_backend_name());
	return true;
}

/* Audio visualization is a preset of the normal source. Only supply
 * defaults, never replace an explicit input, channel, or sound choice.
 * Reset defaults on every update so switching to a non-audio shader
 * cannot leave an implicit mix subscription or FFT channel behind. */
static void hs_source_audio_defaults(struct hypershader_data *s, obs_data_t *settings)
{
    if (s->kind != HYPER_KIND_SOURCE)
        return;
    const char *path = obs_data_get_string(settings, "shader_file");
    if (path && s->data_path && hs_path_in_data_dir(path, s->data_path)) {
        size_t length = strlen(s->data_path);
        while (length && s->data_path[length - 1] == '/')
            --length;
        path += length;
        if (*path == '/')
            ++path;
    }
    bool texture = path &&
        (strcmp(path, "shaders/root/source.phosphor_scope.effect") == 0 ||
         strcmp(path, "shaders/root/source.spectral_strata.effect") == 0);
    bool audio = texture || (path &&
        strcmp(path, "shaders/root/source.audio_spectrum_orbit.effect") == 0);
    obs_data_set_default_bool(settings, "enable_sound", audio);
    obs_data_set_default_int(settings, "input_mode",
        audio && obs_data_get_bool(settings, "enable_sound") ? INPUT_MODE_AUDIO : INPUT_MODE_SOURCE);
    obs_data_set_default_int(settings, "channel0_type", texture ? 3 : 0);
}

static void cache_runtime_settings(struct hypershader_data *s, obs_data_t *settings);

static void filter_update(void *data, obs_data_t *settings)
{
	struct hypershader_data *s = data;
	if (!s)
		return;

	hyper_ui_migrate_legacy_preset(settings);
    const char *data_uuid = "";
    if (obs_data_get_int(settings,"perf_mod_source")==8 &&
        !strcmp(obs_data_get_string(settings,"perf_mod_shader"),obs_data_get_string(settings,"shader_file")))
        data_uuid=obs_data_get_string(settings,"perf_mod_data_uuid");
    long long data_age=obs_data_get_int(settings,"perf_mod_data_age_ms");
    hs_data_driver_configure(s->data_driver,data_uuid,data_age>0?(int)fmin(60000.,(double)data_age):5000);
	hs_source_audio_defaults(s, settings);
	const bool sight_requested = obs_data_get_bool(settings, "sight_use_tracking");
	const char *channel = obs_data_get_string(settings, "sight_tracking_channel");
	if (!channel || !*channel) channel = "face";
	pthread_mutex_lock(&s->mutex);
	s->sight_use_tracking = sight_requested;
	snprintf(s->sight_channel, sizeof(s->sight_channel), "%s", channel);
	pthread_mutex_unlock(&s->mutex);
	/* A Sight-only instance does not even initialize native models.
	 * Native graph remains available if the user switches back. */
	if (!sight_requested && !s->mp_runner)
		hs_ensure_native_mediapipe(s);
    int graph = (int)obs_data_get_int(settings, "mp_controlnet_model");
    if (graph == 0) graph=MP_GRAPH_FACE_MESH;
    bool graph_ready = !sight_requested && s->mp_runner &&
        mp_graph_available(s->mp_runner,(mp_graph_type_t)graph);
    const bool native_enabled = graph_ready && s->mp_thread_run;

    /* Publish desired graph and generation together in the graphics owner.
     * Only the inference worker actually starts/switches a graph. */
    obs_enter_graphics();
    const mp_graph_type_t requested = native_enabled ? (mp_graph_type_t)graph : MP_GRAPH_NONE;
    if (requested != s->mp_requested_graph || native_enabled != s->mp_enabled) {
        atomic_fetch_add_explicit(&s->mp_epoch,1,memory_order_acq_rel);
        s->mp_requested_graph=requested;
        s->mp_enabled=native_enabled;
        mp_cancel_pending_readback(s);
        s->mp_capture_frame_valid=false;
    }
    if (!sight_requested && s->mp_runner && !graph_ready)
        blog(LOG_WARNING,"[hypershader] MediaPipe mode %d unavailable; processing disabled",graph);
	if (s->effect && s->metadata_rc) {
		size_t count = gs_effect_get_num_params(s->effect);
		for (size_t i = 0; i < count; i++) {
			gs_eparam_t *param = gs_effect_get_param_by_idx(s->effect, i);
			struct gs_effect_param_info info;
			gs_effect_get_param_info(param, &info);

			if (hs_runtime_uniform(s, info.name))
				continue;

			/* If this is a mapped audio uniform, skip it too (controlled by audio logic) */
			bool is_mapped = false;
			for (int m = 0; m < AUDIO_MAP_SLOTS; m++) {
				if (s->audio_map_name[m] && strcmp(s->audio_map_name[m], info.name) == 0) {
					is_mapped = true;
					break;
				}
			}
			if (is_mapped)
				continue;

			char prop_name[128];
			snprintf(prop_name, sizeof(prop_name), "param_%s", info.name);

			switch (info.type) {
			case GS_SHADER_PARAM_FLOAT: {
				float val = (float)obs_data_get_double(settings, prop_name);
				gs_effect_set_float(param, val);
				break;
			}
			case GS_SHADER_PARAM_INT: {
				int val = (int)obs_data_get_int(settings, prop_name);
				gs_effect_set_int(param, val);
				break;
			}
			case GS_SHADER_PARAM_BOOL: {
				bool val = obs_data_get_bool(settings, prop_name);
				gs_effect_set_bool(param, val);
				break;
			}
			case GS_SHADER_PARAM_VEC4: {
				bool value_set = false;
				if (obs_data_has_user_value(settings, prop_name)) {
					uint32_t cval = (uint32_t)obs_data_get_int(settings, prop_name);
					struct vec4 v;
					vec4_from_rgba(&v, cval);
					gs_effect_set_vec4(param, &v);
					value_set = true;
				} else {
					/* Fallback: use metadata default if available */
					for (size_t mi = 0; mi < s->metadata_rc->meta.num; mi++) {
						if (s->metadata_rc->meta.array[mi].name &&
						    strcmp(s->metadata_rc->meta.array[mi].name, info.name) == 0) {
							if (s->metadata_rc->meta.array[mi].default_val_str) {
								struct vec4 v;
								if (sscanf(s->metadata_rc->meta.array[mi]
										   .default_val_str,
									   "%f,%f,%f,%f", &v.x, &v.y, &v.z,
									   &v.w) == 4) {
									gs_effect_set_vec4(param, &v);
									value_set = true;
								}
							}
							break;
						}
					}
				}
				if (!value_set) {
					struct vec4 v = {0.5f, 0.5f, 0.5f, 1.0f};
					gs_effect_set_vec4(param, &v);
				}
				break;
			}
			case GS_SHADER_PARAM_VEC3: {
				bool value_set = false;

				/* Try user-saved value first */
				if (obs_data_has_user_value(settings, prop_name)) {
					uint32_t cval = (uint32_t)obs_data_get_int(settings, prop_name);
					struct vec4 v4;
					vec4_from_rgba(&v4, cval);
					struct vec3 v3;
					vec3_set(&v3, v4.x, v4.y, v4.z);
					gs_effect_set_vec3(param, &v3);
					value_set = true;
				}

				/* Fallback: use metadata default if user value wasn't set or was invalid */
				if (!value_set) {
					for (size_t mi = 0; mi < s->metadata_rc->meta.num; mi++) {
						if (s->metadata_rc->meta.array[mi].name &&
						    strcmp(s->metadata_rc->meta.array[mi].name, info.name) == 0) {
							if (s->metadata_rc->meta.array[mi].default_val_str) {
								struct vec3 v;
								if (sscanf(s->metadata_rc->meta.array[mi]
										   .default_val_str,
									   "%f,%f,%f", &v.x, &v.y, &v.z) == 3) {
									gs_effect_set_vec3(param, &v);
									value_set = true;
								}
							}
							break;
						}
					}
				}

				/* Last resort: set to a safe default to prevent 'invalid size 0' error */
				if (!value_set) {
					struct vec3 v = {0.5f, 0.5f, 0.5f};
					gs_effect_set_vec3(param, &v);
				}
				break;
			}
			case GS_SHADER_PARAM_UNKNOWN:
			case GS_SHADER_PARAM_STRING:
			case GS_SHADER_PARAM_VEC2:
			case GS_SHADER_PARAM_INT2:
			case GS_SHADER_PARAM_INT3:
			case GS_SHADER_PARAM_INT4:
			case GS_SHADER_PARAM_MATRIX4X4:
				/* These parameter shapes do not have dynamic UI bindings yet. */
				break;
			case GS_SHADER_PARAM_TEXTURE: {
				/* Handle @text input */
				for (size_t mi = 0; mi < s->metadata_rc->meta.num; mi++) {
					struct shader_param_meta *meta = &s->metadata_rc->meta.array[mi];
					if (meta->name && strcmp(meta->name, info.name) == 0) {
						if (meta->is_text) {
							const char *txt = obs_data_get_string(settings, prop_name);
							update_text_texture(meta, txt);
							if (meta->text_texture) {
								gs_effect_set_texture(param, meta->text_texture);
							}
						}
						break;
					}
				}
				break;
			}
			}
		}
	}
	cache_runtime_settings(s, settings);
	obs_leave_graphics();

    float requested_speed = (float)obs_data_get_double(settings, "speed");
    s->time_scale = isfinite(requested_speed) ? fminf(10.0f, fmaxf(0.0f, requested_speed)) : 1.0f;
    s->compose_base_opacity = fminf(1.0f, fmaxf(0.0f,
        (float)obs_data_get_double(settings, "compose_base_opacity")));
    s->compose_effect_opacity = fminf(1.0f, fmaxf(0.0f,
        (float)obs_data_get_double(settings, "compose_effect_opacity")));
    s->compose_blend_mode = (int)obs_data_get_int(settings, "compose_blend_mode");
    if (s->compose_blend_mode < 0 || s->compose_blend_mode > 5)
        s->compose_blend_mode = 0;

	/* Source dimensions (clamped to valid bounds) */
	if (s->kind == HYPER_KIND_SOURCE) {
		int w = (int)obs_data_get_int(settings, "source_width");
		int h = (int)obs_data_get_int(settings, "source_height");
		if (w < HYPER_SOURCE_WIDTH_MIN)
			w = HYPER_SOURCE_WIDTH_MIN;
		if (w > HYPER_SOURCE_WIDTH_MAX)
			w = HYPER_SOURCE_WIDTH_MAX;
		if (h < HYPER_SOURCE_HEIGHT_MIN)
			h = HYPER_SOURCE_HEIGHT_MIN;
		if (h > HYPER_SOURCE_HEIGHT_MAX)
			h = HYPER_SOURCE_HEIGHT_MAX;
		s->source_width = w;
		s->source_height = h;
	}

	/* Read input mode and optional audio source */
	pthread_mutex_lock(&s->audio_route_lock);
	s->input_mode = (int)obs_data_get_int(settings, "input_mode");
	const char *audio_src = obs_data_get_string(settings, "audio_source");
	bool audio_source_changed = (s->audio_source == NULL) != (audio_src == NULL || !audio_src[0]) ||
		(s->audio_source && audio_src && strcmp(s->audio_source, audio_src) != 0);
	if (audio_source_changed)
		stop_source_audio_connection(s);
	bfree(s->audio_source);
	s->audio_source = audio_src && audio_src[0] ? bstrdup(audio_src) : NULL;

	/* Audio settings */
	s->sound_enabled = (bool)obs_data_get_bool(settings, "enable_sound");
	s->sound_mix = (int)obs_data_get_int(settings, "sound_mix");
	s->audio_update_rate = (int)obs_data_get_int(settings, "audio_update_rate");
	s->audio_smoothing = (float)obs_data_get_double(settings, "audio_smoothing");
	bool prev_debug = s->audio_debug;
	s->audio_test_tone = (bool)obs_data_get_bool(settings, "audio_test_tone");
	s->audio_debug = (bool)obs_data_get_bool(settings, "audio_debug");
	if (prev_debug != s->audio_debug) {
		blog(LOG_INFO, "[hypershader] Audio debug %s", s->audio_debug ? "enabled" : "disabled");
	}

	/* FFT settings: if changed, reallocate FFT resources safely */
	hyper_audio_update_settings(s, settings);

	/* Selected mix index (0 = master). Keep within allowed range. */
	int mix = (int)obs_data_get_int(settings, "audio_mix");
	if (mix < 0)
		mix = 0;
	if (mix >= MAX_AUDIO_MIXES)
		mix = MAX_AUDIO_MIXES - 1;
	/* A normal video filter can be audio-reactive when its explicit
	 * Enable Sound setting is on. Do not require switching the filter to
	 * the unrelated Audio input mode (which breaks video usage). */
	const bool wants_audio = s->input_mode == INPUT_MODE_AUDIO ||
		(s->sound_enabled && s->audio_source);
	const bool wants_source_audio = s->sound_enabled && s->audio_source;
	const bool wants_mix_audio = !wants_source_audio && s->input_mode == INPUT_MODE_AUDIO;

	/* Disconnect the previous mix before changing the mix index: the
	 * callback must be removed from exactly the mix it subscribed to. */
	if (s->audio_connected && s->audio_mix_idx != (size_t)mix)
		stop_obs_audio_connection(s);
	s->audio_mix_idx = (size_t)mix;

	/* Enable Sound routes the selected source, not the entire OBS mix.
	 * Audio input mode retains its separately selected mix behavior. */
	if (!wants_mix_audio && s->audio_connected)
		stop_obs_audio_connection(s);
	if (!wants_source_audio && s->source_audio_connected)
		stop_source_audio_connection(s);
	if (wants_source_audio && s->selected_audio_source && obs_source_removed(s->selected_audio_source))
		stop_source_audio_connection(s);
	if (wants_source_audio && !s->source_audio_connected)
		start_source_audio_connection(s);
	if (wants_mix_audio && !s->audio_connected)
		start_obs_audio_connection(s);
	pthread_mutex_unlock(&s->audio_route_lock);

	/* Read user mappings */
	for (int i = 0; i < AUDIO_MAP_SLOTS; i++) {
		char namebuf[64];
		char featbuf[64];
		snprintf(namebuf, sizeof(namebuf), "mapping_%d_name", i);
		snprintf(featbuf, sizeof(featbuf), "mapping_%d_feature", i);
		const char *mn = obs_data_get_string(settings, namebuf);
		int mf = (int)obs_data_get_int(settings, featbuf);
		if (s->audio_map_name[i])
			bfree(s->audio_map_name[i]);
		s->audio_map_name[i] = mn && mn[0] ? bstrdup(mn) : NULL;
		s->audio_map_feature[i] = mf;
		s->audio_map_enabled[i] = (s->audio_map_name[i] != NULL);
		s->audio_map_param[i] = NULL; /* will be (re)cached later */
	}

	const char *new_path = obs_data_get_string(settings, "shader_file");

	/* Security: Convert absolute paths within plugin data directory to relative paths */
	char *secure_path = NULL;
	if (new_path && new_path[0] == '/' && s->data_path) {
		const char *original_path = new_path;
		size_t data_path_len = strlen(s->data_path);
		/* Normalize: remove trailing slash from data_path for comparison */
		while (data_path_len > 0 && s->data_path[data_path_len - 1] == '/') {
			data_path_len--;
		}
		if (hs_path_in_data_dir(new_path, s->data_path)) {
			/* Path is within plugin data directory, convert to relative */
			secure_path = bstrdup(new_path + data_path_len);
			if (secure_path[0] == '/') {
				memmove(secure_path, secure_path + 1, strlen(secure_path));
			}
			blog(LOG_INFO, "[hypershader] SECURITY: Converted absolute path '%s' to relative '%s'",
			     original_path, secure_path);
			new_path = secure_path;
		} else if (!hs_path_in_managed_dir(new_path)) {
			/* File loading rejects paths outside installed assets and managed revisions. */
			blog(LOG_WARNING,
			     "[hypershader] SECURITY: Path '%s' is outside plugin data directory (data_path='%s') "
			     "and not a managed revision; rejected by shader loader",
			     new_path, s->data_path);
		}
	}
	if (!s->shader_path || strcmp(s->shader_path, new_path) != 0) {
		/* Disable stale dock schema immediately during async replacement. */
		obs_data_t *live_status = obs_source_get_private_settings(s->context);
		if (live_status) {
			obs_data_set_string(live_status, "hypershader_live_state", "pending");
			obs_data_release(live_status);
		}
		/* Queue asynchronous shader loading task */
		struct shader_load_task_data *task_data = bzalloc(sizeof(*task_data));
		task_data->s = s;
		task_data->path = bstrdup(new_path);
		task_data->settings = obs_data_create();
		obs_data_apply(task_data->settings, settings); /* Copy current settings */

		/* Queue task to worker thread */
		pthread_mutex_lock(&s->shader_load_mutex);
		if (s->shader_load_pending_task) {
			/* Cancel previous pending task */
			struct shader_load_task_data *old = s->shader_load_pending_task;
			bfree(old->path);
			obs_data_release(old->settings);
			bfree(old);
		}
		s->shader_load_pending_task = task_data;
		pthread_cond_signal(&s->shader_load_cond);
		pthread_mutex_unlock(&s->shader_load_mutex);

		/* Update path immediately for UI feedback */
		if (s->shader_path)
			bfree(s->shader_path);
		s->shader_path = bstrdup(new_path);
	}

	/* Note: Removed duplicate inputs mirroring - channels are the only input system now */
	for (int i = 0; i < CHANNEL_COUNT; i++) {
		char key_type[32], key_file[32], key_src[32], key_uuid[32];
		snprintf(key_type, sizeof(key_type), "channel%d_type", i);
		snprintf(key_file, sizeof(key_file), "channel%d_file", i);
		snprintf(key_src, sizeof(key_src), "channel%d_source", i);
		snprintf(key_uuid, sizeof(key_uuid), "channel%d_uuid", i);

		int type = (int)obs_data_get_int(settings, key_type);
		const char *file = obs_data_get_string(settings, key_file);
		const char *src_name = obs_data_get_string(settings, key_src);
		const char *src_uuid = obs_data_get_string(settings, key_uuid);

        /* Image storage, type and texture publication share the graphics
         * lock with animation uploads. Never leave a retired file bound. */
        if (type == 1) {
            obs_enter_graphics();
            if (s->channel_types[i] == 2)
                clear_channel_source(s, i);
            bool changed = s->channel_types[i] != 1 ||
                !s->channel_files[i] || strcmp(s->channel_files[i], file ? file : "") != 0;
            if (changed) {
                bfree(s->channel_files[i]);
                s->channel_files[i] = file && file[0] ? bstrdup(file) : NULL;
                hs_channel_image_free(&s->channel_images[i]);
                memset(&s->atlas_clock[i],0,sizeof(s->atlas_clock[i]));
                if (s->channel_files[i])
                    hs_channel_image_load(&s->channel_images[i], s->channel_files[i]);
            }
            s->channel_types[i] = type;
            s->channel_textures[i] = hs_channel_image_texture(&s->channel_images[i]);
            obs_leave_graphics();
        } else {
            obs_enter_graphics();
            if (s->channel_types[i] == 1) {
                hs_channel_image_free(&s->channel_images[i]);
                memset(&s->atlas_clock[i],0,sizeof(s->atlas_clock[i]));
                bfree(s->channel_files[i]);
                s->channel_files[i] = NULL;
            }
            if (s->channel_types[i] == 2 && type != 2)
                clear_channel_source(s, i);
            if (s->channel_types[i] != type || type != 2)
                s->channel_textures[i] = type == 3 ? s->audio_texture : NULL;
            s->channel_types[i] = type;
            if (type == 2) {
                bfree(s->channel_source_names[i]);
                s->channel_source_names[i] = src_name && src_name[0] ? bstrdup(src_name) : NULL;
                bfree(s->channel_source_uuids[i]);
                s->channel_source_uuids[i] = src_uuid && src_uuid[0] ? bstrdup(src_uuid) : NULL;
                bind_channel_source(s, i, src_uuid, src_name);
            }
            obs_leave_graphics();
        }
	}

	/* An opted-in video shader needs the FFT worker just as much as an
	 * audio-only input. Keep enable_sound=false as the default opt-out. */
	bool should_run_worker = wants_audio;
	bool currently_running = audio_thread_is_active(&s->audio_thread_mgr);

	if (should_run_worker && !currently_running) {
		/* Start worker */
		if (!audio_thread_start(&s->audio_thread_mgr, audio_worker, s)) {
			blog(LOG_ERROR, "[hypershader] Failed to start audio worker thread");
		}
	} else if (!should_run_worker && currently_running) {
		/* Stop worker immediately when leaving audio mode */
		audio_thread_stop(&s->audio_thread_mgr, &s->audio_cv);
	}

	if (secure_path)
		bfree(secure_path);
}

/* Forward declarations for worker threads */
extern void *mp_worker_thread(void *data);

/* Replaces filter_create to fix initialization bug */
/* Read-only live status. No source settings writes, GPU calls or inference
 * on the dock thread; render publishes a compact atomic snapshot. */
/* Filters receive showing notifications from their parent; their own
 * show_refs are not a reliable visibility predicate. A real draw also
 * leases the workload for consumers that render outside an OBS view. */
#define HS_WORKLOAD_DRAW_GRACE_NS UINT64_C(250000000)

static void workload_set_active(struct hypershader_data *s, bool active)
{
    bool previous=atomic_exchange_explicit(&s->workload_active,active,memory_order_acq_rel);
    if (previous && !active) {
        atomic_fetch_add_explicit(&s->workload_epoch,1,memory_order_seq_cst);
        atomic_fetch_add_explicit(&s->mp_epoch,1,memory_order_acq_rel);
    }
    if (previous!=active && s->audio_lockfree_ring.data)
        pthread_cond_signal(&s->audio_cv);
}

static void workload_show(void *data)
{
    struct hypershader_data *s=data;
    atomic_store_explicit(&s->workload_showing,true,memory_order_release);
}

static void workload_hide(void *data)
{
    struct hypershader_data *s=data;
    atomic_store_explicit(&s->workload_showing,false,memory_order_release);
}

static void workload_touch_draw(struct hypershader_data *s)
{
    if (!obs_source_enabled(s->context)) return;
    atomic_store_explicit(&s->workload_last_draw_ns,os_gettime_ns(),memory_order_release);
    workload_set_active(s,true);
}

static bool workload_demand(struct hypershader_data *s)
{
    if (!obs_source_enabled(s->context)) return false;
    if (s->kind == HYPER_KIND_FILTER) {
        if (atomic_load_explicit(&s->workload_showing,memory_order_acquire)) return true;
    } else if (obs_source_showing(s->context)) return true;
    const uint64_t last=atomic_load_explicit(&s->workload_last_draw_ns,memory_order_acquire);
    const uint64_t now=os_gettime_ns();
    return last && now>=last && now-last<=HS_WORKLOAD_DRAW_GRACE_NS;
}

static void hs_live_status(void *data, calldata_t *out)
{
	struct hypershader_data *s = data;
	const uint64_t now = os_gettime_ns();
	const uint64_t render = atomic_load_explicit(&s->live_render_ns, memory_order_acquire);
	const uint64_t pcm = atomic_load_explicit(&s->last_audio_capture_ns, memory_order_acquire);
	const bool fresh = render && now >= render && now - render < 1000000000ULL;
	const bool audio = atomic_load_explicit(&s->workload_active,memory_order_acquire) &&
        atomic_load_explicit(&s->audio_capture_admitted,memory_order_acquire) &&
        atomic_load_explicit(&s->audio_capture_epoch,memory_order_acquire)==
        atomic_load_explicit(&s->workload_epoch,memory_order_seq_cst) &&
        pcm && now >= pcm && now - pcm < 1000000000ULL;
	calldata_set_bool(out, "rendering", fresh);
	calldata_set_bool(out, "audio", audio);
	calldata_set_int(out, "level", audio && fresh ? atomic_load(&s->live_audio_permille) : 0);
	calldata_set_int(out, "signals", fresh ? atomic_load(&s->live_signal_flags) : 0);
}

static void *hypershader_create(obs_data_t *settings, obs_source_t *context, enum hypershader_instance_kind kind)
{
	struct hypershader_data *s = bzalloc(sizeof(struct hypershader_data));
	if (!s) {
		blog(LOG_ERROR, "[hypershader] Failed to allocate filter data structure");
		return NULL;
	}
	atomic_init(&s->last_audio_capture_ns, 0);
	atomic_init(&s->mod_motion_requested, false);
	atomic_init(&s->history_reset_requested, false);
    atomic_init(&s->mp_frame_buffer_in_use, false);
    atomic_init(&s->mp_thread_run, false);
    atomic_init(&s->mp_enabled, false);
    atomic_init(&s->mp_epoch,1);
    atomic_init(&s->mp_result_epoch,0);
    atomic_init(&s->mp_analyze_trigger, false);
	atomic_init(&s->live_render_ns, 0);
    atomic_init(&s->workload_active,false);
    atomic_init(&s->workload_showing,false);
    atomic_init(&s->workload_last_draw_ns,0);
    atomic_init(&s->audio_capture_admitted,false);
    atomic_init(&s->audio_capture_inflight,0);
    atomic_init(&s->workload_epoch,1);
    atomic_init(&s->audio_capture_epoch,0);
	atomic_init(&s->live_signal_flags, 0);
	atomic_init(&s->live_audio_permille, 0);
	s->context = context;
	s->data_driver = hs_data_driver_create();
	s->effect_hotkeys = OBS_INVALID_HOTKEY_PAIR_ID;
	s->history_reset_hotkeys = OBS_INVALID_HOTKEY_PAIR_ID;
	s->kind = kind;
	s->scene = NULL;

	/* Configurable source dimensions (only meaningful for HYPER_KIND_SOURCE) */
	s->source_width = HYPER_SOURCE_WIDTH_DEFAULT;
	s->source_height = HYPER_SOURCE_HEIGHT_DEFAULT;

	/* Initialize sync primitives */
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&s->mutex, &attr);
	pthread_mutexattr_destroy(&attr);
	pthread_mutex_init(&s->audio_route_lock, NULL);

	/* Initialize Basic State */

	vec2_zero(&s->mouse_pos);
	vec2_zero(&s->mouse_click);

	/* Initialize Performance Stats */
	memset(&s->perf_stats, 0, sizeof(s->perf_stats));
	s->perf_stats.last_reset_time = os_gettime_ns();

	s->sound_enabled = false;

	/* Initialize audio subsystem */
	if (!hyper_audio_init(s)) {
		blog(LOG_ERROR, "[hypershader] Failed to initialize audio subsystem");
		s->audio_system_healthy = false;
	} else {
		s->audio_system_healthy = true;
	}

	s->audio_update_rate = 20;
	s->audio_test_tone = false;

	/* Initialize Shader Loading worker thread */
	pthread_mutex_init(&s->shader_load_mutex, NULL);
	pthread_cond_init(&s->shader_load_cond, NULL);
	s->shader_load_pending_task = NULL;

	extern void *shader_load_worker_thread(void *data); /* Forward decl */
	/* Publish running state before pthread_create: the worker may execute
	 * immediately, even before pthread_create returns to this thread. */
	s->shader_load_thread_run = true;
	if (pthread_create(&s->shader_load_thread, NULL, shader_load_worker_thread, s) != 0) {
		s->shader_load_thread_run = false;
		blog(LOG_ERROR, "[hypershader] Failed to create shader load worker thread");
	}
	s->audio_debug = false;
	s->audio_phase = 0.0f;
	s->obs_audio = NULL;
	s->audio_connected = false;
	s->audio_mix_idx = 0;
	s->audio_change_threshold = 0.01f; /* Throttle threshold (1-2% recommended, lower = more responsive) */

	/* Pre-allocate audio scratch buffer (avoids real-time allocation in audio callback) */
	s->audio_mono_scratch_size = 8192; /* Covers typical audio chunk sizes */
	s->audio_mono_scratch = bzalloc(s->audio_mono_scratch_size * sizeof(float));
	if (!s->audio_mono_scratch) {
		blog(LOG_WARNING, "[hypershader] Failed to allocate audio scratch buffer, using fallback path");
		s->audio_mono_scratch_size = 0;
	}

	/* Initialize per-instance debug state */
	s->audio_debug_tick = 0;
	s->audio_debug_last_level = -1.0f;
	s->audio_debug_last_peak = -1.0f;
	memset(s->audio_debug_last_bands, 0xFF, sizeof(s->audio_debug_last_bands)); /* -1.0f pattern */

	/* Initialize hot reload state */
	s->hot_reload_state.backup_effect = NULL;
	s->hot_reload_state.backup_error = NULL;
	s->hot_reload_state.last_reload_attempt = 0;
	s->hot_reload_state.reload_pending = false;
	s->hot_reload_state.recovery_mode = false;
	s->hot_reload_state.consecutive_failures = 0;

	/* Initialize plugin data path for security validation & model loading */
	s->data_path = hs_plugin_data_path();
	if (!s->data_path) {
		blog(LOG_ERROR, "[hypershader] ┌─ Data path unavailable");
		blog(LOG_ERROR, "[hypershader] └─ Action: plugin data path could not be resolved");
		filter_destroy(s);
		return NULL;
	}

	/* MediaPipe Initialization */
	s->mp_enabled = false;
	s->mp_analyze_trigger = false;
	s->mp_landmarks_tex = NULL;

	/* Native models load below only when the instance requests them. */

	/* Pre-allocate audio worker local buffer (avoids bzalloc per iteration) */
	s->audio_localbuf_size = 16384;
	s->audio_localbuf = bzalloc(s->audio_localbuf_size * sizeof(float));
	if (!s->audio_localbuf) {
		blog(LOG_WARNING, "[hypershader] Failed to allocate audio worker buffer, using fallback");
		s->audio_localbuf_size = 0;
	}

	/* Synchronization is initialized for both modes to support switching
	 * from Sight to native tracking without recreating the OBS filter. */
	pthread_mutex_init(&s->mp_mutex, NULL);
	pthread_cond_init(&s->mp_cond, NULL);
	s->mp_sync_ready = true;
	s->mp_thread_run = false;
	s->mp_pending_task_active = false;
	if (!settings || !obs_data_get_bool(settings, "sight_use_tracking"))
		hs_ensure_native_mediapipe(s);
	else
		blog(LOG_INFO, "[hypershader] Sight selected: skipping redundant native MediaPipe models");

	/* Entity scenes are allocated only by a compatible compiled effect. */

#ifdef __linux__
	s->x11_display = XOpenDisplay(NULL);
	if (!s->x11_display) {
		blog(LOG_WARNING, "[hypershader] Failed to open X11 display for cursor tracking");
	}
#endif

	/* Channel render targets are allocated on demand. */
	obs_enter_graphics();

	/* Create 1x1 black texture (Bug G fix) */
	uint32_t black = 0xFF000000;
	const uint8_t *data = (const uint8_t *)&black;
	s->dummy_texture = gs_texture_create(1, 1, GS_RGBA, 1, &data, GS_DYNAMIC);

	obs_leave_graphics();

	for (int i = 0; i < AUDIO_MAP_SLOTS; i++) {
		s->audio_map_name[i] = NULL;
		s->audio_map_feature[i] = 0;
		s->audio_map_enabled[i] = false;
		s->audio_map_param[i] = NULL;
	}

	/* Initialize audio bands parameter cache */
	for (int i = 0; i < 8; i++) {
		s->ep_audio_band[i] = NULL;
	}

	proc_handler_add(obs_source_get_proc_handler(context),
		"void hypershader_live_status(out bool rendering, out bool audio, out int level, out int signals)",
		hs_live_status, s);
    if (kind != HYPER_KIND_TRANSITION) {
        proc_handler_add(obs_source_get_proc_handler(context),
            "void hypershader_reset_history()", request_history_reset_proc, s);
        /* OBS marks filters private and rejects the single-hotkey API.
         * A source-scoped pair works for both filters and generators. */
        s->history_reset_hotkeys = obs_hotkey_pair_register_source(context,
            "effect.reset_history", obs_module_text("Effect.ResetHistory"),
            "effect.cancel_history_reset", obs_module_text("Effect.CancelHistoryReset"),
            history_reset_pressed, history_cancel_pressed, s, s);
    }


	/* Ensure sound settings follow any updated 'audio_mix' setting as default */
	if (settings) {
		s->sound_mix = (int)obs_data_get_int(settings, "sound_mix");
		filter_update(s, settings);
	}

	return s;
}

static void *filter_create_wrapper(obs_data_t *settings, obs_source_t *context)
{
	struct hypershader_data *s = hypershader_create(settings, context, HYPER_KIND_FILTER);
	if (s)
		s->effect_hotkeys = effect_hotkeys_register(context);
	return s;
}

static void *source_create_wrapper(obs_data_t *settings, obs_source_t *context)
{
	return hypershader_create(settings, context, HYPER_KIND_SOURCE);
}

static void *transition_create_wrapper(obs_data_t *settings, obs_source_t *context)
{
	return hypershader_create(settings, context, HYPER_KIND_TRANSITION);
}

static enum gs_color_space filter_get_color_space(void *data, size_t count, const enum gs_color_space *preferred)
{
	struct hypershader_data *s = data;
	obs_source_t *target = obs_filter_get_target(s->context);
	enum gs_color_space cs = GS_CS_SRGB;

	if (target) {
		cs = obs_source_get_color_space(target, count, preferred);
	}

	return cs;
}

/*
 * video_tick: Called every frame on the graphics thread before video_render.
 * Registered on all three source types so OBS knows this plugin supports
 * per-frame updates and color space awareness.
 */
static int sight_gesture_code(const char *name)
{
	static const char *names[] = {"Closed_Fist","Open_Palm","Pointing_Up",
		"Thumb_Down","Thumb_Up","Victory","ILoveYou"};
	for (int i = 0; name && i < 7; ++i)
		if (strcmp(name, names[i]) == 0) return i + 1;
	return 0;
}

static void sight_clear_tracking(struct hypershader_data *s)
{
    hs_touch_reset(&s->sight_touch);
	obs_enter_graphics();
	hs_sight_upload_rgba32f(&s->sight_points_tex,0,0,NULL);
	hs_sight_upload_rgba32f(&s->sight_hands_tex,0,0,NULL);
	hs_sight_upload_rgba32f(&s->sight_blendshape_tex,0,0,NULL);
	hs_sight_upload_rgba32f(&s->sight_transform_tex,0,0,NULL);
	if (s->sight_face_surface)
		hs_face_surface_render(s->sight_face_surface,NULL,0);
	obs_leave_graphics();
	s->sight_sequence=0;
	s->sight_kind=s->sight_point_count=s->sight_hand_count=0;
	s->sight_pinch[0]=s->sight_pinch[1]=0.f;
	s->sight_gesture[0]=s->sight_gesture[1]=0;
}

static void sight_update_tracking(struct hypershader_data *s,
				 const sight_snapshot_v1 *snapshot, bool available)
{
	if (!available || !snapshot || !snapshot->point_count) {
		if (s->sight_sequence || s->sight_points_tex) sight_clear_tracking(s);
		return;
	}
	if (s->sight_sequence==snapshot->sequence && s->sight_kind==snapshot->kind)
		return;
	obs_enter_graphics();
	bool ok=hs_sight_upload_rgba32f(&s->sight_points_tex,snapshot->point_count,1,
					(const float *)snapshot->points);
	if (snapshot->kind==SIGHT_SNAPSHOT_HAND && snapshot->hands_count) {
		ok &= hs_sight_upload_rgba32f(&s->sight_hands_tex,21,2,
					     (const float *)snapshot->hands);
	} else hs_sight_upload_rgba32f(&s->sight_hands_tex,0,0,NULL);
	if (snapshot->kind==SIGHT_SNAPSHOT_FACE) {
		float blends[64*4]={0};
		for (uint32_t i=0;i<snapshot->blendshape_count;i++)
			blends[i*4]=snapshot->blendshapes[i];
		ok &= hs_sight_upload_rgba32f(&s->sight_blendshape_tex,64,1,blends);
		if (snapshot->has_head_transform)
			ok &= hs_sight_upload_rgba32f(&s->sight_transform_tex,4,1,
						     snapshot->head_transform);
		else hs_sight_upload_rgba32f(&s->sight_transform_tex,0,0,NULL);
	} else {
		hs_sight_upload_rgba32f(&s->sight_blendshape_tex,0,0,NULL);
		hs_sight_upload_rgba32f(&s->sight_transform_tex,0,0,NULL);
	}
	if (ok && snapshot->kind==SIGHT_SNAPSHOT_FACE && snapshot->point_count>=468 &&
	    (s->ep_face_surface || s->ep_face_triangles)) {
		if (!s->sight_face_surface) {
			char *path=obs_module_file("effects/face_surface.effect");
			if (path) {s->sight_face_surface=hs_face_surface_create(path);bfree(path);}
		}
		if (s->sight_face_surface)
			ok &= hs_face_surface_render(s->sight_face_surface,
					(const float *)snapshot->points,snapshot->point_count);
	} else if (s->sight_face_surface)
		hs_face_surface_render(s->sight_face_surface,NULL,0);
	obs_leave_graphics();
	if (!ok) { sight_clear_tracking(s);return; }
	s->sight_sequence=snapshot->sequence;
	s->sight_kind=snapshot->kind;
	s->sight_point_count=snapshot->point_count;
	s->sight_hand_count=snapshot->hands_count;
	for (int i=0;i<2;i++) {
		s->sight_pinch[i]=snapshot->pinch[i];
		s->sight_gesture[i]=sight_gesture_code(snapshot->gesture[i]);
	}
	if (!s->sight_announced) {
		blog(LOG_INFO,"[hypershader] Sight shared tracking: channel=%s kind=%u count=%u",
		     s->sight_render_channel,snapshot->kind,snapshot->point_count);
		s->sight_announced=true;
	}
}

static void sight_depth_tick(struct hypershader_data *s,bool enabled)
{
    s->sight_depth_present=false;
    bool surface=s->ep_sight_surface_uv||s->ep_sight_surface_present;
    if(!enabled||(!s->ep_sight_depth&&!s->ep_sight_depth_present&&!surface)){
        s->sight_surface_depth.sequence=0;return;
    }
    sight_depth_v1 snap;
    if(!hs_sight_depth_read(s->context,surface?(s->sight_surface_depth.sequence==s->sight_depth_sequence?s->sight_depth_sequence:0):s->sight_depth_sequence,&snap)){
        s->sight_surface_depth.sequence=0;return;
    }
    if(surface&&snap.sequence!=s->sight_surface_depth.sequence)
        hs_surface_depth_set(&s->sight_surface_depth,snap.pixels,snap.width,snap.height,
                             snap.source_width,snap.source_height,snap.sequence);
    if(snap.sequence!=s->sight_depth_sequence){
        obs_enter_graphics();
        if(!s->sight_depth_tex||gs_texture_get_width(s->sight_depth_tex)!=snap.width||
           gs_texture_get_height(s->sight_depth_tex)!=snap.height){
            if(s->sight_depth_tex)gs_texture_destroy(s->sight_depth_tex);
            s->sight_depth_tex=gs_texture_create(snap.width,snap.height,GS_R8,1,NULL,GS_DYNAMIC);
        }
        if(s->sight_depth_tex){
            gs_texture_set_image(s->sight_depth_tex,snap.pixels,snap.width,false);
            s->sight_depth_sequence=snap.sequence;
        }
        obs_leave_graphics();
    }
    s->sight_depth_present=s->sight_depth_tex&&s->sight_depth_sequence==snap.sequence;
    if(s->sight_depth_present&&!s->sight_depth_announced){
        s->sight_depth_announced=true;
        blog(LOG_INFO,"[hypershader] SIGHT_DEPTH_BRIDGE_20261118: %ux%u R8 relative inverse depth",snap.width,snap.height);
    }
}

static void sight_matte_tick(struct hypershader_data *s, bool enabled)
{
    // On demand; skip copy and GPU upload for unchanged sequence.
    s->sight_matte_present = false;
    if (!enabled || (!s->ep_segmentation && !s->ep_sight_matte_present &&
        !s->ep_sight_subject_present && !s->ep_sight_subject_bounds &&
        !s->ep_sight_subject_center && !s->ep_sight_subject_coverage)) return;
    sight_matte_v1 snapshot;
    if (!hs_sight_matte_read(s->context,s->sight_matte_sequence,&snapshot)) return;
    if (snapshot.sequence != s->sight_matte_sequence) {
        const struct hs_sight_subject_stats subject =
            hs_sight_subject_measure(snapshot.pixels,snapshot.width,snapshot.height);
        obs_enter_graphics();
        if (!s->sight_matte_tex || gs_texture_get_width(s->sight_matte_tex)!=snapshot.width ||
            gs_texture_get_height(s->sight_matte_tex)!=snapshot.height) {
            if (s->sight_matte_tex) gs_texture_destroy(s->sight_matte_tex);
            s->sight_matte_tex=gs_texture_create(snapshot.width,snapshot.height,GS_R8,1,NULL,GS_DYNAMIC);
        }
        if (s->sight_matte_tex) {
            gs_texture_set_image(s->sight_matte_tex,snapshot.pixels,snapshot.width,false);
            memcpy(s->sight_subject_bounds,subject.bounds,sizeof(subject.bounds));
            memcpy(s->sight_subject_center,subject.center,sizeof(subject.center));
            s->sight_subject_coverage=subject.coverage;
            s->sight_subject_present=subject.present;
            s->sight_matte_sequence=snapshot.sequence;
        }
        obs_leave_graphics();
    }
    s->sight_matte_present=s->sight_matte_tex && s->sight_matte_sequence==snapshot.sequence;
    if (s->sight_matte_present && !s->sight_matte_announced) {
        s->sight_matte_announced=true;
        blog(LOG_INFO,"[hypershader] SIGHT_SHARED_MATTE_20261117: %ux%u R8",snapshot.width,snapshot.height);
    }
}

static void sight_flow_tick(struct hypershader_data *s,bool enabled)
{
    s->sight_flow_present=false;s->sight_surface_present=false;
    const bool surface=s->ep_sight_surface_uv||s->ep_sight_surface_present;
    if(!enabled||!surface){hs_surface_reset(&s->sight_surface);s->sight_surface_uploaded=0;}
    s->sight_flow_interval=0;s->sight_flow_coverage=0;
    if(!enabled||(!s->ep_sight_flow&&!s->ep_sight_flow_present&&
       !s->ep_sight_flow_interval&&!s->ep_sight_flow_coverage&&!s->ep_sight_flow_step&&!surface))return;
    sight_flow_v1 snap;
    uint64_t predecessor=0;bool linked=false;
    if(!hs_sight_flow_read_linked(s->context,&snap,&predecessor,surface?&linked:NULL))return;
    if(surface&&linked){
        hs_surface_update_depth(&s->sight_surface,&snap,predecessor,s->sight_depth_present?&s->sight_surface_depth:NULL);
        if(s->sight_surface_uploaded!=snap.sequence){
            obs_enter_graphics();
            if(!s->sight_surface_texture)
                s->sight_surface_texture=gs_texture_create(SIGHT_FLOW_WIDTH,SIGHT_FLOW_HEIGHT,GS_RGBA32F,1,NULL,GS_DYNAMIC);
            if(s->sight_surface_texture){
                gs_texture_set_image(s->sight_surface_texture,(const uint8_t*)s->sight_surface.field,SIGHT_FLOW_WIDTH*16,false);
                s->sight_surface_uploaded=snap.sequence;
            }
            obs_leave_graphics();
        }
        s->sight_surface_present=s->sight_surface_texture&&s->sight_surface_uploaded==snap.sequence;
    }else if(surface){hs_surface_reset(&s->sight_surface);s->sight_surface_uploaded=0;}
    if(snap.sequence!=s->sight_flow_sequence) {
        obs_enter_graphics();
        if(!s->sight_flow_texture)
            s->sight_flow_texture=gs_texture_create(SIGHT_FLOW_WIDTH,SIGHT_FLOW_HEIGHT,GS_RGBA32F,1,NULL,GS_DYNAMIC);
        if(s->sight_flow_texture) {
            gs_texture_set_image(s->sight_flow_texture,(const uint8_t*)snap.cells,SIGHT_FLOW_WIDTH*16,false);
            s->sight_flow_sequence=snap.sequence;
        }
        obs_leave_graphics();
    }
    s->sight_flow_present=s->sight_flow_texture&&s->sight_flow_sequence==snap.sequence;
    if(s->sight_flow_present) {
        s->sight_flow_interval=snap.interval_seconds;
        s->sight_flow_coverage=snap.coverage;
        if(!s->sight_flow_announced){
            s->sight_flow_announced=true;
            blog(LOG_INFO,"[hypershader] SIGHT_FLOW_BRIDGE_20261117: 32x18 backward motion grid");
        }
    }
}

static void sight_surface_bind(struct hypershader_data *s,bool selected)
{
    const bool present=selected&&s->sight_surface_present;
    if(s->ep_sight_surface_present)gs_effect_set_float(s->ep_sight_surface_present,present?1.f:0.f);
    if(s->ep_sight_surface_uv)gs_effect_set_texture(s->ep_sight_surface_uv,present?s->sight_surface_texture:s->dummy_texture);
}

static void sight_text_tick(struct hypershader_data *s,bool enabled)
{
    s->sight_text_present=false;
    s->sight_text_count=0;
    if(!enabled||(!s->ep_sight_text_mask&&!s->ep_sight_text_bounds&&
                  !s->ep_sight_text_present&&!s->ep_sight_text_count))return;
    sight_text_v1 snapshot;
    if(!hs_sight_text_read(s->context,&snapshot))return;
    if(snapshot.sequence!=s->sight_text_sequence) {
        uint8_t *pixels=bmalloc(HS_TEXT_EDGE*HS_TEXT_EDGE);
        int16_t *scratch=bmalloc(sizeof(*scratch)*HS_TEXT_EDGE*(HS_TEXT_EDGE+1));
        hs_text_raster(&snapshot,pixels,scratch);
        bfree(scratch);
        float bounds[SIGHT_TEXT_MAX_LINES*4]={0};
        for(uint32_t i=0;i<snapshot.count;++i) {
            const sight_text_line_v1 *b=&snapshot.lines[i];
            bounds[4*i]=b->x0;bounds[4*i+1]=b->y0;
            bounds[4*i+2]=b->x1;bounds[4*i+3]=b->y1;
        }
        obs_enter_graphics();
        if(!s->sight_text_mask)
            s->sight_text_mask=gs_texture_create(HS_TEXT_EDGE,HS_TEXT_EDGE,GS_R8,1,NULL,GS_DYNAMIC);
        bool ok=s->sight_text_mask!=NULL;
        if(ok)gs_texture_set_image(s->sight_text_mask,pixels,HS_TEXT_EDGE,false);
        ok=hs_sight_upload_rgba32f(&s->sight_text_bounds,SIGHT_TEXT_MAX_LINES,1,bounds)&&ok;
        if(ok)s->sight_text_sequence=snapshot.sequence;
        obs_leave_graphics();
        bfree(pixels);
    }
    s->sight_text_present=s->sight_text_sequence==snapshot.sequence&&s->sight_text_mask&&s->sight_text_bounds;
    if(s->sight_text_present) {
        s->sight_text_count=snapshot.count;
        if(!s->sight_text_announced) {
            s->sight_text_announced=true;
            blog(LOG_INFO,"[hypershader] SIGHT_TEXT_GEOMETRY_20261117: shared OCR line mask");
        }
    }
}

static void sight_semantic_tick(struct hypershader_data *s,bool enabled)
{
    s->sight_semantic_present=false;s->sight_semantic_count=0;
    if(!enabled||(!s->ep_sight_semantic_present&&!s->ep_sight_semantic_count&&
        !s->ep_sight_semantic_bounds&&!s->ep_sight_semantic_meta&&!s->ep_sight_semantic_mask))return;
    sight_semantic_v1 snap;
    if(!hs_sight_semantic_read(s->context,&snap))return;
    if(snap.sequence!=s->sight_semantic_sequence){
        float bounds[SIGHT_SEMANTIC_MAX_REGIONS*4],meta[SIGHT_SEMANTIC_MAX_REGIONS*4];
        hs_semantic_pack(&snap,bounds,meta);
        sight_text_v1 raster={0};raster.count=snap.count;
        for(uint32_t i=0;i<snap.count;++i){
            const sight_semantic_region_v1 *r=&snap.regions[i];
            raster.lines[i]=(sight_text_line_v1){r->x0,r->y0,r->x1,r->y1,r->confidence};
        }
        uint8_t *pixels=bmalloc(HS_TEXT_EDGE*HS_TEXT_EDGE);
        int16_t *scratch=bmalloc(sizeof(*scratch)*HS_TEXT_EDGE*(HS_TEXT_EDGE+1));
        hs_text_raster(&raster,pixels,scratch);bfree(scratch);
        obs_enter_graphics();
        if(!s->sight_semantic_mask)
            s->sight_semantic_mask=gs_texture_create(HS_TEXT_EDGE,HS_TEXT_EDGE,GS_R8,1,NULL,GS_DYNAMIC);
        bool ok=s->sight_semantic_mask!=NULL;
        if(ok)gs_texture_set_image(s->sight_semantic_mask,pixels,HS_TEXT_EDGE,false);
        ok=hs_sight_upload_rgba32f(&s->sight_semantic_bounds,SIGHT_SEMANTIC_MAX_REGIONS,1,bounds)&&ok;
        ok=hs_sight_upload_rgba32f(&s->sight_semantic_meta,SIGHT_SEMANTIC_MAX_REGIONS,1,meta)&&ok;
        if(ok)s->sight_semantic_sequence=snap.sequence;
        obs_leave_graphics();bfree(pixels);
    }
    s->sight_semantic_present=s->sight_semantic_sequence==snap.sequence&&s->sight_semantic_mask&&
        s->sight_semantic_bounds&&s->sight_semantic_meta;
    if(s->sight_semantic_present)s->sight_semantic_count=snap.count;
}
static void sight_semantic_bind(struct hypershader_data *s,bool selected)
{
    const bool present=selected&&s->sight_semantic_present;
    if(s->ep_sight_semantic_present)gs_effect_set_float(s->ep_sight_semantic_present,present?1.f:0.f);
    if(s->ep_sight_semantic_count)gs_effect_set_int(s->ep_sight_semantic_count,present?(int)s->sight_semantic_count:0);
    if(s->ep_sight_semantic_bounds)gs_effect_set_texture(s->ep_sight_semantic_bounds,present?s->sight_semantic_bounds:s->dummy_texture);
    if(s->ep_sight_semantic_meta)gs_effect_set_texture(s->ep_sight_semantic_meta,present?s->sight_semantic_meta:s->dummy_texture);
    if(s->ep_sight_semantic_mask)gs_effect_set_texture(s->ep_sight_semantic_mask,present?s->sight_semantic_mask:s->dummy_texture);
}

/* Only copies Sight's current immutable channel snapshot, never starts a model. */
static void sight_tick(struct hypershader_data *s)
{
	bool enabled;
	char channel[97]={0};
	pthread_mutex_lock(&s->mutex);
	enabled=s->sight_use_tracking;
	memcpy(channel,s->sight_channel,sizeof(channel));
	pthread_mutex_unlock(&s->mutex);
    s->sight_motion_present=false;
    s->sight_motion_energy=0.f;
    bool mod_motion = atomic_load_explicit(&s->mod_motion_requested, memory_order_relaxed);
    if (enabled && (s->ep_sight_motion_present || s->ep_sight_motion_energy || mod_motion)) {
        sight_motion_v1 motion;
        if (hs_sight_motion_read(s->context,&motion)) {
            s->sight_motion_present=true;
            s->sight_motion_energy=motion.energy;
            if (!s->sight_motion_announced) {
                s->sight_motion_announced=true;
                blog(LOG_INFO,"[hypershader] SIGHT_MOTION_ENERGY_20261117: source-local worker motion");
            }
        }
    }
	sight_matte_tick(s,enabled);
    sight_text_tick(s,enabled);
    sight_semantic_tick(s,enabled);
    sight_depth_tick(s,enabled);
    sight_flow_tick(s,enabled);
	if (!enabled) {
		if (s->sight_receiver) {
			obs_source_release(s->sight_receiver);
			s->sight_receiver=NULL;
		}
		if (s->sight_sequence || s->sight_points_tex) sight_clear_tracking(s);
		s->sight_render_channel[0]=0;
		s->sight_retry_ticks=0;
		return;
	}
	if (strcmp(channel,s->sight_render_channel)!=0) {
		sight_clear_tracking(s);
		snprintf(s->sight_render_channel,sizeof(s->sight_render_channel),"%s",channel);
		s->sight_announced=false;
	}
	/* OBS may load plugins in either order: retry with a bounded cadence. */
	if (!s->sight_receiver && (s->sight_retry_ticks++ % 120)==0)
		s->sight_receiver=hs_sight_reader_create();
	sight_snapshot_v1 snapshot;
	const bool fresh=hs_sight_reader_read(s->sight_receiver,channel,750,&snapshot);
	sight_update_tracking(s,&snapshot,fresh);
    if(s->ep_sight_touch[0] || s->ep_sight_ripple[0]){
        obs_source_t* target=obs_filter_get_target(s->context);
        uint32_t w=target?obs_source_get_base_width(target):0;
        uint32_t h=target?obs_source_get_base_height(target):0;
        hs_touch_consume(&s->sight_touch,fresh?&snapshot:NULL,w,h,(double)os_gettime_ns()/1e9);
    }else if(s->sight_touch.sequence)hs_touch_reset(&s->sight_touch);
}

static void filter_tick(void *data, float seconds)
{
	struct hypershader_data *s = data;
	if (!s)
		return;
	s->last_tick_delta = seconds;
	s->frame_count += 1;
    const bool active=workload_demand(s);
    workload_set_active(s,active);
    if (active) {
        sight_tick(s);
    } else {
        /* Drop presence immediately, but keep compatible GPU allocations.
         * The next active tick reads a fresh provider snapshot. */
        s->sight_point_count=0;
        s->sight_sequence=0;
        s->sight_matte_present=s->sight_motion_present=s->sight_depth_present=false;
        s->sight_flow_present=s->sight_surface_present=false;
        s->sight_text_present=s->sight_semantic_present=false;
        if (s->sight_touch.sequence) hs_touch_reset(&s->sight_touch);
        if (s->sight_surface_uploaded) {
            hs_surface_reset(&s->sight_surface);
            s->sight_surface_uploaded=0;
        }
    }
    /* A scene with an active entityTexture shader has actual evolving physics.
     * Do not spend cycles updating unused demo scenes in every filter. */
    if (active && seconds > 0.0f && pthread_mutex_trylock(&s->mutex) == 0) {
        if (s->scene && (s->ep_entityTexture || s->ep_entities))
            scene_update(s->scene, s->mp_enabled &&
                atomic_load_explicit(&s->mp_result_epoch,memory_order_acquire)==
                atomic_load_explicit(&s->mp_epoch,memory_order_acquire) ? s->mp_runner : NULL,
                seconds * 1000.0f);
        pthread_mutex_unlock(&s->mutex);
    }
    /* hs_graph_frame and physical frame_count remain real render counters for
     * feedback initialization. hs_frame_index is virtual, governed by global speed. */
    s->shader_frame_count += s->time_scale;
	if (seconds > 0.0f)
		s->time_accumulator += seconds * s->time_scale;
}

static bool mp_epoch_current(struct hypershader_data *s,unsigned epoch)
{
    return s->mp_thread_run && s->mp_enabled &&
        atomic_load_explicit(&s->workload_active,memory_order_acquire) &&
        epoch==atomic_load_explicit(&s->mp_epoch,memory_order_acquire);
}

static bool mp_result_current(struct hypershader_data *s)
{
    return mp_epoch_current(s,atomic_load_explicit(&s->mp_result_epoch,memory_order_acquire));
}

/* TASK executed on graphics thread to upload MediaPipe results */
static void mp_update_textures_task(void *param)
{
	struct mp_update_task_data *task = param;
	if (!task || !task->s) {
		if (task) {
			if (task->mask_buffer)
				bfree(task->mask_buffer);
			if (task->landmark_data)
				bfree(task->landmark_data);
			if (task->ref_landmark_data)
				bfree(task->ref_landmark_data);
			if (task->s)
				obs_source_release(task->s->context);
			bfree(task);
		}
		return;
	}

	struct hypershader_data *s = task->s;

    /* Validate inside the graphics context, after any concurrent settings
     * update; stale results never modify live textures or reference data. */
    obs_enter_graphics();
    if (!mp_epoch_current(s,task->epoch)) {
        obs_leave_graphics();
        goto release_task;
    }
    if (!task->mask_buffer && s->mp_mask_tex) {
        gs_texture_destroy(s->mp_mask_tex);
        s->mp_mask_tex=NULL;
    }
	if (task->mask_buffer) {
		uint32_t cw = s->mp_mask_tex ? gs_texture_get_width(s->mp_mask_tex) : 0;
		uint32_t ch = s->mp_mask_tex ? gs_texture_get_height(s->mp_mask_tex) : 0;

		if (!s->mp_mask_tex || cw != (uint32_t)task->mask_width || ch != (uint32_t)task->mask_height) {
			if (s->mp_mask_tex)
				gs_texture_destroy(s->mp_mask_tex);
			s->mp_mask_tex =
				gs_texture_create(task->mask_width, task->mask_height, GS_R8, 1, NULL, GS_DYNAMIC);
		}
		if (s->mp_mask_tex) {
			gs_texture_set_image(s->mp_mask_tex, task->mask_buffer, task->mask_width, false);
		}
	}

	if (!task->landmark_data && s->mp_landmarks_tex) {
		gs_texture_destroy(s->mp_landmarks_tex);
		s->mp_landmarks_tex = NULL;
	}
	if (task->landmark_data) {
		uint32_t cw = s->mp_landmarks_tex ? gs_texture_get_width(s->mp_landmarks_tex) : 0;

		if (!s->mp_landmarks_tex || cw != (uint32_t)task->landmark_count) {
			if (s->mp_landmarks_tex)
				gs_texture_destroy(s->mp_landmarks_tex);
			s->mp_landmarks_tex =
				gs_texture_create(task->landmark_count, 1, GS_RGBA32F, 1, NULL, GS_DYNAMIC);
		}
		if (s->mp_landmarks_tex) {
			gs_texture_set_image(s->mp_landmarks_tex, (uint8_t *)task->landmark_data,
					     task->landmark_count * 16, false);
		}
	}

	/* Optional GPU face topology, only created for effects requesting it.
	 * No extra GPU drawing is performed for ordinary landmark-only shaders. */
	if (s->ep_face_surface || s->ep_face_triangles) {
		if (!s->mp_face_surface && task->landmark_data) {
			char *effect_path = obs_module_file("effects/face_surface.effect");
			if (effect_path) {
				s->mp_face_surface = hs_face_surface_create(effect_path);
				bfree(effect_path);
			}
		}
		if (s->mp_face_surface)
			hs_face_surface_render(s->mp_face_surface, task->landmark_data,
					       (size_t)task->landmark_count);
	} else if (s->mp_face_surface && !task->landmark_data) {
		/* A previously active surface must not retain a lost face. */
		hs_face_surface_render(s->mp_face_surface, NULL, 0);
	}

	if (task->ref_landmark_data) {
		uint32_t cw = s->mp_ref_landmarks_tex ? gs_texture_get_width(s->mp_ref_landmarks_tex) : 0;

		if (!s->mp_ref_landmarks_tex || cw != (uint32_t)task->ref_landmark_count) {
			if (s->mp_ref_landmarks_tex)
				gs_texture_destroy(s->mp_ref_landmarks_tex);
			s->mp_ref_landmarks_tex =
				gs_texture_create(task->ref_landmark_count, 1, GS_RGBA32F, 1, NULL, GS_DYNAMIC);
		}
		if (s->mp_ref_landmarks_tex) {
			gs_texture_set_image(s->mp_ref_landmarks_tex, (uint8_t *)task->ref_landmark_data,
					     task->ref_landmark_count * 16, false);
		}
	}

    atomic_store_explicit(&s->mp_result_epoch,task->epoch,memory_order_release);
	obs_leave_graphics();
release_task:
	if (task->mask_buffer)
		bfree(task->mask_buffer);
	if (task->landmark_data)
		bfree(task->landmark_data);
	if (task->ref_landmark_data)
		bfree(task->ref_landmark_data);
    atomic_store_explicit(&s->mp_frame_buffer_in_use,false,memory_order_release);
	obs_source_release(s->context);
	bfree(task);
}

/* A Builder Apply request writes a per-instance, non-scene status marker.
 * Only the exact matching requested revision may complete it; unrelated
 * filters, older async jobs and ordinary preset changes are ignored. */
static void publish_builder_compile_result(struct shader_load_task_data *task, bool passed, const char *error)
{
	if (!task || !task->s || !task->s->context || !task->path)
		return;
	obs_source_t *source = task->s->context;
	obs_data_t *private_data = obs_source_get_private_settings(source);
	obs_data_t *live_settings = obs_source_get_settings(source);
	if (!private_data || !live_settings) {
		if (live_settings)
			obs_data_release(live_settings);
		if (private_data)
			obs_data_release(private_data);
		return;
	}
	const char *expected = obs_data_get_string(private_data, "hypershader_builder_compile_path");
	const char *state = obs_data_get_string(private_data, "hypershader_builder_compile_state");
	const char *current = obs_data_get_string(live_settings, "shader_file");
	if (expected && state && current && strcmp(expected, task->path) == 0 &&
	    strcmp(current, task->path) == 0 && strcmp(state, "pending") == 0) {
		obs_data_set_string(private_data, "hypershader_builder_compile_state", passed ? "succeeded" : "failed");
		obs_data_set_string(private_data, "hypershader_builder_compile_error",
				    passed ? "" : (error ? error : "Unknown shader load error"));
	}
	/* Every selection, not just Builder revisions, publishes its actual
	 * compile identity. Dock controls must never edit a stale schema. */
	const char *queued = obs_data_get_string(task->settings, "shader_file");
	if (current && queued && strcmp(current, queued) == 0) {
		obs_data_set_string(private_data, "hypershader_live_path", current);
		obs_data_set_string(private_data, "hypershader_live_state", passed ? "ready" : "failed");
		obs_data_set_int(private_data, "hypershader_live_revision",
			obs_data_get_int(private_data, "hypershader_live_revision") + 1);
	}

	obs_data_release(live_settings);
	obs_data_release(private_data);
}

static void shader_load_complete_task(void *param)
{
	struct shader_load_task_data *task = param;
	if (!task || !task->s) {
		if (task) {
			if (task->path)
				bfree(task->path);
			if (task->settings)
				obs_data_release(task->settings);
			if (task->s)
				obs_source_release(task->s->context);
			bfree(task);
		}
		return;
	}

	struct hypershader_data *s = task->s;

	/* Check if source has been destroyed */
	if (!s->shader_load_thread_run) {
		if (task->path)
			bfree(task->path);
		if (task->settings)
			obs_data_release(task->settings);
		obs_source_release(s->context);
		bfree(task);
		return;
	}

	/* OBS_TASK_GRAPHICS may execute without a current graphics context.
	 * Enter explicitly before creating or destroying effect objects. */
	obs_enter_graphics();
	shader_resource_t *res = load_shader_resource(s, task->path);
	const bool compiled = res && res->effect && res->rc;

	if (compiled) {
		/* The async shader loader previously applied metadata defaults to
		 * the new GPU effect but discarded saved user slider values. The
		 * initial filter_update ran before an effect existed, so choosing a
		 * shader looked successful while P-Frame Loss stayed at its default.
		 * Transfer saved numeric uniforms onto the compiled effect now. */
		for (size_t i=0;i<res->rc->meta.num;i++) {
			struct shader_param_meta *m=&res->rc->meta.array[i];
			if (!m->cached_param || !m->name || hs_named_input_owns(&res->rc->meta, m->name))continue;
			char name[128];
			snprintf(name,sizeof(name),"param_%s",m->name);
			if (!obs_data_has_user_value(task->settings,name))continue;
			struct gs_effect_param_info info;
			gs_effect_get_param_info(m->cached_param,&info);
			if (info.type==GS_SHADER_PARAM_FLOAT)
				gs_effect_set_float(m->cached_param,
					(float)obs_data_get_double(task->settings,name));
			else if (info.type==GS_SHADER_PARAM_INT)
				gs_effect_set_int(m->cached_param,
					(int)obs_data_get_int(task->settings,name));
			else if (info.type==GS_SHADER_PARAM_BOOL)
				gs_effect_set_bool(m->cached_param,
					obs_data_get_bool(task->settings,name));
		}
		/* A task's settings are only an async snapshot. Persist metadata
		 * defaults in the *live source settings* as well, otherwise a later
		 * unrelated obs_source_update reads absent param_* values as zero.
		 * Do not overwrite explicit user values (including explicit zero) or
		 * write old shader defaults into a filter switched in the meantime. */
		obs_data_t *live_settings = obs_source_get_settings(s->context);
		const char *queued_shader = obs_data_get_string(task->settings, "shader_file");
		const char *current_shader = live_settings ? obs_data_get_string(live_settings, "shader_file") : NULL;
		bool same_shader = current_shader && queued_shader && strcmp(current_shader, queued_shader) == 0;
		for (size_t i = 0; i < res->rc->meta.num; i++) {
			struct shader_param_meta *m = &res->rc->meta.array[i];
			if (!m->has_default || !m->name)
				continue;
			char prop_name[128];
			snprintf(prop_name, sizeof(prop_name), "param_%s", m->name);
			/* The color/string metadata defaults live in default_val_str.
			 * Persist only scalar values with their matching OBS type; absent
			 * vector defaults must retain the shader metadata fallback. */
			struct gs_effect_param_info param_info;
			if (!m->cached_param)
				continue;
			gs_effect_get_param_info(m->cached_param, &param_info);
			if (param_info.type == GS_SHADER_PARAM_FLOAT) {
				if (!obs_data_has_user_value(task->settings, prop_name))
					obs_data_set_double(task->settings, prop_name, m->default_val);
				if (same_shader && !obs_data_has_user_value(live_settings, prop_name))
					obs_data_set_double(live_settings, prop_name, m->default_val);
			} else if (param_info.type == GS_SHADER_PARAM_INT) {
				if (!obs_data_has_user_value(task->settings, prop_name))
					obs_data_set_int(task->settings, prop_name, (long long)m->default_val);
				if (same_shader && !obs_data_has_user_value(live_settings, prop_name))
					obs_data_set_int(live_settings, prop_name, (long long)m->default_val);
			} else if (param_info.type == GS_SHADER_PARAM_BOOL) {
				if (!obs_data_has_user_value(task->settings, prop_name))
					obs_data_set_bool(task->settings, prop_name, m->default_val != 0.0f);
				if (same_shader && !obs_data_has_user_value(live_settings, prop_name))
					obs_data_set_bool(live_settings, prop_name, m->default_val != 0.0f);
			}
		}
		if (live_settings)
			obs_data_release(live_settings);

		/* Atomic Swap */
		/* Atomic Swap using Graphics Context (avoids mutex in render thread) */
		pthread_mutex_lock(&s->mutex);
		gs_effect_t *old_effect = s->effect;
		struct shader_meta_rc *old_rc = s->metadata_rc;
		float *old_audio_values = s->meta_audio_values;

		s->effect = res->effect;
		s->metadata_rc = res->rc;
		s->meta_audio_values = res->audio_values;
		s->meta_audio_count = res->rc->meta.num;

		memcpy(s->channel_required, res->channel_required, sizeof(s->channel_required));

		/* Clear res ownership */
		res->effect = NULL;
		res->rc = NULL;
		res->audio_values = NULL;

		/* Resolve binding descriptors once per accepted compiled effect. */
		for (size_t i = 0; i < s->metadata_rc->meta.num; ++i) {
			struct shader_param_meta *m = &s->metadata_rc->meta.array[i];
			if (!m->name || !m->cached_param || hs_runtime_uniform(s, m->name))
				continue;
			struct gs_effect_param_info info;
			gs_effect_get_param_info(m->cached_param, &info);
			m->render_type = info.type;
			m->render_audio_signal = hs_metadata_signal_resolve(m->audio_tag);
			snprintf(m->render_key, sizeof(m->render_key), "param_%s", m->name);
			if (m->has_default && m->default_val_str) {
				struct vec4 *v = &m->render_default_color;
				if (info.type == GS_SHADER_PARAM_VEC3)
					m->render_default_color_valid = sscanf(m->default_val_str, "%f,%f,%f",
						&v->x, &v->y, &v->z) == 3;
				else if (info.type == GS_SHADER_PARAM_VEC4)
					m->render_default_color_valid = sscanf(m->default_val_str, "%f,%f,%f,%f",
						&v->x, &v->y, &v->z, &v->w) == 4;
			}
		}
		/* Update cached standard params for NEW effect */
		s->ep_image = gs_effect_get_param_by_name(s->effect, "image");
		s->ep_transition_progress = gs_effect_get_param_by_name(s->effect, "hs_transition_progress");
		s->ep_animation_playback = gs_effect_get_param_by_name(s->effect, "animation_playback");
		s->ep_time = gs_effect_get_param_by_name(s->effect, "hs_time");
		s->ep_scale = gs_effect_get_param_by_name(s->effect, "hs_scale");
		s->ep_resolution = gs_effect_get_param_by_name(s->effect, "hs_resolution");
		s->ep_delta_time = gs_effect_get_param_by_name(s->effect, "hs_delta_time");
		s->ep_frame_index = gs_effect_get_param_by_name(s->effect, "hs_frame_index");
		s->ep_date = gs_effect_get_param_by_name(s->effect, "hs_date");
		s->ep_frame_rate = gs_effect_get_param_by_name(s->effect, "hs_frame_rate");
		s->ep_sample_rate = gs_effect_get_param_by_name(s->effect, "hs_sample_rate");
		s->ep_pointer = gs_effect_get_param_by_name(s->effect, "hs_pointer");
		s->ep_history = gs_effect_get_param_by_name(s->effect, "hs_history");
		/* Both graph forms use the same validated schedule and GPU owners. */
		if (s->graph.enabled || res->graph.enabled)
			graph_release_targets(s);
		else
			graph_reset(s);
		s->graph = res->graph;
		s->graph_budget_rejected = false;
		s->ep_graph_frame = gs_effect_get_param_by_name(s->effect, "hs_graph_frame");
		if (s->graph.enabled && s->graph.valid) {
			if (s->graph.named)
				blog(LOG_INFO, "[hypershader] Named pass graph enabled: %u passes", s->graph.count);
			else
				blog(LOG_INFO, "[hypershader] Two-buffer graph enabled: A -> B -> Draw");
		}

		s->ep_segmentation = gs_effect_get_param_by_name(s->effect, "hs_segmentation");
		s->ep_sight_matte_present = gs_effect_get_param_by_name(s->effect, "hs_sight_matte_present");
        s->ep_sight_depth=gs_effect_get_param_by_name(s->effect,"hs_sight_depth");
        s->ep_sight_depth_present=gs_effect_get_param_by_name(s->effect,"hs_sight_depth_present");
        s->ep_sight_flow=gs_effect_get_param_by_name(s->effect,"hs_sight_flow");
        s->ep_sight_flow_present=gs_effect_get_param_by_name(s->effect,"hs_sight_flow_present");
        s->ep_sight_flow_interval=gs_effect_get_param_by_name(s->effect,"hs_sight_flow_interval");
        s->ep_sight_flow_coverage=gs_effect_get_param_by_name(s->effect,"hs_sight_flow_coverage");
        s->ep_sight_flow_step=gs_effect_get_param_by_name(s->effect,"hs_sight_flow_step");
        s->ep_sight_surface_uv=gs_effect_get_param_by_name(s->effect,"hs_sight_surface_uv");
        s->ep_sight_surface_present=gs_effect_get_param_by_name(s->effect,"hs_sight_surface_present");
        hs_surface_reset(&s->sight_surface);s->sight_surface_uploaded=0;s->sight_surface_present=false;
        s->ep_sight_text_present = gs_effect_get_param_by_name(s->effect, "hs_sight_text_present");
        s->ep_sight_semantic_present = gs_effect_get_param_by_name(s->effect, "hs_sight_semantic_present");
        s->ep_sight_semantic_count = gs_effect_get_param_by_name(s->effect, "hs_sight_semantic_count");
        s->ep_sight_semantic_bounds = gs_effect_get_param_by_name(s->effect, "hs_sight_semantic_bounds");
        s->ep_sight_semantic_meta = gs_effect_get_param_by_name(s->effect, "hs_sight_semantic_meta");
        s->ep_sight_semantic_mask = gs_effect_get_param_by_name(s->effect, "hs_sight_semantic_mask");
        s->ep_sight_text_count = gs_effect_get_param_by_name(s->effect, "hs_sight_text_count");
        s->ep_sight_text_mask = gs_effect_get_param_by_name(s->effect, "hs_sight_text_mask");
        s->ep_sight_text_bounds = gs_effect_get_param_by_name(s->effect, "hs_sight_text_bounds");
        s->ep_sight_motion_present = gs_effect_get_param_by_name(s->effect, "hs_sight_motion_present");
        s->ep_sight_motion_energy = gs_effect_get_param_by_name(s->effect, "hs_sight_motion_energy");
        s->ep_sight_subject_present = gs_effect_get_param_by_name(s->effect, "hs_sight_subject_present");
        s->ep_sight_subject_bounds = gs_effect_get_param_by_name(s->effect, "hs_sight_subject_bounds");
        s->ep_sight_subject_center = gs_effect_get_param_by_name(s->effect, "hs_sight_subject_center");
        s->ep_sight_subject_coverage = gs_effect_get_param_by_name(s->effect, "hs_sight_subject_coverage");
		s->ep_landmarks = gs_effect_get_param_by_name(s->effect, "hs_landmarks");
		s->ep_face_present = gs_effect_get_param_by_name(s->effect, "hs_face_present");
		s->ep_reference_landmarks = gs_effect_get_param_by_name(s->effect, "hs_reference_landmarks");
		s->ep_face_surface = gs_effect_get_param_by_name(s->effect, "hs_face_surface");
		s->ep_face_triangles = gs_effect_get_param_by_name(s->effect, "hs_face_triangles");
		s->ep_sight_points = gs_effect_get_param_by_name(s->effect, "hs_sight_points");
		s->ep_sight_hands = gs_effect_get_param_by_name(s->effect, "hs_sight_hands");
        hs_touch_reset(&s->sight_touch);
        for(unsigned i=0;i<2;i++){
            char key[32];snprintf(key,sizeof(key),"hs_sight_touch%u",i);
            s->ep_sight_touch[i]=gs_effect_get_param_by_name(s->effect,key);
        }
        for(unsigned i=0;i<HS_TOUCH_CAPACITY;i++){
            char key[32];snprintf(key,sizeof(key),"hs_sight_ripple%u",i);
            s->ep_sight_ripple[i]=gs_effect_get_param_by_name(s->effect,key);
        }
		s->ep_sight_blendshapes = gs_effect_get_param_by_name(s->effect, "hs_sight_blendshapes");
		s->ep_sight_head_rows = gs_effect_get_param_by_name(s->effect, "hs_sight_head_rows");
		s->ep_sight_present = gs_effect_get_param_by_name(s->effect, "hs_sight_present");
		s->ep_sight_kind = gs_effect_get_param_by_name(s->effect, "hs_sight_kind");
		s->ep_sight_point_count = gs_effect_get_param_by_name(s->effect, "hs_sight_point_count");
		s->ep_sight_hand_count = gs_effect_get_param_by_name(s->effect, "hs_sight_hand_count");
		s->ep_sight_pinch0 = gs_effect_get_param_by_name(s->effect, "hs_sight_pinch0");
		s->ep_sight_pinch1 = gs_effect_get_param_by_name(s->effect, "hs_sight_pinch1");
		s->ep_sight_gesture0 = gs_effect_get_param_by_name(s->effect, "hs_sight_gesture0");
		s->ep_sight_gesture1 = gs_effect_get_param_by_name(s->effect, "hs_sight_gesture1");

        s->ep_entityCount = gs_effect_get_param_by_name(s->effect, "entityCount");
        s->ep_entityTexture = gs_effect_get_param_by_name(s->effect, "entityTexture");
        s->ep_entities = gs_effect_get_param_by_name(s->effect, "entities");
        entity_scene_configure(s);

		/* Cache the source texture handles on the newly loaded effect.
		 * These must be refreshed on every shader swap. In particular, OBS
		 * transitions bind the actual A/B source textures via hs_input0/1;
		 * previously the uninitialized handles silently discarded both. */
		for (int i = 0; i < 4; i++) {
			char name[32];
			snprintf(name, sizeof(name), "hs_input%d", i);
			s->ep_input[i] = gs_effect_get_param_by_name(s->effect, name);
			s->channel_named[i] = false;
			snprintf(name, sizeof(name), "hs_input_resolution%d", i);
			s->ep_input_resolution[i] = gs_effect_get_param_by_name(s->effect, name);
            snprintf(name,sizeof(name),"hs_input_atlas%d",i);
            s->ep_input_atlas[i]=gs_effect_get_param_by_name(s->effect,name);
            memset(&s->atlas_clock[i],0,sizeof(s->atlas_clock[i]));
            vec4_set(&s->atlas_value[i],1,1,0,0);
		}

		/* Cache hs_input_time parameters */
		for (int i = 0; i < 4; i++) {
			char name[32];
			snprintf(name, sizeof(name), "hs_input_time%d", i);
			s->ep_input_time[i] = gs_effect_get_param_by_name(s->effect, name);
		}

		/* Named uniforms replace the numbered texture handle for the slot.
		 * Dimensions, timing, atlas and resource settings stay shared. */
		for (size_t i = 0; i < s->metadata_rc->meta.num; ++i) {
			const struct shader_param_meta *m = &s->metadata_rc->meta.array[i];
			if (m->input_slot > 0) {
				const int slot = m->input_slot - 1;
				char companion[160];
				s->ep_input[slot] = m->cached_param;
				s->channel_named[slot] = true;
				snprintf(companion, sizeof(companion), "%s_resolution", m->name);
				gs_eparam_t *resolution = gs_effect_get_param_by_name(s->effect, companion);
				if (resolution)
					s->ep_input_resolution[slot] = resolution;
				snprintf(companion, sizeof(companion), "%s_time", m->name);
				gs_eparam_t *time = gs_effect_get_param_by_name(s->effect, companion);
				if (time)
					s->ep_input_time[slot] = time;
			}
		}

		/* Cache audio band parameters */
		for (int i = 0; i < 8; i++) {
			char name[32];
			snprintf(name, sizeof(name), "audio_band%d", i);
			s->ep_audio_band[i] = gs_effect_get_param_by_name(s->effect, name);
		}

		s->ep_audio_reaction_state = gs_effect_get_param_by_name(s->effect, "audio_reaction_state");
        memset(&s->reaction_state,0,sizeof(s->reaction_state));
        s->reaction_frame_ns=0;
        s->reaction_input_key=0;
		s->ep_audio_level = gs_effect_get_param_by_name(s->effect, "audio_level");
		s->ep_audio_peak = gs_effect_get_param_by_name(s->effect, "audio_peak");
		/* Cache the beat timing parameters (absent in shaders that ignore them) */
		s->ep_audio_beat = gs_effect_get_param_by_name(s->effect, "audio_beat");
		s->ep_audio_envelope = gs_effect_get_param_by_name(s->effect, "audio_envelope");
        static const char *music_names[HS_MUSIC_FIELDS]={
            "audio_kick","audio_snare","audio_flux","audio_bpm",
            "audio_beat_phase","audio_bar_phase","audio_beat_confidence"};
        for(int i=0;i<HS_MUSIC_FIELDS;++i)
            s->ep_audio_music[i]=gs_effect_get_param_by_name(s->effect,music_names[i]);

		/* Cache param views for UI (avoids gs_effect inspection in properties callback) */
		cache_effect_params(s);

		/* A newly compiled effect must not inherit an unrelated shader's
		 * damaged reference frames, even if both use hs_history. */
		for (int i=0;i<2;i++) {
			if (s->history[i]) {
				gs_texrender_destroy(s->history[i]);
				s->history[i] = NULL;
			}
		}
		s->history_idx=0;
		s->feedback_width=s->feedback_height=0;
		s->feedback_first_render_logged=false;

		/* Post-Swap Cleanup */
		if (!s->ep_history && !s->graph.enabled) {
			if (s->feedback_input) {
				gs_texrender_destroy(s->feedback_input);
				s->feedback_input = NULL;
			}
			for (int i = 0; i < 2; i++) {
				if (s->history[i]) {
					gs_texrender_destroy(s->history[i]);
					s->history[i] = NULL;
				}
			}
		}

		schedule_property_refresh(s);

		/* Free old resources */
		if (old_effect) {
			gs_effect_destroy(old_effect);
		}
		if (old_rc)
			metadata_rc_release(&old_rc);
		if (old_audio_values)
			bfree(old_audio_values);

		if (s->shader_path)
			bfree(s->shader_path);
		s->shader_path = bstrdup(task->path);
		bfree(s->performance_compiled_path);
		s->performance_compiled_path = bstrdup(obs_data_get_string(task->settings, "shader_file"));
        obs_data_t *current_settings = obs_source_get_settings(s->context);
        cache_runtime_settings(s, current_settings ? current_settings : task->settings);
        if (current_settings)
            obs_data_release(current_settings);

#ifdef _DEBUG
		if (s->watcher)
			shader_watcher_destroy((struct shader_watcher *)s->watcher);
		s->watcher = shader_watcher_create(s->shader_path);
#endif

		if (s->shader_error) {
			bfree(s->shader_error);
			s->shader_error = NULL;
		}
		pthread_mutex_unlock(&s->mutex);

		blog(LOG_INFO, "[hypershader] Loaded shader: %s", task->path);

	} else {
		/* Error handling */
		if (res && res->error) {
			if (s->shader_error)
				bfree(s->shader_error);
			s->shader_error = bstrdup(res->error);
			blog(LOG_ERROR, "[hypershader] ┌─ Shader load failed");
			blog(LOG_ERROR, "[hypershader] ├─ Path : %s", task->path);
			blog(LOG_ERROR, "[hypershader] └─ Detail:\n%s", res->error);
		} else {
			if (s->shader_error)
				bfree(s->shader_error);
			s->shader_error = bstrdup("Unknown shader load error");
		}
		blog(LOG_ERROR, "[hypershader] ┌─ Shader load failed");
		blog(LOG_ERROR, "[hypershader] ├─ Path : %s", task->path);
		blog(LOG_ERROR, "[hypershader] └─ Action: see diagnostics above");
	}

	publish_builder_compile_result(task, compiled, res ? res->error : NULL);
	free_shader_resource(res);
	obs_leave_graphics();

	/* Cleanup task data */
	if (task->path)
		bfree(task->path);
	if (task->settings)
		obs_data_release(task->settings);

	obs_source_release(s->context);
	bfree(task);
}

void *shader_load_worker_thread(void *data)
{
	struct hypershader_data *s = data;

	/* The stop flag is checked under shader_load_mutex below. */
	for (;;) {
		struct shader_load_task_data *task_data = NULL;

		pthread_mutex_lock(&s->shader_load_mutex);
		while (s->shader_load_thread_run && !s->shader_load_pending_task) {
			pthread_cond_wait(&s->shader_load_cond, &s->shader_load_mutex);
		}
		if (!s->shader_load_thread_run) {
			pthread_mutex_unlock(&s->shader_load_mutex);
			break;
		}
		task_data = s->shader_load_pending_task;
		s->shader_load_pending_task = NULL;
		pthread_mutex_unlock(&s->shader_load_mutex);

		if (task_data) {
			/* Queue completion task to graphics thread */
			obs_source_get_ref(s->context);
			obs_queue_task(OBS_TASK_GRAPHICS, shader_load_complete_task, task_data, false);
		}
	}

	return NULL;
}

void *mp_worker_thread(void *data)
{
	struct hypershader_data *s = data;
    unsigned graph_epoch=0;

	while (s->mp_thread_run) {
		pthread_mutex_lock(&s->mp_mutex);
		while (s->mp_thread_run && !s->mp_pending_task_active) {
			pthread_cond_wait(&s->mp_cond, &s->mp_mutex);
		}
		if (!s->mp_thread_run) {
			pthread_mutex_unlock(&s->mp_mutex);
			break;
		}

		/* Copy task data locally so render thread can reuse storage immediately */
		struct mp_process_task_data task = s->mp_pending_task_storage;
		s->mp_pending_task_active = false;
		pthread_mutex_unlock(&s->mp_mutex);

        if (!mp_epoch_current(s,task.epoch))
            goto release_frame;
        /* No settings/video thread changes the runner's graph while it is
         * executing. Restart model state on a new workload generation. */
        if (graph_epoch!=task.epoch) {
            if (!mp_start_graph(s->mp_runner,task.graph))
                goto release_frame;
            graph_epoch=task.epoch;
        }
        if (!mp_epoch_current(s,task.epoch))
            goto release_frame;
        bool analyze_reference=atomic_exchange_explicit(&s->mp_analyze_trigger,false,memory_order_relaxed);
		/* Process frame with MediaPipe */
		if (mp_process_frame(s->mp_runner, task.frame_data, task.width, task.height, task.width * 4) &&
            mp_epoch_current(s,task.epoch)) {

			struct mp_update_task_data *update_data = bzalloc(sizeof(*update_data));
			update_data->s = s;
            update_data->epoch=task.epoch;

			/* Retrieve segmentation mask */
			mp_mask_data_t mask = {0};
			if (mp_get_segmentation_mask(s->mp_runner, &mask)) {
				update_data->mask_buffer = bmalloc(mask.width * mask.height);
				memcpy(update_data->mask_buffer, mask.buffer, mask.width * mask.height);
				update_data->mask_width = mask.width;
				update_data->mask_height = mask.height;
				bfree(mask.buffer);
			}

			/* Retrieve landmarks */
			mp_landmark_list_t landmarks = {0};
			if (mp_get_landmarks(s->mp_runner, &landmarks)) {
				update_data->landmark_data = bzalloc(landmarks.count * 4 * sizeof(float));
				for (int i = 0; i < landmarks.count; i++) {
					update_data->landmark_data[i * 4 + 0] = landmarks.landmarks[i].x;
					update_data->landmark_data[i * 4 + 1] = landmarks.landmarks[i].y;
					update_data->landmark_data[i * 4 + 2] = landmarks.landmarks[i].z;
					update_data->landmark_data[i * 4 + 3] = landmarks.landmarks[i].visibility;
				}
				update_data->landmark_count = landmarks.count;
				bfree(landmarks.landmarks);

				if (analyze_reference) {
					update_data->ref_landmark_data = bzalloc(landmarks.count * 4 * sizeof(float));
					if (update_data->ref_landmark_data) {
						memcpy(update_data->ref_landmark_data, update_data->landmark_data,
						       landmarks.count * 4 * sizeof(float));
						update_data->ref_landmark_count = landmarks.count;
						blog(LOG_INFO, "[hypershader] Reference landmarks captured: %d",
						     landmarks.count);
					}
				}
			}

            if (mp_epoch_current(s,task.epoch) && obs_source_get_ref(s->context)) {
                obs_queue_task(OBS_TASK_GRAPHICS, mp_update_textures_task, update_data, false);
                /* The upload task releases the reservation; never queue a
                 * second result until the first has been consumed. */
                continue;
            }
            bfree(update_data->mask_buffer);
            bfree(update_data->landmark_data);
            bfree(update_data->ref_landmark_data);
            bfree(update_data);
        }

release_frame:
        atomic_store_explicit(&s->mp_frame_buffer_in_use,false,memory_order_release);

		/* Task struct is embedded - no bfree needed */
	}

	return NULL;
}

static void free_task_data(void *param)
{
	(void)param;
	/* Task struct is embedded in hypershader_data - no bfree needed */
}

/* Update a single parameter with dynamics animation
 * Based on control theory second-order systems:
 *   x'' + 2*zeta*omega*x' + omega^2*x = omega^2*target
 * where:
 *   omega = 2*PI*freq (angular frequency)
 *   zeta = damping ratio (<1 underdamped/bouncy, 1 critical, >1 overdamped)
 */
static void dynamics_update(struct shader_param_meta *m, float dt)
{
	if (m->dynamics_type == DYNAMICS_NONE || dt <= 0.0f)
		return;

	float target = m->dynamics_target;
	float pos = m->dynamics_pos;
	float vel = m->dynamics_vel;

	switch (m->dynamics_type) {
	case DYNAMICS_SPRING: {
		/* Second-order spring-damper system */
		float omega = 2.0f * 3.14159265f * m->dynamics_freq;
		float zeta = m->dynamics_damp;

		/* Integrate at bounded substeps to keep high frequencies stable
		 * at low FPS and after frame stalls. */
		dt = fminf(dt, 0.1f);
		unsigned steps = (unsigned)ceilf(dt * fmaxf(240.0f, omega * 12.0f * fmaxf(1.0f,zeta)));
		if (steps > 2400) steps = 2400;
		float h = dt / fmaxf(1.0f, (float)steps);
		float omega_sq = omega * omega;
		float max_vel = omega * 10.0f;
		for (unsigned step = 0; step < steps; ++step) {
			float accel = omega_sq * (target - pos) - 2.0f * zeta * omega * vel;
			vel = fminf(max_vel, fmaxf(-max_vel, vel + accel * h));
			pos += vel * h;
		}
		break;
	}

	case DYNAMICS_SMOOTH_DAMP: {
		/* Unity-style SmoothDamp approximation */
		float smooth_time = 1.0f / (m->dynamics_freq * 2.0f);
		if (smooth_time < 0.0001f)
			smooth_time = 0.0001f;

		float omega = 2.0f / smooth_time;
		float x = omega * dt;
		float exp_factor = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
		float delta = pos - target;
		float temp = (vel + omega * delta) * dt;
		vel = (vel - omega * temp) * exp_factor;
		pos = target + (delta + temp) * exp_factor;
		break;
	}

	case DYNAMICS_EXPONENTIAL: {
		/* Simple exponential ease */
		float rate = m->dynamics_freq * 2.0f * 3.14159265f;
		float alpha = 1.0f - expf(-rate * dt);
		pos = pos + (target - pos) * alpha;
		vel = (target - pos) / (dt > 0.0f ? dt : 0.016f);
		break;
	}
	}

	m->dynamics_pos = pos;
	m->dynamics_vel = vel;
}

/* Regular Draw techniques clear uniforms between frames just like graph
 * BufferA/BufferB techniques. They must not rely on one-time load defaults. */
static void graph_bind_controls(struct hypershader_data *s);
static void graph_snapshot_controls(struct hypershader_data *s);

/* Opt-in native response envelope. Only an effect declaring audio_reaction_state
 * pays for this work; state belongs to the instance, not to a shared shader. */
static void bind_reaction_state(struct hypershader_data *s)
{
    if (!s->ep_audio_reaction_state) return;
    struct vec4 value;
    vec4_set(&value,s->reaction_state.gate[0],s->reaction_state.gate[1],
             s->reaction_state.gate[2],s->reaction_state.position);
    gs_effect_set_vec4(s->ep_audio_reaction_state,&value);
}

static float reaction_setting(obs_data_t *settings,const char *key,float fallback)
{
    return obs_data_has_user_value(settings,key) ? (float)obs_data_get_double(settings,key) : fallback;
}

static void update_reaction_state(struct hypershader_data *s)
{
    if (!s->ep_audio_reaction_state) return;
    uint64_t now=obs_get_video_frame_time();
    if(now==s->reaction_frame_ns && now!=0) {
        bind_reaction_state(s);
        return;
    }
    const int driver=s->reaction_driver;
    const uint64_t input_key=s->reaction_config_key;
    if(s->reaction_input_key!=input_key || !s->reaction_frame_ns ||
       now<s->reaction_frame_ns || now-s->reaction_frame_ns>500000000ULL) {
        memset(&s->reaction_state,0,sizeof(s->reaction_state));
        s->reaction_input_key=input_key;
    }
    float dt=s->reaction_frame_ns && now>s->reaction_frame_ns
        ? (float)((double)(now-s->reaction_frame_ns)/1e9) : s->last_tick_delta;
    s->reaction_frame_ns=now;
    float signal=s->render_audio_level;
    switch(driver) {
    case 1:signal=s->render_audio_peak;break;
    case 2:signal=fmaxf(s->render_audio_music[HS_MUSIC_KICK],s->render_audio_envelope*0.35f);break;
    case 3:signal=(s->render_audio_bands[0]+s->render_audio_bands[1])*0.5f;break;
    case 4:signal=s->render_audio_bands[4];break;
    case 5:signal=s->render_audio_bands[7];break;
    default:break;
    }
    // A source can stop sending PCM without ever sending a final silent
    // frame. A repeatedly analyzed FFT window is not new audio.
    uint64_t capture=atomic_load_explicit(&s->last_audio_capture_ns,memory_order_acquire);
    uint64_t wall_now=os_gettime_ns();
    if(!capture || wall_now<capture || wall_now-capture>250000000ULL)
        signal=0.0f;
    if(s->reaction_enabled)
        hs_reaction_step(&s->reaction_state,&s->reaction_config,
                         signal*s->reaction_sensitivity,dt);
    else
        memset(&s->reaction_state,0,sizeof(s->reaction_state));
    bind_reaction_state(s);
}

/* GIF inputs use OBS's decoder/texture cache, never reopen the file on
 * a reaction edge. The sprite-specific policy is explicitly opted into by
 * the animation_playback uniform; other shaders play GIFs continuously. */
static void update_animated_channels(struct hypershader_data *s)
{
    bool any = false;
    for (int i=0; i<CHANNEL_COUNT; ++i)
        any |= s->channel_types[i] == 1 && s->ep_input[i] &&
               hs_channel_image_animated(&s->channel_images[i]);
    if (!any) return;

    enum hs_image_playback mode = HS_IMAGE_CONTINUOUS;
    bool reactive = s->ep_audio_reaction_state && s->effect &&
        s->ep_animation_playback;
    if (reactive && s->animation_policy >= HS_IMAGE_CONTINUOUS &&
        s->animation_policy <= HS_IMAGE_RESUME)
        mode = (enum hs_image_playback)s->animation_policy;
    uint64_t frame_ns = obs_get_video_frame_time();
    for (int i=0; i<CHANNEL_COUNT; ++i) {
        if (s->channel_types[i] != 1 || !s->ep_input[i]) continue;
        int state = i - (s->kind == HYPER_KIND_SOURCE ? 1 : 0);
        bool expression = reactive && state >= 0 && state < 3;
        bool triggered = expression && s->reaction_state.target[state] > 0.01f;
        bool visible = expression && s->reaction_state.gate[state] > 0.01f;
        hs_channel_image_play(&s->channel_images[i], frame_ns,
            expression ? mode : HS_IMAGE_CONTINUOUS, triggered, visible);
    }
}

static void bind_sprite_atlases(struct hypershader_data *s)
{
    for (int i=0;i<CHANNEL_COUNT;++i)
        if(s->ep_input_atlas[i])
            gs_effect_set_vec4(s->ep_input_atlas[i],&s->atlas_value[i]);
}

static int atlas_setting(obs_data_t *settings,const char *prefix,const char *suffix,int fallback)
{
    char key[64];
    snprintf(key,sizeof(key),"param_%s_atlas_%s",prefix,suffix);
    if(!obs_data_has_user_value(settings,key)) return fallback;
    int64_t n=obs_data_get_int(settings,key);
    return (int)(n<-4096?-4096:n>4096?4096:n);
}

static void update_sprite_atlases(struct hypershader_data *s)
{
    bool any=false;
    for(int i=0;i<CHANNEL_COUNT;++i) any|=s->ep_input_atlas[i]!=NULL;
    if(!any) return;
    int policy=s->animation_policy;
    uint64_t now=obs_get_video_frame_time();
    for(int i=0;i<CHANNEL_COUNT;++i) {
        if(!s->ep_input_atlas[i]) continue;
        int state=i+(s->kind==HYPER_KIND_SOURCE?0:1);
        if(state>=4) continue;
        struct hs_atlas_config c=s->atlas_config[state];
        bool trigger=state>0&&s->reaction_state.target[state-1]>0.01f;
        bool visible=state==0||s->reaction_state.gate[state-1]>0.01f;
        unsigned frame=hs_atlas_step(&s->atlas_clock[i],c,now,state==0?0:policy,
            trigger,visible,(uint32_t)((uintptr_t)s>>4)^(uint32_t)now^(uint32_t)(i+1));
        vec4_set(&s->atlas_value[i],(float)c.columns,(float)c.rows,(float)frame,0.0f);
    }
    bind_sprite_atlases(s);
}

/* Helper: Update all standard uniforms and parameters shared by Filter and Source */
static void update_shader_uniforms(struct hypershader_data *s, uint32_t width, uint32_t height)
{
    if (s->effect && width && height) workload_touch_draw(s);
	capture_live_channels(s);
	float dt = s->last_tick_delta;
	if (dt < 0.0f)
		dt = 0.0f;

	/* Snapshot audio values safely */
	float level_snap = 0.0f;
	float peak_snap = 0.0f;
	float bands_snap[8] = {0};
	float beat_snap = 0.0f;
	float envelope_snap = 0.0f;
	uint8_t tex_data_snap[AUDIO_TEX_WIDTH * AUDIO_TEX_HEIGHT];

    float music_snap[HS_MUSIC_FIELDS]={0};
	hyper_audio_snapshot(s, &level_snap, &peak_snap, bands_snap, &beat_snap, &envelope_snap,music_snap, tex_data_snap);
    memcpy(s->render_audio_music,music_snap,sizeof(music_snap));
	s->render_audio_level = level_snap;
	s->render_audio_peak = peak_snap;
	s->render_audio_beat = beat_snap;
	s->render_audio_envelope = envelope_snap;
	memcpy(s->render_audio_bands, bands_snap, sizeof(bands_snap));
	update_reaction_state(s);
	update_animated_channels(s);
	update_sprite_atlases(s);

	/* Update Audio Texture */
	bool needs_audio_tex = (s->input_mode == INPUT_MODE_AUDIO);
	if (!needs_audio_tex) {
		for (int i = 0; i < 4; i++) {
			if (s->channel_types[i] == 3) {
				needs_audio_tex = true;
				break;
			}
		}
	}

	if (needs_audio_tex && s->audio_system_healthy) {
		if (!s->audio_texture) {
			s->audio_texture =
				gs_texture_create(AUDIO_TEX_WIDTH, AUDIO_TEX_HEIGHT, GS_R8, 1, NULL, GS_DYNAMIC);
			if (!s->audio_texture) {
				blog(LOG_ERROR, "[hypershader] Failed to create audio texture (out of VRAM?)");
				s->audio_system_healthy = false;
				/* Graceful degradation - shader continues without audio reactivity */
			} else {
				s->audio_tex_dirty = true;
				/* The channel mapping can be configured before the GPU texture
				 * exists. Refresh it as soon as the texture is allocated. */
				for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
					if (s->channel_types[channel] == 3)
						s->channel_textures[channel] = s->audio_texture;
				}
				memset(s->audio_texture_prev, 0, sizeof(s->audio_texture_prev));
				blog(LOG_INFO, "[hypershader] Audio texture created (%dx%d)", AUDIO_TEX_WIDTH,
				     AUDIO_TEX_HEIGHT);
			}
		}

		if (s->audio_texture) {
			uint64_t now_ns = os_gettime_ns();

			/* Weighted difference: emphasize high-frequency bands for better 
               responsiveness to hi-hats, cymbals, and percussive transients */
			float weighted_diff = 0.0f;
			static const float band_weights[8] = {0.5f, 0.7f, 1.0f, 1.2f, 1.5f, 2.0f, 2.5f, 3.0f};

			for (int y = 0; y < AUDIO_TEX_HEIGHT; y++) {
				float band_weight = (y < 8) ? band_weights[y] : 1.0f;
				for (int x = 0; x < AUDIO_TEX_WIDTH; x++) {
					int idx = y * AUDIO_TEX_WIDTH + x;
					float diff =
						fabsf((float)tex_data_snap[idx] - (float)s->audio_texture_prev[idx]);
					weighted_diff += diff * band_weight;
				}
			}
			weighted_diff /= (AUDIO_TEX_WIDTH * AUDIO_TEX_HEIGHT * 255.0f);

			/* Adaptive threshold: lower threshold during quiet passages */
			float adaptive_threshold = s->audio_change_threshold * (1.0f + level_snap * 0.5f);

			/* Update if changed significantly OR throttle time passed */
			if (s->audio_tex_dirty || weighted_diff > adaptive_threshold ||
			    (now_ns - s->last_audio_tex_update_ns) > 16666666ULL) {

				/* Upload CPU audio data through OBS's backend-neutral texture API.
				 * gs_stage_texture is GPU -> CPU readback, not an upload path. */
				gs_texture_set_image(s->audio_texture, tex_data_snap, AUDIO_TEX_WIDTH, false);
				memcpy(s->audio_texture_prev, tex_data_snap, sizeof(s->audio_texture_prev));
				s->last_audio_tex_update_ns = now_ns;
				s->audio_tex_dirty = false;
			}
		}
	}

	/* Set standard uniforms */
	if (s->ep_time)
		gs_effect_set_float(s->ep_time, s->time_accumulator);
	if (s->ep_scale)
		gs_effect_set_float(s->ep_scale, (float)s->render_scale_percent / 100.0f);
	if (s->ep_delta_time)
		gs_effect_set_float(s->ep_delta_time, dt * s->time_scale);
	if (s->ep_frame_rate)
		gs_effect_set_float(s->ep_frame_rate, dt > 0.0f ? (1.0f / dt) : 0.0f);
	if (s->ep_sample_rate)
		gs_effect_set_float(s->ep_sample_rate, (float)s->audio_sample_rate);

	/* CRITICAL FIX: Set hs_input_time for per-channel time sync */
	for (int i = 0; i < 4; i++) {
		if (s->ep_input_time[i]) {
			gs_effect_set_float(s->ep_input_time[i], s->time_accumulator);
		}
	}

	if (s->ep_frame_index) {
		struct gs_effect_param_info pinfo;
		gs_effect_get_param_info(s->ep_frame_index, &pinfo);
		if (pinfo.type == GS_SHADER_PARAM_INT)
			gs_effect_set_int(s->ep_frame_index, (int)s->shader_frame_count);
		else
			gs_effect_set_float(s->ep_frame_index, s->shader_frame_count);
	}

	if (s->ep_date) {
		time_t now_t = time(NULL);
		struct tm *tm = localtime(&now_t);
		if (tm) {
			float date[4] = {(float)(tm->tm_year + 1900), (float)(tm->tm_mon), (float)(tm->tm_mday),
					 (float)(tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec) + dt};
			struct vec4 v_date;
			vec4_from_float4(&v_date, date);
			gs_effect_set_vec4(s->ep_date, &v_date);
		}
	}

	/* Update mouse position for global cursor mode */
	if (s->input_mode == INPUT_MODE_GLOBAL_CURSOR) {
		float mx, my;
		if (get_global_cursor_pos(s, &mx, &my)) {
			s->mouse_pos.x = mx;
			s->mouse_pos.y = my;
		}
	}

	if (s->ep_pointer) {
		/* Mouse needs to be set (vec4: x, y, click_x, click_y) */
		struct vec4 v_mouse;
		const float scale = (float)s->render_scale_percent / 100.0f;
        vec4_set(&v_mouse, s->mouse_pos.x * scale, s->mouse_pos.y * scale,
            s->mouse_click.x * scale, s->mouse_click.y * scale);
		gs_effect_set_vec4(s->ep_pointer, &v_mouse);
	}

	if (s->ep_resolution) {
		struct vec3 res;
		res.x = (float)width;
		res.y = (float)height;
		res.z = (height > 0) ? (res.x / res.y) : 1.0f; // aspect ratio (width/height)
		gs_effect_set_vec3(s->ep_resolution, &res);
	}

	if (s->ep_screen_size) {
		struct vec2 res;
		res.x = (float)width;
		res.y = (float)height;
		gs_effect_set_vec2(s->ep_screen_size, &res);
	}

	/* Channel Resolutions */
	for (int i = 0; i < 4; i++) {
		if (s->ep_input_resolution[i]) {
			/* CRITICAL FIX: Use actual texture resolution for each channel, not output resolution */
			gs_texture_t *tex = s->channel_textures[i];
			if (tex) {
				float w = (float)gs_texture_get_width(tex);
				float h = (float)gs_texture_get_height(tex);
				struct vec4 v_dims;
				vec4_set(&v_dims, w, h, (h > 0) ? w / h : 1.0f, 0.0f);
				gs_effect_set_vec4(s->ep_input_resolution[i], &v_dims);
			} else {
				/* No texture bound - use zero resolution */
				struct vec4 v_dims;
				vec4_set(&v_dims, 0.0f, 0.0f, 1.0f, 0.0f);
				gs_effect_set_vec4(s->ep_input_resolution[i], &v_dims);
			}
		}
	}

	/* Audio Bands (Float Arrays not supported in OBS GLSL transpiler, check individual params) */
	/* Handled by mapping in shadertoy_compat.glsl to audio_bandX */
	/* Or we set them manually if we found them */

	/* Log check for audio bands */
	// blog(LOG_DEBUG, "Setting audio bands");

	/* Use cached parameter handles instead of string lookups for better performance */
	for (int i = 0; i < 8; i++) {
		if (s->ep_audio_band[i]) {
			gs_effect_set_float(s->ep_audio_band[i], bands_snap[i]);
		}
	}

	if (s->ep_audio_level)
		gs_effect_set_float(s->ep_audio_level, level_snap);
	if (s->ep_audio_peak)
		gs_effect_set_float(s->ep_audio_peak, peak_snap);
	if (s->ep_audio_beat)
		gs_effect_set_float(s->ep_audio_beat, beat_snap);
	if (s->ep_audio_envelope)
		gs_effect_set_float(s->ep_audio_envelope, envelope_snap);
    for(int i=0;i<HS_MUSIC_FIELDS;++i)
        if(s->ep_audio_music[i])
            gs_effect_set_float(s->ep_audio_music[i],music_snap[i]);

	/* Scene Data */
	if (s->use_scene_data) {
		if (s->ep_view_mat)
			gs_effect_set_matrix4(s->ep_view_mat, &s->view_mat);
		if (s->ep_proj_mat)
			gs_effect_set_matrix4(s->ep_proj_mat, &s->proj_mat);
		if (s->ep_camera_pos)
			gs_effect_set_vec3(s->ep_camera_pos, &s->camera_pos);
		if (s->ep_camera_dir)
			gs_effect_set_vec3(s->ep_camera_dir, &s->camera_dir);
	}

	/* Custom Parameters */
	/* Custom Parameters (Mutex removed) */

	/* Sight snapshots are already inferred by its producer: no second model.
	 * Explicit presence flags guard sampling when a current snapshot is absent. */
	bool sight_selected;
	pthread_mutex_lock(&s->mutex);
	sight_selected = s->sight_use_tracking;
	pthread_mutex_unlock(&s->mutex);
    const bool native_current=mp_result_current(s);
	const bool sight_present = sight_selected && s->sight_points_tex &&
		s->sight_point_count > 0;
	const bool sight_face = sight_present && s->sight_kind == SIGHT_SNAPSHOT_FACE &&
		s->sight_point_count >= 468;
	if (s->ep_sight_present)
		gs_effect_set_float(s->ep_sight_present, sight_present ? 1.f : 0.f);
	if (s->ep_sight_kind)
		gs_effect_set_int(s->ep_sight_kind, sight_present ? (int)s->sight_kind : 0);
	if (s->ep_sight_point_count)
		gs_effect_set_int(s->ep_sight_point_count, sight_present ? (int)s->sight_point_count : 0);
	if (s->ep_sight_hand_count)
		gs_effect_set_int(s->ep_sight_hand_count, sight_present ? (int)s->sight_hand_count : 0);
	if (s->ep_sight_pinch0)
		gs_effect_set_float(s->ep_sight_pinch0, sight_present ? s->sight_pinch[0] : 0.f);
	if (s->ep_sight_pinch1)
		gs_effect_set_float(s->ep_sight_pinch1, sight_present ? s->sight_pinch[1] : 0.f);
	if (s->ep_sight_gesture0)
		gs_effect_set_int(s->ep_sight_gesture0, sight_present ? s->sight_gesture[0] : 0);
	if (s->ep_sight_gesture1)
		gs_effect_set_int(s->ep_sight_gesture1, sight_present ? s->sight_gesture[1] : 0);
	if (s->ep_sight_points)
		gs_effect_set_texture(s->ep_sight_points,
				      sight_present ? s->sight_points_tex : s->dummy_texture);
	if (s->ep_sight_hands)
		gs_effect_set_texture(s->ep_sight_hands,
				      sight_present && s->sight_hands_tex ? s->sight_hands_tex : s->dummy_texture);
    if(s->ep_sight_touch[0]||s->ep_sight_ripple[0]){
        const bool valid=sight_present && s->sight_kind==SIGHT_SNAPSHOT_HAND &&
            s->sight_touch.width==width && s->sight_touch.height==height;
        double now=(double)os_gettime_ns()/1e9;
        for(unsigned i=0;i<2;i++)if(s->ep_sight_touch[i]){
            struct vec4 v;
            if(valid)vec4_set(&v,s->sight_touch.fingers[i][0],s->sight_touch.fingers[i][1],
                s->sight_touch.fingers[i][2],s->sight_touch.fingers[i][3]);
            else vec4_zero(&v);
            gs_effect_set_vec4(s->ep_sight_touch[i],&v);
        }
        for(unsigned i=0;i<HS_TOUCH_CAPACITY;i++)if(s->ep_sight_ripple[i]){
            float wave[4]={0};struct vec4 v;
            if(valid)hs_touch_wave(&s->sight_touch,i,now,wave);
            vec4_set(&v,wave[0],wave[1],wave[2],wave[3]);
            gs_effect_set_vec4(s->ep_sight_ripple[i],&v);
        }
    }
	if (s->ep_sight_blendshapes)
		gs_effect_set_texture(s->ep_sight_blendshapes,
				      sight_face && s->sight_blendshape_tex ? s->sight_blendshape_tex : s->dummy_texture);
	if (s->ep_sight_head_rows)
		gs_effect_set_texture(s->ep_sight_head_rows,
				      sight_face && s->sight_transform_tex ? s->sight_transform_tex : s->dummy_texture);

	if (s->ep_face_present)
		gs_effect_set_float(s->ep_face_present,
				sight_selected ? (sight_face ? 1.f : 0.f) :
				(native_current && s->mp_landmarks_tex ? 1.f : 0.f));
	if (s->ep_sight_matte_present)
		gs_effect_set_float(s->ep_sight_matte_present,sight_selected && s->sight_matte_present ? 1.f : 0.f);
    const bool depth=sight_selected&&s->sight_depth_present&&s->sight_depth_tex;
    if(s->ep_sight_depth)gs_effect_set_texture(s->ep_sight_depth,depth?s->sight_depth_tex:s->dummy_texture);
    if(s->ep_sight_depth_present)gs_effect_set_float(s->ep_sight_depth_present,depth?1.f:0.f);
    const bool flow=sight_selected&&s->sight_flow_present;
    if(s->ep_sight_flow)gs_effect_set_texture(s->ep_sight_flow,flow?s->sight_flow_texture:s->dummy_texture);
    if(s->ep_sight_flow_present)gs_effect_set_float(s->ep_sight_flow_present,flow?1.f:0.f);
    if(s->ep_sight_flow_interval)gs_effect_set_float(s->ep_sight_flow_interval,flow?s->sight_flow_interval:0.f);
    if(s->ep_sight_flow_coverage)gs_effect_set_float(s->ep_sight_flow_coverage,flow?s->sight_flow_coverage:0.f);
    if(s->ep_sight_flow_step)gs_effect_set_float(s->ep_sight_flow_step,0.f);
    sight_surface_bind(s,sight_selected);
    sight_semantic_bind(s,sight_selected);
    const bool text = sight_selected && s->sight_text_present;
    if(s->ep_sight_text_present)gs_effect_set_float(s->ep_sight_text_present,text?1.f:0.f);
    if(s->ep_sight_text_count)gs_effect_set_int(s->ep_sight_text_count,text?(int)s->sight_text_count:0);
    if(s->ep_sight_text_mask)gs_effect_set_texture(s->ep_sight_text_mask,text?s->sight_text_mask:s->dummy_texture);
    if(s->ep_sight_text_bounds)gs_effect_set_texture(s->ep_sight_text_bounds,text?s->sight_text_bounds:s->dummy_texture);
    const bool motion = sight_selected && s->sight_motion_present;
    if (s->ep_sight_motion_present)
        gs_effect_set_float(s->ep_sight_motion_present,motion?1.f:0.f);
    if (s->ep_sight_motion_energy)
        gs_effect_set_float(s->ep_sight_motion_energy,motion?s->sight_motion_energy:0.f);
    const bool subject = sight_selected && s->sight_matte_present && s->sight_subject_present;
    if (s->ep_sight_subject_present)
        gs_effect_set_float(s->ep_sight_subject_present, subject ? 1.f : 0.f);
    if (s->ep_sight_subject_bounds) {
        struct vec4 bounds;
        vec4_set(&bounds, subject?s->sight_subject_bounds[0]:0.f, subject?s->sight_subject_bounds[1]:0.f,
                 subject?s->sight_subject_bounds[2]:0.f, subject?s->sight_subject_bounds[3]:0.f);
        gs_effect_set_vec4(s->ep_sight_subject_bounds,&bounds);
    }
    if (s->ep_sight_subject_center) {
        struct vec2 center;
        vec2_set(&center,subject?s->sight_subject_center[0]:0.f,subject?s->sight_subject_center[1]:0.f);
        gs_effect_set_vec2(s->ep_sight_subject_center,&center);
    }
    if (s->ep_sight_subject_coverage)
        gs_effect_set_float(s->ep_sight_subject_coverage,subject?s->sight_subject_coverage:0.f);
	if (sight_selected) {
		if (s->ep_landmarks)
			gs_effect_set_texture(s->ep_landmarks,
					     sight_present ? s->sight_points_tex : s->dummy_texture);
		if (s->ep_face_surface)
			gs_effect_set_texture(s->ep_face_surface,
					     sight_face && s->sight_face_surface ?
					     hs_face_surface_texture(s->sight_face_surface) : s->dummy_texture);
		if (s->ep_face_triangles)
			gs_effect_set_texture(s->ep_face_triangles,
					     sight_face && s->sight_face_surface ?
					     hs_face_surface_triangles(s->sight_face_surface) : s->dummy_texture);
		if (s->ep_segmentation)
			gs_effect_set_texture(s->ep_segmentation, s->sight_matte_present ? s->sight_matte_tex : s->dummy_texture);
    } else {
        if (s->ep_segmentation)
            gs_effect_set_texture(s->ep_segmentation,native_current && s->mp_mask_tex ? s->mp_mask_tex : s->dummy_texture);
        if (s->ep_landmarks)
            gs_effect_set_texture(s->ep_landmarks,native_current ? s->mp_landmarks_tex : NULL);
        if (s->ep_reference_landmarks)
            gs_effect_set_texture(s->ep_reference_landmarks,s->mp_enabled ? s->mp_ref_landmarks_tex : NULL);
        if (s->ep_face_surface)
            gs_effect_set_texture(s->ep_face_surface,native_current && s->mp_landmarks_tex && s->mp_face_surface ?
                hs_face_surface_texture(s->mp_face_surface) : s->dummy_texture);
        if (s->ep_face_triangles)
            gs_effect_set_texture(s->ep_face_triangles,native_current && s->mp_landmarks_tex && s->mp_face_surface ?
                hs_face_surface_triangles(s->mp_face_surface) : s->dummy_texture);
    }

    /* OBS's effect compiler flattens float4 uniform arrays in OpenGL.
     * Use a typed RGBA32F texture, exactly matching scene_get_entities'
     * real [x,y,radius,type] records. Every pixel is one entire entity. */
    if (s->ep_entityCount && (s->ep_entityTexture || s->ep_entities)) {
        if (s->scene && s->entity_data && pthread_mutex_trylock(&s->mutex) == 0) {
            s->entity_count = scene_get_entities(s->scene, s->entity_data, s->entity_count_max);
            pthread_mutex_unlock(&s->mutex);
        }
        const int count = s->ep_entityTexture && !s->entity_texture ? 0 : s->entity_count;
        gs_effect_set_int(s->ep_entityCount, count);
        if (s->ep_entityTexture) {
            if (s->entity_texture) {
                gs_texture_set_image(s->entity_texture,
                    (const uint8_t *)s->entity_data,
                    (uint32_t)(s->entity_count_max * 4 * sizeof(float)), false);
                gs_effect_set_texture(s->ep_entityTexture, s->entity_texture);
            } else {
                gs_effect_set_texture(s->ep_entityTexture, s->dummy_texture);
            }
        } else if (s->ep_entities && count > 0) {
            gs_effect_set_val(s->ep_entities, s->entity_data,
                (size_t)count * 4 * sizeof(float));
        }
    }

	/* Update hs_input Resolution and bind textures */
	struct vec3 channel_res[4];
	memset(channel_res, 0, sizeof(channel_res));
	for (int i = 0; i < 4; i++) {
		gs_texture_t *tex = s->channel_textures[i];
		if (tex) {
			float w = (float)gs_texture_get_width(tex);
			float h = (float)gs_texture_get_height(tex);
			channel_res[i].x = w;
			channel_res[i].y = h;
			channel_res[i].z = (h > 0) ? w / h : 0.0f;
		}
		if (s->ep_input[i])
			gs_effect_set_texture(s->ep_input[i], tex ? tex :
				(s->channel_named[i] || s->channel_types[i] == 2 ? s->dummy_texture : NULL));
	}

	/* Snapshot only actual data usable in this render. */
	unsigned flags = (sight_present ? 1u : 0u) | (depth ? 2u : 0u) |
		(flow ? 4u : 0u) | (text ? 8u : 0u) | (motion ? 16u : 0u) |
		(sight_selected && s->sight_matte_present ? 32u : 0u) | (subject ? 64u : 0u) |
		(sight_face || (!sight_selected && native_current && s->mp_landmarks_tex) ? 128u : 0u);
	for (unsigned i = 0; i < 4; ++i)
		if (s->channel_textures[i])
			flags |= 1u << (8 + i);
	atomic_store_explicit(&s->live_signal_flags, flags, memory_order_relaxed);
	atomic_store_explicit(&s->live_audio_permille,
		(unsigned)(fmaxf(0.f, fminf(level_snap, 1.f)) * 1000.f), memory_order_relaxed);
	atomic_store_explicit(&s->live_render_ns, os_gettime_ns(), memory_order_release);

	/* Apply Audio Metadata Bindings with Dynamics */
	pthread_mutex_lock(&s->mutex);
	if (s->effect && s->metadata_rc && s->metadata_rc->meta.num > 0 && s->meta_audio_values) {
		for (size_t i = 0; i < s->metadata_rc->meta.num && i < s->meta_audio_count; i++) {
			struct shader_param_meta *m = &s->metadata_rc->meta.array[i];

			/* The host-owned audio uniforms have already received their real
			 * worker snapshot. Metadata may modulate only shader-owned floats. */
			if (m->audio_tag && m->render_key[0] && m->render_type == GS_SHADER_PARAM_FLOAT) {
                float target = hs_metadata_signal_value(m->render_audio_signal,
                    level_snap, peak_snap, beat_snap, envelope_snap, bands_snap, music_snap);
                target *= m->audio_gain;

				float final_value;

				/* Use dynamics if enabled, otherwise simple smoothing */
				if (m->dynamics_type != DYNAMICS_NONE) {
					m->dynamics_target = target;
					dynamics_update(m, dt);
					final_value = m->dynamics_pos;
					s->meta_audio_values[i] = final_value;
				} else {
					/* Fallback to original exponential smoothing */
					float alpha = 1.0f - m->audio_smooth;
					if (alpha < 0.01f)
						alpha = 0.01f;
					s->meta_audio_values[i] += (target - s->meta_audio_values[i]) * alpha;
					final_value = s->meta_audio_values[i];
				}

				if (s->effect)
					gs_effect_set_float(m->cached_param, final_value);
			}
		}
	}
    pthread_mutex_unlock(&s->mutex);
    /* Reassert saved/default shader parameters before every native Draw,
     * including regular filters, feedback, sources and transitions. */
    graph_snapshot_controls(s);
    graph_bind_controls(s);
}

static void update_performance_stats(struct hypershader_data *s, double render_time_ms)
{
	if (!s)
		return;

	s->perf_stats.frame_count++;

	/* Exponential moving average for smoother display */
	if (s->perf_stats.frame_count == 1) {
		s->perf_stats.avg_render_time_ms = render_time_ms;
	} else {
		s->perf_stats.avg_render_time_ms = (s->perf_stats.avg_render_time_ms * 0.95) + (render_time_ms * 0.05);
	}

	if (render_time_ms > s->perf_stats.max_render_time_ms) {
		s->perf_stats.max_render_time_ms = render_time_ms;
	}

	/* Reset stats every 5 seconds to keep "Max" relevant */
	uint64_t now = os_gettime_ns();
	if (now - s->perf_stats.last_reset_time > 5000000000ULL) {
		s->perf_stats.max_render_time_ms = 0;
		s->perf_stats.last_reset_time = now;
		/* Don't reset avg or frame_count fully, just max */
	}
}

/* OBS may clear effect parameter values between technique loops. Restore
 * the scene's numeric controls immediately before each graph pass, including
 * after any pass that used the same mutable gs_effect_t.
 * Audio-bound uniforms use the current smoothed value instead of saved sliders. */
/* A live modulation is a bounded render-time override, not a settings
 * mutation. One mapping per instance; stored path binds it to its shader.
 * The same evaluated signal feeds regular Draw and every graph pass. */
static float performance_modulated_value(struct hypershader_data *s,
        const struct shader_param_meta *m, float base)
{
    const int driver = m->render_mod_driver;
    if (!driver)
        return base;
    if (driver == 8) {
        float value=0;
        if (!hs_data_driver_read(s->data_driver,s->mod_uuid,s->mod_age_ms,&value))
            return base;
        return fmaxf(m->min,fminf(m->max,value));
    }
	float signal = 0.f;
	const float depth = s->mod_depth;
	if (!isfinite(depth) || depth <= 0.f)
		return base;
	if (driver == 1) {
		const float hz = s->mod_hz;
		signal = cosf(6.28318530718f * s->time_accumulator *
			(isfinite(hz) ? fmaxf(.01f, fminf(hz, 10.f)) : 1.f));
	} else if (driver <= 5) {
		uint64_t pcm = atomic_load_explicit(&s->last_audio_capture_ns, memory_order_acquire);
		uint64_t now = os_gettime_ns();
		if (!pcm || now < pcm || now - pcm > 250000000ULL)
			return base;
		switch (driver) {
		case 2: signal = s->render_audio_level; break;
		case 3: signal = s->render_audio_bands[0]; break;
		case 4: signal = s->render_audio_music[HS_MUSIC_KICK]; break;
		case 5: signal = s->render_audio_beat; break;
		}
		signal = fmaxf(0.f, fminf(signal, 1.f));
	} else {
		unsigned flags = atomic_load_explicit(&s->live_signal_flags, memory_order_relaxed);
		if (driver == 6 && (flags & 16u))
			signal = fmaxf(0.f, fminf(s->sight_motion_energy, 1.f));
		else if (driver == 7 && (flags & 1u) && s->sight_kind == SIGHT_SNAPSHOT_HAND)
			signal = fmaxf(0.f, fminf(s->sight_pinch[0], 1.f));
		else
			return base;
	}
	if (!isfinite(signal))
		return base;
	return fmaxf(m->min, fminf(m->max,
		base + signal * fminf(depth, 1.f) * (m->max - m->min)));
}

/* Decode persistent settings only on update or effect replacement. The graphics
 * context serializes publication with drawing; only the motion-request bit is
 * consumed by video_tick outside that context. */
static void cache_runtime_settings(struct hypershader_data *s, obs_data_t *settings)
{
    if (!settings) return;
    s->render_scale_percent = s->kind == HYPER_KIND_TRANSITION ? 100 :
        hs_render_scale_percent(obs_data_get_int(settings,"render_scale_percent"));
    const char *shader = obs_data_get_string(settings,"shader_file");
    const char *mod_shader = obs_data_get_string(settings,"perf_mod_shader");
    const char *mod_param = obs_data_get_string(settings,"perf_mod_param");
    const int mod_driver = (int)obs_data_get_int(settings,"perf_mod_source");
    const bool requested = *shader && !strcmp(shader,mod_shader);
    atomic_store_explicit(&s->mod_motion_requested,requested && mod_driver==6,memory_order_relaxed);
    const bool driver_valid = requested && s->performance_compiled_path &&
        !strcmp(shader,s->performance_compiled_path) && mod_driver>=1 && mod_driver<=8;
    s->mod_depth=(float)obs_data_get_double(settings,"perf_mod_depth");
    s->mod_hz=(float)obs_data_get_double(settings,"perf_mod_hz");
    const char *uuid=obs_data_get_string(settings,"perf_mod_data_uuid");
    s->mod_uuid[0]=0;
    if(strlen(uuid)==36) memcpy(s->mod_uuid,uuid,37);
    const long long age=obs_data_get_int(settings,"perf_mod_data_age_ms");
    s->mod_age_ms=age>0?(int)fmin(60000.,(double)age):5000;
    int driver=(int)obs_data_get_int(settings,"param_response_driver");
    const char *source=obs_data_get_string(settings,"audio_source");
    bool mix=obs_data_get_int(settings,"input_mode")==INPUT_MODE_AUDIO;
    bool enabled=obs_data_get_bool(settings,"enable_sound") && source && *source;
    bool temporal=!obs_data_has_user_value(settings,"param_temporal_response") ||
                  obs_data_get_bool(settings,"param_temporal_response");
    uint64_t input_key=1469598103934665603ULL;
    if(source) for(const unsigned char *p=(const unsigned char *)source;*p;p++)
        input_key=(input_key ^ *p)*1099511628211ULL;
    input_key=(input_key ^ (uint64_t)(unsigned)driver)*1099511628211ULL;
    input_key=(input_key ^ (uint64_t)mix)*1099511628211ULL;
    input_key=(input_key ^ (uint64_t)obs_data_get_int(settings,"audio_mix"))*1099511628211ULL;
    input_key=(input_key ^ (uint64_t)enabled)*1099511628211ULL;
    input_key=(input_key ^ (uint64_t)temporal)*1099511628211ULL;
    struct hs_reaction_config cfg={
        .threshold={reaction_setting(settings,"param_threshold",.025f),
                    reaction_setting(settings,"param_strong_threshold",.16f),
                    reaction_setting(settings,"param_accent_threshold",.30f)},
        .width=reaction_setting(settings,"param_transition_width",.045f),
        .hysteresis=reaction_setting(settings,"param_hysteresis",.008f),
        .attack=reaction_setting(settings,"param_attack_ms",35.0f)*.001f,
        .release=reaction_setting(settings,"param_release_ms",120.0f)*.001f,
        .hold=reaction_setting(settings,"param_hold_ms",90.0f)*.001f,
        .frequency=reaction_setting(settings,"param_spring_frequency",3.0f),
        .damping=reaction_setting(settings,"param_spring_damping",.55f)
    };
    if(!temporal) cfg.attack=cfg.release=cfg.hold=cfg.hysteresis=0.0f;
    s->reaction_config=cfg;
    s->reaction_config_key=input_key;
    s->reaction_driver=driver;
    s->reaction_enabled=enabled || mix;
    s->reaction_sensitivity=reaction_setting(settings,"param_sensitivity",1.0f);
    s->animation_policy=obs_data_has_user_value(settings,"param_animation_playback")
        ? (int)obs_data_get_int(settings,"param_animation_playback") : HS_IMAGE_RESTART;
    const char *states[]={"idle","active","strong","accent"};
    for (int state=0;state<4;state++) {
        const char *prefix=states[state];
        char fps_key[64];
        snprintf(fps_key,sizeof(fps_key),"param_%s_atlas_fps",prefix);
        struct hs_atlas_config c={
            .columns=atlas_setting(settings,prefix,"columns",1),
            .rows=atlas_setting(settings,prefix,"rows",1),
            .first=atlas_setting(settings,prefix,"first",0),
            .count=atlas_setting(settings,prefix,"count",0),
            .mode=atlas_setting(settings,prefix,"mode",0),
            .fps=obs_data_has_user_value(settings,fps_key)?obs_data_get_double(settings,fps_key):12.0,
            .rest_min=state==0?reaction_setting(settings,"param_idle_atlas_rest_min",1.0f):0.0,
            .rest_max=state==0?reaction_setting(settings,"param_idle_atlas_rest_max",4.0f):0.0
        };
        if(state>0&&c.mode>HS_ATLAS_RANDOM) c.mode=HS_ATLAS_LOOP;
        c=hs_atlas_sanitize(c);
        s->atlas_config[state]=c;
    }
    if (!s->metadata_rc) return;
	for (size_t i = 0; i < s->metadata_rc->meta.num; i++) {
		struct shader_param_meta *m = &s->metadata_rc->meta.array[i];
		m->render_valid = false;
		m->render_mod_driver = 0;
		if (!settings || !m->render_key[0])
			continue;
		const char *prop = m->render_key;
		if (m->audio_tag && s->meta_audio_values && i < s->meta_audio_count) {
			if (m->render_type == GS_SHADER_PARAM_FLOAT) {
				m->render_value.scalar = 0.f;
				m->render_valid = true;
			}
			continue;
		}
		const bool user = obs_data_has_user_value(settings, prop);
		switch (m->render_type) {
		case GS_SHADER_PARAM_FLOAT: {
			float base = (float)(user || !m->has_default ?
				obs_data_get_double(settings, prop) : m->default_val);
			m->render_base = base;
            m->render_value.scalar = base;
            if (driver_valid && !strcmp(mod_param, prop) &&
                m->has_min && m->has_max && isfinite(m->min) &&
                isfinite(m->max) && m->min < m->max) {
                bool mapped = false;
                if (mod_driver == 8) {
                    for (int j=0;j<AUDIO_MAP_SLOTS;j++) {
                        char key[64];
                        snprintf(key,sizeof(key),"mapping_%d_name",j);
                        if (!strcmp(obs_data_get_string(settings,key),m->name))
                            mapped = true;
                    }
                }
                if (!mapped)
                    m->render_mod_driver = mod_driver;
            }
			m->render_valid = true;
			break;
		}
		case GS_SHADER_PARAM_INT:
			m->render_value.integer = (int)(user || !m->has_default ?
				obs_data_get_int(settings, prop) : (int)m->default_val);
			m->render_valid = true;
			break;
		case GS_SHADER_PARAM_BOOL:
			m->render_value.boolean = user || !m->has_default ?
				obs_data_get_bool(settings, prop) : m->default_val != 0.0f;
			m->render_valid = true;
			break;
		case GS_SHADER_PARAM_VEC3:
		case GS_SHADER_PARAM_VEC4:
			if (user) {
				vec4_from_rgba(&m->render_color, (uint32_t)obs_data_get_int(settings, prop));
				m->render_valid = true;
			} else if (m->render_default_color_valid) {
				m->render_color = m->render_default_color;
				m->render_valid = true;
			}
			break;
		default:
			break;
		}
	}
}

/* Only time-varying inputs are evaluated once per render. All passes share
 * this value, including the unmodified base for missing/stale signals. */
static void graph_snapshot_controls(struct hypershader_data *s)
{
    if (!s->metadata_rc) return;
    for (size_t i=0;i<s->metadata_rc->meta.num;i++) {
        struct shader_param_meta *m=&s->metadata_rc->meta.array[i];
        if (!m->render_valid || m->render_type!=GS_SHADER_PARAM_FLOAT)
            continue;
        if (m->audio_tag && s->meta_audio_values && i<s->meta_audio_count)
            m->render_value.scalar=s->meta_audio_values[i];
        else
            m->render_value.scalar=performance_modulated_value(s,m,m->render_base);
    }
}

static void graph_bind_controls(struct hypershader_data *s)
{
	if (!s->metadata_rc)
		return;
	for (size_t i = 0; i < s->metadata_rc->meta.num; i++) {
		const struct shader_param_meta *m = &s->metadata_rc->meta.array[i];
		if (!m->render_valid)
			continue;
		switch (m->render_type) {
		case GS_SHADER_PARAM_FLOAT:
			gs_effect_set_float(m->cached_param, m->render_value.scalar);
			break;
		case GS_SHADER_PARAM_INT:
			gs_effect_set_int(m->cached_param, m->render_value.integer);
			break;
		case GS_SHADER_PARAM_BOOL:
			gs_effect_set_bool(m->cached_param, m->render_value.boolean);
			break;
		case GS_SHADER_PARAM_VEC3: {
			struct vec3 rgb;
			vec3_set(&rgb, m->render_color.x, m->render_color.y, m->render_color.z);
			gs_effect_set_vec3(m->cached_param, &rgb);
			break;
		}
		case GS_SHADER_PARAM_VEC4:
			gs_effect_set_vec4(m->cached_param, &m->render_color);
			break;
		default:
			break;
		}
	}
}

/* Built-in uniforms have the same lifetime issue as custom scalars:
 * gs_effect_loop clears mutable values after a technique. Rebind each pass
 * before rendering so subsequent stages never see uninitialized uniforms. */
static void graph_bind_builtins(struct hypershader_data *s, uint32_t width, uint32_t height, bool flow, bool flow_step, bool sight_selected)
{
	/* A technique loop may clear mutable bindings. All graph stages share
	 * the same already-updated external resources, without decoding or
	 * advancing animation again. A missing input has a defined black
	 * fallback and zero logical dimensions. */
	for (unsigned i = 0; i < CHANNEL_COUNT; i++) {
		gs_texture_t *tex = s->channel_textures[i];
		if (s->ep_input[i])
			gs_effect_set_texture(s->ep_input[i], tex ? tex : s->dummy_texture);
		if (s->ep_input_resolution[i]) {
			const float w = tex ? (float)gs_texture_get_width(tex) : 0.0f;
			const float h = tex ? (float)gs_texture_get_height(tex) : 0.0f;
			struct vec4 dims;
			vec4_set(&dims, w, h, h > 0.0f ? w / h : 1.0f, 0.0f);
			gs_effect_set_vec4(s->ep_input_resolution[i], &dims);
		}
		if (s->ep_input_time[i])
			gs_effect_set_float(s->ep_input_time[i], s->time_accumulator);
	}

    bind_reaction_state(s);
    bind_sprite_atlases(s);
	for (int i = 0; i < 8; i++) {
		if (s->ep_audio_band[i])
			gs_effect_set_float(s->ep_audio_band[i], s->render_audio_bands[i]);
	}
	if (s->ep_audio_level)
		gs_effect_set_float(s->ep_audio_level, s->render_audio_level);
	if (s->ep_audio_peak)
		gs_effect_set_float(s->ep_audio_peak, s->render_audio_peak);
	if (s->ep_audio_beat)
		gs_effect_set_float(s->ep_audio_beat, s->render_audio_beat);
	if (s->ep_audio_envelope)
		gs_effect_set_float(s->ep_audio_envelope, s->render_audio_envelope);
    for(int i=0;i<HS_MUSIC_FIELDS;++i)
        if(s->ep_audio_music[i])
            gs_effect_set_float(s->ep_audio_music[i],s->render_audio_music[i]);
	if (s->ep_graph_frame)
		gs_effect_set_int(s->ep_graph_frame, (int)s->graph_frame);
    /* Native effect loops clear parameters between techniques. Rebind
     * the same shared-flow decision for all passes in this graph render. */
    if(s->ep_sight_flow)gs_effect_set_texture(s->ep_sight_flow,flow?s->sight_flow_texture:s->dummy_texture);
    if(s->ep_sight_flow_present)gs_effect_set_float(s->ep_sight_flow_present,flow?1.f:0.f);
    if(s->ep_sight_flow_interval)gs_effect_set_float(s->ep_sight_flow_interval,flow?s->sight_flow_interval:0.f);
    if(s->ep_sight_flow_coverage)gs_effect_set_float(s->ep_sight_flow_coverage,flow?s->sight_flow_coverage:0.f);
    if(s->ep_sight_flow_step)gs_effect_set_float(s->ep_sight_flow_step,flow_step?1.f:0.f);
    // Rebind the same source-local matte in every graph pass.  Do not
    // replace the existing native MediaPipe binding when Sight is off.
    const bool depth=sight_selected&&s->sight_depth_present&&s->sight_depth_tex;
    if(s->ep_sight_depth)gs_effect_set_texture(s->ep_sight_depth,depth?s->sight_depth_tex:s->dummy_texture);
    if(s->ep_sight_depth_present)gs_effect_set_float(s->ep_sight_depth_present,depth?1.f:0.f);
    sight_surface_bind(s,sight_selected);
    sight_semantic_bind(s,sight_selected);
    const bool matte=sight_selected&&s->sight_matte_present&&s->sight_matte_tex;
    if(s->ep_sight_matte_present)gs_effect_set_float(s->ep_sight_matte_present,matte?1.f:0.f);
    if(sight_selected&&s->ep_segmentation)
        gs_effect_set_texture(s->ep_segmentation,matte?s->sight_matte_tex:s->dummy_texture);


	if (s->ep_time)
		gs_effect_set_float(s->ep_time, s->time_accumulator);
    if (s->ep_scale)
        gs_effect_set_float(s->ep_scale, (float)s->render_scale_percent / 100.0f);
	if (s->ep_resolution) {
		struct vec3 resolution;
		vec3_set(&resolution, (float)width, (float)height,
			 height ? (float)width / (float)height : 1.0f);
		gs_effect_set_vec3(s->ep_resolution, &resolution);
	}
	if (s->ep_delta_time)
		gs_effect_set_float(s->ep_delta_time, s->last_tick_delta * s->time_scale);
	if (s->ep_frame_index) {
		struct gs_effect_param_info info;
		gs_effect_get_param_info(s->ep_frame_index, &info);
		if (info.type == GS_SHADER_PARAM_INT)
			gs_effect_set_int(s->ep_frame_index, (int)s->shader_frame_count);
		else if (info.type == GS_SHADER_PARAM_FLOAT)
			gs_effect_set_float(s->ep_frame_index, s->shader_frame_count);
	}
}

/* Native two-buffer shader graph. Every render draws BufferA then BufferB
 * into separate ping-pong textures, followed by Draw to the active target.
 * Texture outputs are bound after the source capture and before each pass.
 * Never read from the texture currently being written. */
static bool output_composition_enabled(const struct hypershader_data *s)
{
    return s && (s->compose_base_opacity < 0.99999f ||
                 s->compose_effect_opacity < 0.99999f ||
                 s->compose_blend_mode != 0);
}

/* OBS graphics lock is held by all callers. Keep compositing AFTER the shader's
 * feedback update, so changing opacity/blend never changes historical state. */
static bool output_compositor_prepare(struct hypershader_data *s, enum gs_color_format format)
{
    if (!s->output_compositor) {
        char *filename = obs_module_file("stdlib/hyper_output_compositor.effect");
        if (!filename) {
            blog(LOG_ERROR, "[hypershader] Compositor shader resource missing");
            return false;
        }
        char *error = NULL;
        s->output_compositor = gs_effect_create_from_file(filename, &error);
        if (!s->output_compositor)
            blog(LOG_ERROR, "[hypershader] Compositor compile failed: %s",
                 error ? error : "unknown error");
        if (error)
            bfree(error);
        bfree(filename);
        if (!s->output_compositor)
            return false;
        s->compose_base_image = gs_effect_get_param_by_name(s->output_compositor, "baseImage");
        s->compose_fx_image = gs_effect_get_param_by_name(s->output_compositor, "effectImage");
        s->compose_base_alpha = gs_effect_get_param_by_name(s->output_compositor, "baseOpacity");
        s->compose_fx_alpha = gs_effect_get_param_by_name(s->output_compositor, "effectOpacity");
        s->compose_mode = gs_effect_get_param_by_name(s->output_compositor, "blendMode");
    }
    if (s->output_composite_fx && s->output_composite_format != format) {
        gs_texrender_destroy(s->output_composite_fx);
        s->output_composite_fx = NULL;
    }
    if (s->output_composite_base && s->output_composite_format != format) {
        gs_texrender_destroy(s->output_composite_base);
        s->output_composite_base = NULL;
    }
    s->output_composite_format = format;
    if (!s->output_composite_fx)
        s->output_composite_fx = gs_texrender_create(format, GS_ZS_NONE);
    if (!s->output_composite_base)
        s->output_composite_base = gs_texrender_create(format, GS_ZS_NONE);
    if (!s->output_composite_fx || !s->output_composite_base)
        return false;
    if (!s->output_transparent) {
        const uint32_t pixel = 0;
        const uint8_t *bytes = (const uint8_t *)&pixel;
        s->output_transparent = gs_texture_create(1, 1, GS_RGBA, 1, &bytes, 0);
    }
    return s->output_transparent != NULL;
}

/* Offscreen render keeps the produced effect separate from source opacity and
 * blending, and supports original current-frame, feedback and transition modes. */
static gs_texture_t *output_render_effect(struct hypershader_data *s,
                                          gs_texture_t *image, uint32_t width,
                                          uint32_t height, enum gs_color_format format)
{
    if (!s->effect || !image || !output_compositor_prepare(s, format))
        return NULL;
    gs_texrender_reset(s->output_composite_fx);
    if (!gs_texrender_begin(s->output_composite_fx, width, height))
        return NULL;
    gs_projection_push();
    gs_matrix_push();
    gs_matrix_identity();
    gs_ortho(0, (float)width, 0, (float)height, -100, 100);
    struct vec4 clear;
    vec4_zero(&clear);
    gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
    gs_eparam_t *param = s->ep_image;
    if (param)
        gs_effect_set_texture(param, image);
    if (s->render_scale_percent != 100) {
        gs_blend_state_push();
        gs_enable_blending(false);
    }
    while (gs_effect_loop(s->effect, "Draw"))
        gs_draw_sprite(image, 0, width, height);
    if (s->render_scale_percent != 100)
        gs_blend_state_pop();
    gs_matrix_pop();
    gs_projection_pop();
    gs_texrender_end(s->output_composite_fx);
    return gs_texrender_get_texture(s->output_composite_fx);
}

static bool output_draw_composited(struct hypershader_data *s,
                                   gs_texture_t *base, gs_texture_t *effect,
                                   uint32_t width, uint32_t height,
                                   enum gs_color_format format)
{
    if (!effect || !output_compositor_prepare(s, format))
        return false;
    gs_effect_t *compositor = s->output_compositor;
    gs_eparam_t *base_param = s->compose_base_image;
    gs_eparam_t *fx_param = s->compose_fx_image;
    gs_eparam_t *base_a = s->compose_base_alpha;
    gs_eparam_t *fx_a = s->compose_fx_alpha;
    gs_eparam_t *blend = s->compose_mode;
    if (!base_param || !fx_param || !base_a || !fx_a || !blend)
        return false;
    gs_effect_set_texture(base_param, base ? base : s->output_transparent);
    gs_effect_set_texture(fx_param, effect);
    gs_effect_set_float(base_a, base ? s->compose_base_opacity : 0.0f);
    gs_effect_set_float(fx_a, s->compose_effect_opacity);
    gs_effect_set_int(blend, s->compose_blend_mode);
    while (gs_effect_loop(compositor, "Draw"))
        gs_draw_sprite(effect, 0, width, height);
    return true;
}

static bool output_draw_raw(gs_texture_t *texture, uint32_t width, uint32_t height)
{
    if (!texture) return false;
    gs_effect_t *base = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_eparam_t *image = gs_effect_get_param_by_name(base, "image");
    if (!image) return false;
    gs_effect_set_texture(image, texture);
    while (gs_effect_loop(base, "Draw"))
        gs_draw_sprite(texture, 0, width, height);
    return true;
}

static bool output_present_scaled(struct hypershader_data *s, gs_texture_t *base,
    gs_texture_t *fx, uint32_t width, uint32_t height, enum gs_color_format format)
{
    return output_composition_enabled(s)
        ? output_draw_composited(s, base, fx, width, height, format)
        : output_draw_raw(fx, width, height);
}

/* Capture the incoming filter video; this is the *real* upstream frame, not
 * an artificial black fill or the filter's previous output. */
static gs_texture_t *output_capture_filter_base(struct hypershader_data *s,
                                                 obs_source_t *target,
                                                 uint32_t width, uint32_t height,
                                                 enum gs_color_format format)
{
    if (!target || !output_compositor_prepare(s, format))
        return NULL;
    gs_texrender_reset(s->output_composite_base);
    if (!gs_texrender_begin(s->output_composite_base, width, height))
        return NULL;
    gs_projection_push();
    gs_matrix_push();
    gs_matrix_identity();
    gs_ortho(0, (float)width, 0, (float)height, -100, 100);
    struct vec4 clear;
    vec4_zero(&clear);
    gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
    if (obs_source_get_type(target)==OBS_SOURCE_TYPE_FILTER ||
        (obs_source_get_output_flags(target)&OBS_SOURCE_CUSTOM_DRAW)) {
        obs_source_video_render(target);
    } else {
        gs_effect_t *base = obs_get_base_effect(OBS_EFFECT_DEFAULT);
        while (gs_effect_loop(base, "Draw"))
            obs_source_video_render(target);
    }
    gs_matrix_pop();
    gs_projection_pop();
    gs_texrender_end(s->output_composite_base);
    return gs_texrender_get_texture(s->output_composite_base);
}

/* Called only for an admitted synchronous capture or a signaled GPU fence. */
static bool mp_submit_staged_frame(struct hypershader_data *s)
{
    if (!mp_epoch_current(s,s->mp_capture_epoch)) return false;
    const int mp_w=192, mp_h=192;
    uint8_t *pixels=NULL;
    uint32_t pitch=0;
    if (!gs_stagesurface_map(s->mp_staging_tex,&pixels,&pitch))
        return false;
    const bool valid=pixels && pitch >= (uint32_t)mp_w*4;
    if (valid) {
        for (int row=0;row<mp_h;row++)
            memcpy(s->mp_preallocated_frame+(size_t)row*mp_w*4,
                   pixels+(size_t)row*pitch,mp_w*4);
    }
    gs_stagesurface_unmap(s->mp_staging_tex);
    if (!valid)
        return false;

    /* Worker owns the reservation once queued. Never wait on its mutex
     * while holding the graphics context, even for a short submission. */
    if (pthread_mutex_trylock(&s->mp_mutex)!=0)
        return false;
    if (!mp_epoch_current(s,s->mp_capture_epoch)) {
        pthread_mutex_unlock(&s->mp_mutex);
        return false;
    }
    s->mp_pending_task_storage=(struct mp_process_task_data){
        .s=s,.frame_data=s->mp_preallocated_frame,.width=mp_w,.height=mp_h,
        .epoch=s->mp_capture_epoch,.graph=s->mp_requested_graph};
    s->mp_pending_task_active=true;
    pthread_cond_signal(&s->mp_cond);
    pthread_mutex_unlock(&s->mp_mutex);
    return true;

}

static void mp_readback_failed(struct hypershader_data *s)
{
    if (!s->mp_readback_failure_logged) {
        blog(LOG_WARNING, "[hypershader] Native GL readback fence failed; dropping capture");
        s->mp_readback_failure_logged = true;
    }
    if (s->mp_readback_fence)
        mp_cancel_pending_readback(s);
    else if (s->mp_staging_tex) {
        gs_stagesurface_destroy(s->mp_staging_tex);
        s->mp_staging_tex = NULL;
    }
}

static void mp_poll_pending_readback(struct hypershader_data *s, bool flush)
{
    const uint64_t now = os_gettime_ns();
    /* A hidden source may resume much later. Never deliver its old frame. */
    if (now < s->mp_readback_started_ns || now-s->mp_readback_started_ns > 250000000ULL) {
        mp_cancel_pending_readback(s);
        return;
    }
    enum hs_readback_status status = hs_readback_fence_poll(s->mp_readback_fence,flush);
    if (status == HS_READBACK_PENDING)
        return;
    if (status == HS_READBACK_FAILED) {
        mp_readback_failed(s);
        return;
    }
    hs_readback_fence_destroy(s->mp_readback_fence);
    s->mp_readback_fence = NULL;
    s->mp_readback_started_ns = 0;
    if (!mp_submit_staged_frame(s))
        atomic_store_explicit(&s->mp_frame_buffer_in_use,false,memory_order_release);
}

/* At most one native inference capture/result may be outstanding. Reserve
 * before any scaling, readback or mutex access. A busy worker or duplicate
 * video frame is a cheap drop, not a synchronous graphics stall. */
static void process_mediapipe_frame(struct hypershader_data *s,
                                    gs_texture_t *explicit_source)
{
    if (!s->mp_runner || !s->mp_enabled || !s->mp_thread_run ||
        !s->mp_preallocated_frame) {
        mp_cancel_pending_readback(s);
        return;
    }
    if (s->mp_readback_fence && !mp_epoch_current(s,s->mp_capture_epoch))
        mp_cancel_pending_readback(s);
    const uint64_t frame = obs_get_video_frame_time();
    if (s->mp_capture_frame_valid && s->mp_last_capture_frame == frame)
        return;
    if (s->mp_readback_fence) {
        s->mp_last_capture_frame = frame;
        mp_poll_pending_readback(s, false);
        return;
    }
    obs_source_t *target = obs_filter_get_target(s->context);
    if (!target)
        return;
    const uint32_t sw = obs_source_get_base_width(target);
    const uint32_t sh = obs_source_get_base_height(target);
    if (!sw || !sh)
        return;

    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(&s->mp_frame_buffer_in_use,
            &expected,true,memory_order_acquire,memory_order_relaxed))
        return;

    s->mp_capture_epoch=atomic_load_explicit(&s->mp_epoch,memory_order_acquire);
    s->mp_last_capture_frame = frame;
    s->mp_capture_frame_valid = true;
    const int mp_w=192, mp_h=192;
    if (!s->mp_scaler)
        s->mp_scaler=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
    if (!s->mp_scaler)
        goto drop;
    gs_texrender_reset(s->mp_scaler);
    if (!gs_texrender_begin(s->mp_scaler,mp_w,mp_h))
        goto drop;

    gs_projection_push();
    gs_matrix_push();
    gs_matrix_identity();
    gs_ortho(0.0f,(float)mp_w,0.0f,(float)mp_h,-100.0f,100.0f);
    gs_effect_t *basic=obs_get_base_effect(OBS_EFFECT_DEFAULT);
    if (explicit_source) {
        gs_eparam_t *param=gs_effect_get_param_by_name(basic,"image");
        if (param)
            gs_effect_set_texture(param,explicit_source);
        while (gs_effect_loop(basic,"Draw"))
            gs_draw_sprite(explicit_source,0,mp_w,mp_h);
    } else {
        gs_matrix_scale3f((float)mp_w/(float)sw,(float)mp_h/(float)sh,1.0f);
        while (gs_effect_loop(basic,"Draw"))
            obs_source_video_render(target);
    }
    gs_matrix_pop();
    gs_projection_pop();
    gs_texrender_end(s->mp_scaler);

    gs_texture_t *scaled=gs_texrender_get_texture(s->mp_scaler);
    if (!scaled)
        goto drop;
    if (!s->mp_staging_tex)
        s->mp_staging_tex=gs_stagesurface_create(mp_w,mp_h,GS_RGBA);
    if (!s->mp_staging_tex)
        goto drop;

    if (!s->mp_readback_backend_known) {
        s->mp_readback_can_poll = hs_readback_fence_supported();
        s->mp_readback_backend_known = true;
        blog(LOG_INFO, "[hypershader] Native inference readback: %s",
             s->mp_readback_can_poll ? "zero-timeout OpenGL fence polling" :
             "bounded synchronous staging (backend readiness unavailable)");
    }
    gs_stage_texture(s->mp_staging_tex,scaled);
    if (s->mp_readback_can_poll) {
        s->mp_readback_fence = hs_readback_fence_insert();
        if (!s->mp_readback_fence) {
            mp_readback_failed(s);
            goto drop;
        }
        s->mp_readback_started_ns = os_gettime_ns();
        mp_poll_pending_readback(s, true);
        return;
    }
    if (mp_submit_staged_frame(s))
        return;

drop:
    atomic_store_explicit(&s->mp_frame_buffer_in_use,false,memory_order_release);
}

static bool output_render_composited_filter(struct hypershader_data *s,
                                             obs_source_t *target,
                                             uint32_t width, uint32_t height,
                                             enum gs_color_format format)
{
    gs_texture_t *base = output_capture_filter_base(s, target, width, height, format);
    if (!base)
        return false;
    /* Preserve the real MediaPipe capture path in composited filters. */
    process_mediapipe_frame(s, base);
    gs_texture_t *fx = output_render_effect(s, base,
        hs_render_scale_extent(width,s->render_scale_percent),
        hs_render_scale_extent(height,s->render_scale_percent), format);
    return fx && output_present_scaled(s, base, fx, width, height, format);
}

/* Preflight the complete logical allocation before touching the GPU graph.
 * Never silently downscale or lower precision; that changes shader semantics. */
static bool graph_budget_allows(struct hypershader_data *s, uint32_t width, uint32_t height,
				enum gs_color_format format)
{
	const uint32_t output_bpp = format == GS_RGBA16F ? 8 : 4;
	uint32_t buffer_bpp[HS_GRAPH_MAX_PASSES];
	for (unsigned i = 0; i < s->graph.count; ++i)
		buffer_bpp[i] = s->graph.float16[i] ? 8 : output_bpp;
	const uint64_t limit = (uint64_t)s->graph.budget_mib * HS_GRAPH_MIB;
	uint64_t bytes = 0;
	const bool valid = hs_graph_scaled_bytes(width,height,
        hs_render_scale_extent(width,s->render_scale_percent),
        hs_render_scale_extent(height,s->render_scale_percent),
        output_bpp,s->graph.divisor,buffer_bpp,
        s->graph.transient,s->graph.owner,s->graph.count,&bytes);
	if (!valid || bytes > limit) {
		if (!s->graph_budget_rejected) {
			graph_release_targets(s);
			blog(LOG_WARNING, "[hypershader] Graph resource budget exceeded (%ux%u, bytes=%" PRIu64
			     ", limit=%" PRIu64 ", overflow=%d); %s",
			     width, height, bytes, limit, !valid,
			     s->kind == HYPER_KIND_SOURCE ? "drawing no output" : "passing upstream source");
			s->graph_budget_rejected = true;
		}
		return false;
	}
	if (s->graph_budget_rejected) {
		s->graph_budget_rejected = false;
		blog(LOG_INFO, "[hypershader] Graph resource budget recovered (%ux%u)", width, height);
	}
	return true;
}

static bool graph_render(struct hypershader_data *s, obs_source_t *target,
				uint32_t width, uint32_t height, enum gs_color_format format)
{
    const uint32_t rw=hs_render_scale_extent(width,s->render_scale_percent);
    const uint32_t rh=hs_render_scale_extent(height,s->render_scale_percent);
	if (s->graph_width != width || s->graph_height != height ||
	    s->graph_format != format || s->graph_scale_percent!=s->render_scale_percent) {
		graph_release_targets(s);
		s->graph_width = width;
		s->graph_height = height;
		s->graph_format = format;
        s->graph_scale_percent=s->render_scale_percent;
	}

	if (target && !s->feedback_input)
		s->feedback_input = gs_texrender_create(format, GS_ZS_NONE);
    if (target && !s->feedback_input)
        return false;
    for (unsigned i=0;i<s->graph.count;i++) {
        if (s->graph.owner[i]!=i) continue;
        for (unsigned slot=0;slot<(s->graph.transient[i]?1U:2U);slot++) {
            if (!s->graph_history[i][slot])
                s->graph_history[i][slot]=gs_texrender_create(
                    s->graph.float16[i]?GS_RGBA16F:format,GS_ZS_NONE);
            if (!s->graph_history[i][slot]) return false;
        }
    }

	/* A standalone generator has no upstream image. Match the existing
	 * source lane's opaque-black placeholder; external channels still work. */
	gs_texture_t *input = s->dummy_texture;
	if (target) {
		/* Capture the upstream source independently of the filter-end helper. */
		gs_texrender_reset(s->feedback_input);
		if (!gs_texrender_begin(s->feedback_input, width, height))
			return false;
		gs_projection_push();
		gs_matrix_push();
		gs_matrix_identity();
		gs_ortho(0, (float)width, 0, (float)height, -100, 100);
		/* A nested filter (e.g. Sight) owns its capture/draw effect; entering
		 * a default-effect loop here can suppress its nested capture.
		 * Ordinary non-custom input sources still need an active Draw effect
		 * for obs_source_draw(). Preserve that established path. */
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
		if (obs_source_get_type(target) == OBS_SOURCE_TYPE_FILTER ||
		    (obs_source_get_output_flags(target) & OBS_SOURCE_CUSTOM_DRAW)) {
			obs_source_video_render(target);
		} else {
			gs_effect_t *base = obs_get_base_effect(OBS_EFFECT_DEFAULT);
			while (gs_effect_loop(base, "Draw"))
				obs_source_video_render(target);
		}
		gs_matrix_pop();
		gs_projection_pop();
		gs_texrender_end(s->feedback_input);
		input = gs_texrender_get_texture(s->feedback_input);
	}
	if (!input)
		return false;

    pthread_mutex_lock(&s->mutex);
    const bool flow_selected=s->sight_use_tracking;
    pthread_mutex_unlock(&s->mutex);
    const bool flow=flow_selected&&s->sight_flow_present&&s->sight_flow_texture;
    const uint64_t flow_sequence=flow?s->sight_flow_sequence:0;
    const bool flow_step=s->graph_valid&&hs_sight_flow_step(s->graph_flow_sequence,flow_sequence,flow);
	const unsigned write = s->graph_write_idx;
	const unsigned read = write ^ 1U;
	/* A 1x1 fallback is not sufficient for integer texel access. Initialize
	 * each actual history allocation to opaque black before its first read,
	 * preserving the existing initial color/alpha contract. */
	if (!s->graph_valid) {
		struct vec4 black;
		vec4_set(&black, 0.0f, 0.0f, 0.0f, 1.0f);
		for (unsigned i = 0; i < s->graph.count; ++i) {
			if (s->graph.transient[i]) continue;
            gs_texrender_t *history = s->graph_history[i][read];
			gs_texrender_reset(history);
			if (!gs_texrender_begin(history, hs_graph_extent(rw, s->graph.divisor[i]),
						hs_graph_extent(rh, s->graph.divisor[i])))
				return false;
			gs_clear(GS_CLEAR_COLOR, &black, 0.0f, 0);
			gs_texrender_end(history);
		}
	}
	gs_texture_t *previous[HS_GRAPH_MAX_PASSES] = {0};
	gs_texture_t *current[HS_GRAPH_MAX_PASSES] = {0};
	for (unsigned i = 0; i < s->graph.count; ++i) {
		if (s->graph.transient[i]) continue;
        previous[i] = gs_texrender_get_texture(s->graph_history[i][read]);
		if (!previous[i] || gs_texture_get_width(previous[i]) != hs_graph_extent(rw, s->graph.divisor[i]) ||
		    gs_texture_get_height(previous[i]) != hs_graph_extent(rh, s->graph.divisor[i]) ||
		    gs_texture_get_color_format(previous[i]) != (s->graph.float16[i] ? GS_RGBA16F : format))
			return false;
	}
	gs_eparam_t *image = s->ep_image;
	for (unsigned pass = 0; pass < s->graph.count; pass++) {
		gs_texrender_t *destination = s->graph_history[s->graph.owner[pass]][s->graph.transient[pass]?0:write];
		const uint32_t pass_width = hs_graph_extent(rw, s->graph.divisor[pass]);
		const uint32_t pass_height = hs_graph_extent(rh, s->graph.divisor[pass]);
		gs_texrender_reset(destination);
		if (!gs_texrender_begin(destination, pass_width, pass_height))
			return false;
		gs_projection_push();
		gs_matrix_push();
		gs_matrix_identity();
		gs_ortho(0, (float)pass_width, 0, (float)pass_height, -100, 100);
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);

		if (image)
			gs_effect_set_texture(image, input);
		/* Effect loops may clear sampled texture state between techniques.
		 * Every graph pass owns the complete previous-frame contract. */
		for (unsigned i = 0; i < s->graph.count; ++i) {
            if (s->graph.transient[i]) {
                /* Never bind an alias of the active destination. Unproduced
                 * and expired logical resources are explicitly unavailable. */
                gs_effect_set_texture(s->graph.current[i],
                    i<pass && pass<=s->graph.last_use[i]?current[i]:s->dummy_texture);
            } else {
                gs_effect_set_texture(s->graph.previous[i],previous[i]);
                gs_effect_set_texture(s->graph.current[i],i<pass?current[i]:previous[i]);
            }
		}
		graph_bind_controls(s);
		graph_bind_builtins(s, pass_width, pass_height, flow, flow_step, flow_selected);
		graph_bind_resolutions(s, rw, rh);
        /* Graph buffers store simulation data, not premultiplied output.
         * Alpha blending destroyed private alpha state (e.g. Datamosh's
         * temporal accumulator) when the first frame started at alpha=0.
         * Overwrite the offscreen surface exactly; restore caller state. */
        gs_blend_state_push();
        gs_enable_blending(false);
        while (gs_effect_loop(s->effect, s->graph.technique[pass]))
            gs_draw_sprite(input, 0, pass_width, pass_height);
        gs_blend_state_pop();
		gs_matrix_pop();
		gs_projection_pop();
		gs_texrender_end(destination);
		gs_texture_t *output = gs_texrender_get_texture(destination);
		if (!output || gs_texture_get_width(output) != pass_width ||
		    gs_texture_get_height(output) != pass_height ||
		    gs_texture_get_color_format(output) != (s->graph.float16[pass] ? GS_RGBA16F : format))
			return false;
		current[pass] = output;
	}

	if (image)
		gs_effect_set_texture(image, input);
	for (unsigned i = 0; i < s->graph.count; ++i) {
        gs_effect_set_texture(s->graph.current[i],
            s->graph.transient[i] && s->graph.last_use[i]<s->graph.count?s->dummy_texture:current[i]);
        if (!s->graph.transient[i])
            gs_effect_set_texture(s->graph.previous[i],previous[i]);
	}
	graph_bind_controls(s);
    graph_bind_builtins(s, rw, rh, flow, flow_step, flow_selected);
    graph_bind_resolutions(s, rw, rh);
    if (output_composition_enabled(s) || s->render_scale_percent != 100) {
        gs_texture_t *fx = output_render_effect(s, input, rw, rh, format);
        if (!fx || !output_present_scaled(s, target ? input : NULL, fx, width, height, format))
            return false;
    } else {
        while (gs_effect_loop(s->effect, "Draw"))
            gs_draw_sprite(input, 0, width, height);
    }

	s->graph_write_idx = read;
	s->graph_flow_sequence = flow_sequence;
    if(s->ep_sight_flow_step&&flow_step&&!s->graph_flow_announced) {
        s->graph_flow_announced=true;
        blog(LOG_INFO,"[hypershader] SIGHT_FLOW_GRAPH_STEP_20261118: consumed new motion snapshot");
    }
	s->graph_frame++;
	s->graph_valid = true;
	return true;
}

static void filter_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct hypershader_data *s = data;
	if (!s || s->channel_capture_active)
		return;

	/* Initialize timing */
	uint64_t start_time = os_gettime_ns();

	/* SAFE PROPERTY REFRESH with mutex protection */
	/* Unified UI refresh - queues update to UI thread if dirty flag is set */
	check_and_refresh_ui(s);

/* Issue 5: Hot Reload Error Recovery */
#ifdef _DEBUG
	uint64_t now = os_gettime_ns();

	if (s->watcher && !s->hot_reload_state.reload_pending &&
	    check_shader_modified((struct shader_watcher *)s->watcher)) {
		/* Rate limit reload attempts to prevent spam */
		if (now - s->hot_reload_state.last_reload_attempt > 1000000000ULL) {
			/* Preserve current working effect as backup */
			if (s->effect && !s->hot_reload_state.recovery_mode) {
				if (s->hot_reload_state.backup_effect) {
					gs_effect_destroy(s->hot_reload_state.backup_effect);
				}
				s->hot_reload_state.backup_effect = s->effect;
				s->effect = NULL; /* Recreated after queued update */
			}

			s->hot_reload_state.reload_pending = true;
			s->hot_reload_state.last_reload_attempt = now;
			obs_source_get_ref(s->context);
			obs_queue_task(OBS_TASK_UI, handle_hot_reload_update_task, s->context, false);
		}
	}

	if (s->hot_reload_state.reload_pending) {
		/* Success path: new effect was swapped by queued update */
		if (s->effect) {
			if (s->hot_reload_state.backup_effect) {
				gs_effect_destroy(s->hot_reload_state.backup_effect);
				s->hot_reload_state.backup_effect = NULL;
			}
			bfree(s->hot_reload_state.backup_error);
			s->hot_reload_state.backup_error = NULL;
			s->hot_reload_state.recovery_mode = false;
			s->hot_reload_state.consecutive_failures = 0;
			s->hot_reload_state.reload_pending = false;
		} else if (now - s->hot_reload_state.last_reload_attempt > 2000000000ULL) {
			/* Failure path: queued update finished without a new effect */
			s->hot_reload_state.consecutive_failures++;
			s->hot_reload_state.recovery_mode = true;
			s->hot_reload_state.reload_pending = false;

			if (s->hot_reload_state.backup_effect) {
				s->effect = s->hot_reload_state.backup_effect;
				s->hot_reload_state.backup_effect = NULL;
			}

			bfree(s->hot_reload_state.backup_error);
			s->hot_reload_state.backup_error =
				bstrdup(s->shader_error ? s->shader_error : "Unknown shader compilation error");
		}
	}
#endif

	obs_source_t *target = obs_filter_get_target(s->context);
	if (!target) {
		obs_source_skip_video_filter(s->context);
		return;
	}

	/* pthread_mutex_lock(&s->mutex); - REMOVED (High Contention) */
	if (!s->effect) {
		/* pthread_mutex_unlock(&s->mutex); - REMOVED */
		obs_source_skip_video_filter(s->context);
		return;
	}

	uint32_t width = obs_source_get_base_width(target);
	uint32_t height = obs_source_get_base_height(target);

	if (width == 0 || height == 0) {
		obs_source_skip_video_filter(s->context);
		return;
	}

	consume_history_reset(s);
	const uint32_t rw=hs_render_scale_extent(width,s->render_scale_percent);
    const uint32_t rh=hs_render_scale_extent(height,s->render_scale_percent);
    update_shader_uniforms(s, rw, rh);

	/* Modern Color Space Handling (Rule 8) */
	enum gs_color_space space = filter_get_color_space(s, 0, NULL);
	enum gs_color_format format = GS_RGBA;

	/* Support HDR if space is 709 SCRGB or extended */
	if (space == GS_CS_SRGB_16F || space == GS_CS_709_EXTENDED || space == GS_CS_709_SCRGB) {
		format = GS_RGBA16F;
	}

	if (s->graph.enabled) {
		if (!s->graph.valid || !graph_budget_allows(s, width, height, format)) {
			obs_source_skip_video_filter(s->context);
			return;
		}
		if (!graph_render(s, target, width, height, format)) {
			blog(LOG_WARNING, "[hypershader] Graph render failed; passing upstream source");
			obs_source_skip_video_filter(s->context);
		}
		update_performance_stats(s, (double)(os_gettime_ns() - start_time) / 1000000.0);
		return;
	}

	/* Feedback needs both the original camera frame and the previous
	 * *decoded output*. The normal filter-end API binds the original image
	 * only while it draws, so we must own an intermediate input capture.
	 * This path does not call process_filter_begin/end (or a NULL sprite).
	 * MediaPipe-aware shaders remain on the existing non-feedback route. */
	if (s->ep_history && s->effect && s->metadata_rc &&
	    !s->metadata_rc->capabilities.mediapipe_enabled) {
		if (s->feedback_width != width || s->feedback_height != height ||
            s->feedback_scale_percent != s->render_scale_percent) {
			for (int i=0;i<2;i++) {
				if (s->history[i]) {
					gs_texrender_destroy(s->history[i]);
					s->history[i] = NULL;
				}
			}
			s->history_idx=0;
			s->feedback_width=width;
			s->feedback_height=height;
            s->feedback_scale_percent=s->render_scale_percent;
		}
		if (!s->feedback_input)
			s->feedback_input=gs_texrender_create(format,GS_ZS_NONE);
		if (!s->history[0])
			s->history[0]=gs_texrender_create(format,GS_ZS_NONE);
		if (!s->history[1])
			s->history[1]=gs_texrender_create(format,GS_ZS_NONE);
		if (s->feedback_input && s->history[0] && s->history[1]) {
			gs_texrender_reset(s->feedback_input);
			if (gs_texrender_begin(s->feedback_input,width,height)) {
				gs_projection_push();
				gs_matrix_push();
				gs_matrix_identity();
				gs_ortho(0,(float)width,0,(float)height,-100,100);
                if (obs_source_get_type(target)==OBS_SOURCE_TYPE_FILTER ||
                    (obs_source_get_output_flags(target)&OBS_SOURCE_CUSTOM_DRAW)) {
                    obs_source_video_render(target);
                } else {
                    gs_effect_t *default_fx=obs_get_base_effect(OBS_EFFECT_DEFAULT);
                    while (gs_effect_loop(default_fx,"Draw"))
                        obs_source_video_render(target);
                }
				gs_matrix_pop();
				gs_projection_pop();
				gs_texrender_end(s->feedback_input);

				gs_texture_t *input=gs_texrender_get_texture(s->feedback_input);
				int ping=s->history_idx;
				int pong=(ping+1)%2;
				gs_texture_t *previous=s->history_needs_seed ? NULL : gs_texrender_get_texture(s->history[pong]);
				/* Seed a reduced-resolution history from the full-size upstream
                 * frame using the same normalized UVs as subsequent frames. */
                if (!previous && input && s->render_scale_percent!=100) {
                    gs_texrender_reset(s->history[pong]);
                    if (!gs_texrender_begin(s->history[pong],rw,rh)) {
                        obs_source_skip_video_filter(s->context);
                        return;
                    }
                    gs_ortho(0,(float)rw,0,(float)rh,-100,100);
                    gs_blend_state_push();
                    gs_enable_blending(false);
                    bool seeded=output_draw_raw(input,rw,rh);
                    gs_blend_state_pop();
                    gs_texrender_end(s->history[pong]);
                    if (!seeded) {obs_source_skip_video_filter(s->context);return;}
                    previous=gs_texrender_get_texture(s->history[pong]);
                }
                gs_texrender_reset(s->history[ping]);
				if (input && gs_texrender_begin(s->history[ping],rw,rh)) {
					gs_projection_push();
					gs_matrix_push();
					gs_matrix_identity();
					gs_ortho(0,(float)rw,0,(float)rh,-100,100);
					gs_eparam_t *image=s->ep_image;
					if (image)gs_effect_set_texture(image,input);
					gs_effect_set_texture(s->ep_history,previous ? previous : input);
					while (gs_effect_loop(s->effect,"Draw"))
						gs_draw_sprite(input,0,rw,rh);
					gs_matrix_pop();
					gs_projection_pop();
					gs_texrender_end(s->history[ping]);
					gs_texture_t *result=gs_texrender_get_texture(s->history[ping]);
                    if (result) {
                        if (output_composition_enabled(s)) {
                            if (!output_draw_composited(s, input, result, width, height, format)) {
                                obs_source_skip_video_filter(s->context);
                                return;
                            }
                        } else {
                        /* Draw the decoded output, not the original source. OBS's
						 * default effect requires its image sampler to be bound before
						 * gs_effect_loop; gs_draw_sprite alone does not bind it. */
						gs_effect_t *base=obs_get_base_effect(OBS_EFFECT_DEFAULT);
						gs_eparam_t *image=gs_effect_get_param_by_name(base,"image");
						if (image)gs_effect_set_texture(image,result);
                        while(gs_effect_loop(base,"Draw"))
                            gs_draw_sprite(result,0,width,height);
                        } /* default raw output */
						s->history_idx=pong;
                        s->history_needs_seed=false;
						update_performance_stats(s,(double)(os_gettime_ns()-start_time)/1000000.0);
						if(!s->feedback_first_render_logged) {
							blog(LOG_INFO,"[hypershader] ACTIVE_FEEDBACK_CAPTURE_20261010: %ux%u",width,height);
							s->feedback_first_render_logged=true;
						}
						return;
					}
				}
			}
		}
		blog(LOG_WARNING,"[hypershader] Feedback targets unavailable; passing through source");
		obs_source_skip_video_filter(s->context);
		return;
	}

    if (output_composition_enabled(s) || s->render_scale_percent != 100) {
        if (!output_render_composited_filter(s, target, width, height, format)) {
            blog(LOG_WARNING, "[hypershader] Composition render failed; passing source");
            obs_source_skip_video_filter(s->context);
        }
        update_performance_stats(s, (double)(os_gettime_ns()-start_time)/1000000.0);
        return;
    }

	if (!obs_source_process_filter_begin_with_color_space(s->context, format, space, OBS_NO_DIRECT_RENDERING)) {
		obs_source_skip_video_filter(s->context);
		return;
	}

	process_mediapipe_frame(s, NULL);

	obs_source_process_filter_end(s->context, s->effect, width, height);

	/* Update Performance Stats */
	uint64_t end_time = os_gettime_ns();
	double duration_ms = (double)(end_time - start_time) / 1000000.0;
	update_performance_stats(s, duration_ms);

	/* pthread_mutex_unlock(&s->mutex); - REMOVED */
}

/* Shader metadata parsing moved to src/shader/shader_metadata.c */
extern void free_shader_metadata(shader_meta_t *da);
extern void parse_shader_metadata_internal(const char *path, shader_meta_t *da, int depth, bool *channel_usage,
					   char **include_guard, size_t *guard_count, size_t max_guard);
extern void parse_shader_metadata(const char *path, shader_meta_t *da, bool *channel_usage,
					  struct shader_capabilities *capabilities);

/* ============================================================================
 * CACHED EFFECT PARAM VIEWS FOR UI
 * Populated during shader load (in graphics context), read by properties UI
 * without entering graphics context.
 * ============================================================================ */

void cache_effect_params(struct hypershader_data *s)
{
	if (!s)
		return;

	pthread_mutex_lock(&s->mutex);

	/* Free old cache */
	if (s->param_views) {
		for (size_t i = 0; i < s->param_view_count; i++)
			bfree(s->param_views[i].name);
		bfree(s->param_views);
		s->param_views = NULL;
		s->param_view_count = 0;
	}

	gs_effect_t *effect = s->effect;
	if (!effect)
		goto done;

	size_t count = gs_effect_get_num_params(effect);
	if (count == 0)
		goto done;

	s->param_views = bzalloc(sizeof(struct shader_param_view) * count);
	if (!s->param_views)
		goto done;

	for (size_t i = 0; i < count; i++) {
		gs_eparam_t *param = gs_effect_get_param_by_idx(effect, i);
		if (!param)
			continue;

		struct gs_effect_param_info info;
		gs_effect_get_param_info(param, &info);

		if (!info.name || !info.name[0])
			continue;

		s->param_views[s->param_view_count].name = bstrdup(info.name);
		s->param_views[s->param_view_count].type = info.type;
		s->param_view_count++;
	}

done:
	pthread_mutex_unlock(&s->mutex);
}

void free_effect_param_cache(struct hypershader_data *s)
{
	if (!s)
		return;

	pthread_mutex_lock(&s->mutex);
	if (s->param_views) {
		for (size_t i = 0; i < s->param_view_count; i++)
			bfree(s->param_views[i].name);
		bfree(s->param_views);
		s->param_views = NULL;
	}
	s->param_view_count = 0;
	pthread_mutex_unlock(&s->mutex);
}

const char *get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "hypershader";
}

bool reload_examples_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	(void)props;
	(void)property;
	(void)data;
	bool ok = scan_shaders_dir();
	return ok; /* returning true will refresh the properties UI */
}

/* ----- Sound preview: generate a short sine WAV and play it via aplay (if available) ----- */

static void filter_mouse_move(void *data, const struct obs_mouse_event *event, bool mouse_leave)
{
	struct hypershader_data *s = data;
	if (!s)
		return;

	if (mouse_leave) {
		vec2_zero(&s->mouse_pos);
	} else {
		s->mouse_pos.x = (float)event->x;
		s->mouse_pos.y = (float)event->y;
	}
}

static void filter_mouse_click(void *data, const struct obs_mouse_event *event, int32_t type, bool mouse_up,
			       uint32_t click_count)
{
	struct hypershader_data *s = data;
	if (!s)
		return;

	if (type == MOUSE_LEFT && click_count == 1) {
		if (!mouse_up) {
			/* Left mouse button down - record click position */
			s->mouse_click.x = (float)event->x;
			s->mouse_click.y = (float)event->y;
		} else {
			/* Left mouse button released - set negative values according to the compatibility mouse convention */
			s->mouse_click.x = -1.0f;
			s->mouse_click.y = -1.0f;
		}
	}
}

static void workload_filter_added(void *data, obs_source_t *parent)
{
    struct hypershader_data *s=data;
    atomic_store_explicit(&s->workload_showing,obs_source_showing(parent),memory_order_release);
}

static void workload_filter_removed(void *data, obs_source_t *parent)
{
    UNUSED_PARAMETER(parent);
    struct hypershader_data *s=data;
    workload_hide(s);
    atomic_store_explicit(&s->workload_last_draw_ns,0,memory_order_release);
}

struct obs_source_info hypershader_filter_info = {
	.id = "hypershader_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_INTERACTION,
	.get_name = get_name,
	.create = filter_create_wrapper,
	.destroy = filter_destroy,
	.update = filter_update,
	.video_tick = filter_tick,
    .show = workload_show,
    .hide = workload_hide,
    .filter_add = workload_filter_added,
    .filter_remove = workload_filter_removed,
	.video_render = filter_video_render,
	.get_properties = hyper_ui_get_properties,
	.get_defaults = hyper_ui_get_defaults,
	.mouse_move = filter_mouse_move,
	.mouse_click = filter_mouse_click,
	.video_get_color_space = filter_get_color_space,
};

/* ============================================================================
 * hypershader SOURCE IMPLEMENTATION
 * ============================================================================ */

static enum gs_color_space source_get_color_space(void *data, size_t count, const enum gs_color_space *preferred)
{
	/* Source has no filter target, so use the preferred pipeline space
	   or default to sRGB for maximum compatibility. HDR-aware shaders
	   can return GS_CS_SRGB_16F here if they handle extended range. */
	UNUSED_PARAMETER(data);
	if (count > 0 && preferred)
		return preferred[0];
	return GS_CS_SRGB;
}

static uint32_t source_get_width(void *data)
{
	struct hypershader_data *s = data;
	if (!s)
		return HYPER_SOURCE_WIDTH_DEFAULT;
	if (s->source_width < HYPER_SOURCE_WIDTH_MIN)
		return HYPER_SOURCE_WIDTH_MIN;
	if (s->source_width > HYPER_SOURCE_WIDTH_MAX)
		return HYPER_SOURCE_WIDTH_MAX;
	return (uint32_t)s->source_width;
}

static uint32_t source_get_height(void *data)
{
	struct hypershader_data *s = data;
	if (!s)
		return HYPER_SOURCE_HEIGHT_DEFAULT;
	if (s->source_height < HYPER_SOURCE_HEIGHT_MIN)
		return HYPER_SOURCE_HEIGHT_MIN;
	if (s->source_height > HYPER_SOURCE_HEIGHT_MAX)
		return HYPER_SOURCE_HEIGHT_MAX;
	return (uint32_t)s->source_height;
}

static void source_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct hypershader_data *s = data;
	if (!s || s->channel_capture_active)
		return;

	/* Initialize timing */
	uint64_t start_time = os_gettime_ns();

	/* Unified UI refresh - queues update to UI thread if dirty flag is set */
	check_and_refresh_ui(s);

	uint32_t width = obs_source_get_width(s->context);
	uint32_t height = obs_source_get_height(s->context);

	/* Single mutex scope for shader state access */
	/* pthread_mutex_lock(&s->mutex); - REMOVED (High Contention) */

	if (s->effect && width && height)
		consume_history_reset(s);
	const uint32_t rw=hs_render_scale_extent(width,s->render_scale_percent);
    const uint32_t rh=hs_render_scale_extent(height,s->render_scale_percent);
    update_shader_uniforms(s, rw, rh);

	if (!s->effect) {
		/* pthread_mutex_unlock(&s->mutex); - REMOVED */
		return;
	}

	/* Source specific: We are the root, so clear 'image' input or bind the black texture */
	gs_eparam_t *image_param = s->ep_image;
	if (image_param)
		gs_effect_set_texture(image_param, s->dummy_texture);

	/* Render Fullscreen Quad */
	/* Note: We don't use obs_source_process_filter_begin because we ARE the source */

	/* Modern Color Space Handling */
	enum gs_color_space space = filter_get_color_space(s, 0, NULL);
	enum gs_color_format format =
		(space == GS_CS_SRGB_16F || space == GS_CS_709_EXTENDED || space == GS_CS_709_SCRGB) ? GS_RGBA16F
												     : GS_RGBA;

	/* Use the same persistent scheduler as the filter lane. On rejection
	 * a generator has no upstream image to pass through: draw nothing. */
	if (s->graph.enabled) {
		if (s->graph.valid && graph_budget_allows(s, width, height, format))
			graph_render(s, NULL, width, height, format);
		update_performance_stats(s, (double)(os_gettime_ns() - start_time) / 1000000.0);
		return;
	}

	/* Cache values we need before unlocking for render operations */
	bool has_persistence = (s->ep_history != NULL);
	gs_effect_t *current_effect = s->effect;
	gs_eparam_t *prev_frame_param = s->ep_history;
	int hist_idx = s->history_idx;

	/* pthread_mutex_unlock(&s->mutex); - REMOVED */

	/* Native Persistence Logic for Source (outside mutex - only uses local vars) */
	if (has_persistence && current_effect) {
		if (!s->history[0])
			s->history[0] = gs_texrender_create(format, GS_ZS_NONE);
		if (!s->history[1])
			s->history[1] = gs_texrender_create(format, GS_ZS_NONE);

		gs_texrender_t *curr = s->history[hist_idx];
		gs_texrender_t *prev = s->history[!hist_idx];
		if (!curr || !prev)
			return;

		/* Source feedback has no upstream frame to seed it. Explicitly
		 * initialize the read surface to transparent, including on resize,
		 * instead of sampling a null texture (opaque black on GL). */
		gs_texture_t *previous = gs_texrender_get_texture(prev);
		if (s->history_needs_seed || !previous || gs_texture_get_width(previous) != rw ||
		    gs_texture_get_height(previous) != rh) {
			gs_texrender_reset(prev);
			if (!gs_texrender_begin(prev, rw, rh))
				return;
			struct vec4 seed;
			vec4_zero(&seed);
			gs_clear(GS_CLEAR_COLOR, &seed, 0.0f, 0);
			gs_texrender_end(prev);
		}

		/* A texrender must be reset each time it becomes the write target. */
		gs_texrender_reset(curr);
		if (gs_texrender_begin(curr, rw, rh)) {
			struct vec4 clear_color;
			vec4_zero(&clear_color);
			gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
			gs_ortho(0.0f, (float)rw, 0.0f, (float)rh, -100.0f, 100.0f);

			gs_effect_set_texture(prev_frame_param, gs_texrender_get_texture(prev));

			while (gs_effect_loop(current_effect, "Draw")) {
				gs_draw_sprite(s->dummy_texture, 0, rw, rh);
			}
			gs_texrender_end(curr);
		} else {
			return;
		}

		/* Draw the produced frame: the default effect does not implicitly
		 * bind the image sampler when gs_draw_sprite is called. */
		gs_texture_t *result = gs_texrender_get_texture(curr);
		gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_eparam_t *image = gs_effect_get_param_by_name(default_effect, "image");
		if (image && result)
			gs_effect_set_texture(image, result);
        if (result) {
            if (output_composition_enabled(s)) {
                if (!output_draw_composited(s, NULL, result, width, height, format))
                    blog(LOG_ERROR, "[hypershader] Standalone source composite failed");
            } else {
                while (gs_effect_loop(default_effect, "Draw"))
                    gs_draw_sprite(result, 0, width, height);
            }
        }

		/* Update history index (atomic write, no mutex needed) */
		s->history_idx = !hist_idx;
		s->history_needs_seed = false;
	} else {
		/* Standard Render (No Persistence) */
        if (output_composition_enabled(s) || s->render_scale_percent != 100) {
            gs_texture_t *fx = output_render_effect(s, s->dummy_texture, rw, rh, format);
            if (!fx || !output_present_scaled(s, NULL, fx, width, height, format))
                blog(LOG_ERROR, "[hypershader] Standalone source composite failed");
        } else {
            while (gs_effect_loop(current_effect, "Draw"))
                gs_draw_sprite(s->dummy_texture, 0, width, height);
        }
	}

	/* Update Stats */
	uint64_t end_time = os_gettime_ns();
	double duration_ms = (double)(end_time - start_time) / 1000000.0;
	update_performance_stats(s, duration_ms);
}

struct obs_source_info hypershader_source_info = {
	.id = "hypershader_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_INTERACTION | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = get_name,
	.create = source_create_wrapper,
	.destroy = filter_destroy,
	.update = filter_update,
	.video_tick = filter_tick,
	.video_render = source_video_render,
	.get_width = source_get_width,
	.get_height = source_get_height,
	.get_properties = hyper_ui_get_source_properties,
	.get_defaults = hyper_ui_get_source_defaults,
	.mouse_move = filter_mouse_move,
	.mouse_click = filter_mouse_click,
	.video_get_color_space = source_get_color_space,
};

/* ============================================================================
 * hypershader TRANSITION IMPLEMENTATION
 * ============================================================================ */

static void mix_render_callback(void *data, gs_texture_t *a, gs_texture_t *b, float t, uint32_t width, uint32_t height)
{
	struct hypershader_data *s = data;
	if (!s || !s->effect) {
		gs_texture_t *fallback = (t < 0.5f || !b) ? a : b;
		if (fallback) {
			/* The callback has no active effect when shader loading fails.
			 * Bind OBS's default technique before drawing either scene. */
			gs_effect_t *base = obs_get_base_effect(OBS_EFFECT_DEFAULT);
			gs_eparam_t *image = base ? gs_effect_get_param_by_name(base, "image") : NULL;
			if (image)
				gs_effect_set_texture(image, fallback);
			if (base && image)
				while (gs_effect_loop(base, "Draw"))
					gs_draw_sprite(fallback, 0, width, height);
		}
	} else {
		/* Mutex locks removed - protected by Graphics Context */
		/* Update Uniforms */
		update_shader_uniforms(s, width, height);

		/* Set Transition Progress 'hs_transition_progress' */
		gs_eparam_t *ep_progress = s->ep_transition_progress;
		if (ep_progress)
			gs_effect_set_float(ep_progress, t);

		/* Bind Source A and B to Channels */
		if (s->ep_input[0])
			gs_effect_set_texture(s->ep_input[0], a);
		if (s->ep_input[1])
			gs_effect_set_texture(s->ep_input[1], b);

		/* Bind sizes if available */
		for (int i = 0; i < 2; i++) {
			if (s->ep_input_resolution[i]) {
				/* CRITICAL FIX: Use actual texture resolution, not output resolution */
				gs_texture_t *tex = (i == 0) ? a : b;
				if (tex) {
					float w = (float)gs_texture_get_width(tex);
					float h = (float)gs_texture_get_height(tex);
					struct vec4 v_res;
					vec4_set(&v_res, w, h, (h > 0) ? w / h : 1.0f, 0.0f);
					gs_effect_set_vec4(s->ep_input_resolution[i], &v_res);
				} else {
					struct vec4 v_res = {(float)width, (float)height, 1.0f, 0.0f};
					gs_effect_set_vec4(s->ep_input_resolution[i], &v_res);
				}
			}
		}

		/* Render using our shader effect */
		/* Note: Mutex removed for consistency with filter/source render paths -
	       graphics context provides thread safety */
        if (output_composition_enabled(s)) {
            gs_texture_t *fx = output_render_effect(s, a, width, height, GS_RGBA);
            if (!fx || !output_draw_composited(s, a, fx, width, height, GS_RGBA))
                blog(LOG_ERROR, "[hypershader] Transition compositor failed");
        } else {
            while (gs_effect_loop(s->effect, "Draw"))
                gs_draw_sprite(a, 0, width, height);
        }
	}
}

static enum gs_color_space transition_get_color_space(void *data, size_t count, const enum gs_color_space *preferred)
{
	/* Transition delegates color space to the pipeline default.
	   More sophisticated implementations could query child sources. */
	UNUSED_PARAMETER(data);
	if (count > 0 && preferred)
		return preferred[0];
	return GS_CS_SRGB;
}

static void transition_video_render(void *data, gs_effect_t *effect)
{
	struct hypershader_data *s = data;
	if (!s)
		return;

	/* Initialize timing */
	uint64_t start_time = os_gettime_ns();

	/* Unified UI refresh - queues update to UI thread if dirty flag is set */
	check_and_refresh_ui(s);

	obs_transition_video_render(s->context, mix_render_callback);

	/* Update Performance Stats */
	uint64_t end_time = os_gettime_ns();
	double duration_ms = (double)(end_time - start_time) / 1000000.0;
	update_performance_stats(s, duration_ms);

	UNUSED_PARAMETER(effect);
}

static float transition_get_time(void *data)
{
	UNUSED_PARAMETER(data);
	return 0.0f;
}

static float transition_mix_audio(void *data, float t)
{
	UNUSED_PARAMETER(data);
	return t;
}

static bool transition_audio_render(void *data, uint64_t *ts_out, struct obs_source_audio_mix *audio_output,
				    uint32_t mixers, size_t channels, size_t sample_rate)
{
	struct hypershader_data *s = data;
	if (!s)
		return false;

	return obs_transition_audio_render(s->context, ts_out, audio_output, mixers, channels, sample_rate,
					   transition_mix_audio, NULL);
}

struct obs_source_info hypershader_transition_info = {
	.id = "hypershader_transition",
	.type = OBS_SOURCE_TYPE_TRANSITION,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = get_name,
	.create = transition_create_wrapper,
	.destroy = filter_destroy,
	.update = filter_update,
	.video_tick = filter_tick,
	.video_render = transition_video_render,
	.audio_render = transition_audio_render,
	.get_properties = hyper_ui_get_transition_properties,
	.get_defaults = hyper_ui_get_transition_defaults,
	.video_get_color_space = transition_get_color_space,
};

/* END OF FILE */
