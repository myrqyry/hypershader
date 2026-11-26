#include "channel_image_compat.h"

#include <string.h>
#include <util/bmem.h>
#include <util/platform.h>

#ifndef _WIN32
#include <dlfcn.h>
#endif

struct hs_image_file_view {
	gs_texture_t *texture;
	enum gs_color_format format;
	uint32_t cx;
	uint32_t cy;
	bool is_animated_gif;
	bool frame_updated;
	bool loaded;
};

typedef void (*hs_image_init_legacy_fn)(void *image, const char *file);
typedef void (*hs_image_init_ex_fn)(void *image, const char *file, enum gs_image_alpha_mode alpha_mode);
typedef void (*hs_image_free_fn)(void *image);
typedef void (*hs_image_init_texture_fn)(void *image);
typedef bool (*hs_image_tick_fn)(void *image, uint64_t elapsed_ns);
typedef void (*hs_image_update_texture_fn)(void *image);

/* OBS 33 public image-file.h prefix, needed by binaries built with OBS 32.
 * No private decoder fields are accessed. OBS's image source resets the
 * same public clock/frame/loop fields before updating its texture. */
#if LIBOBS_API_MAJOR_VER >= 33
typedef gs_image_file_ex_t hs_image_ex_view;
#else
typedef struct {
    struct hs_image_file_view prefix;
    void *internal;
    uint64_t cur_time;
    int cur_frame;
    int cur_loop;
} hs_image_ex_view;
#endif

static hs_image_init_legacy_fn image_init_legacy;
static hs_image_init_ex_fn image_init_ex;
static hs_image_free_fn image_free;
static hs_image_init_texture_fn image_init_texture;
static hs_image_tick_fn image_tick;
static hs_image_update_texture_fn image_update_texture;
static bool image_api_ready;
static bool image_api_ex;

#if LIBOBS_API_MAJOR_VER >= 33
_Static_assert(sizeof(gs_image_file_ex_t) <= HS_CHANNEL_IMAGE_STORAGE_SIZE,
	       "OBS 33 image state exceeds hypershader channel storage");
#else
_Static_assert(sizeof(gs_image_file_t) <= HS_CHANNEL_IMAGE_STORAGE_SIZE,
	       "legacy OBS image state exceeds hypershader channel storage");
#endif

static struct hs_image_file_view *image_view(hs_channel_image_t *image)
{
	return image && image->storage ? (struct hs_image_file_view *)image->storage : NULL;
}

static const struct hs_image_file_view *image_view_const(const hs_channel_image_t *image)
{
	return image && image->storage ? (const struct hs_image_file_view *)image->storage : NULL;
}

static void *lookup_symbol(void *module, const char *name)
{
#ifdef _WIN32
	return os_dlsym(module, name);
#else
	return dlsym(module, name);
#endif
}

bool hs_channel_image_runtime_init(void)
{
	if (image_api_ready)
		return true;

#ifdef _WIN32
	void *module = os_dlopen("obs");
#else
	void *module = dlopen(NULL, RTLD_LAZY);
#endif
	if (!module) {
		blog(LOG_WARNING, "[hypershader] Unable to inspect OBS image-file ABI; image channels disabled");
		return false;
	}

	image_api_ex = obs_get_version() >= MAKE_SEMANTIC_VERSION(33, 0, 0);
	if (image_api_ex) {
		image_init_ex = (hs_image_init_ex_fn)lookup_symbol(module, "gs_image_file_ex_init");
		image_free = (hs_image_free_fn)lookup_symbol(module, "gs_image_file_ex_free");
		image_init_texture = (hs_image_init_texture_fn)lookup_symbol(module, "gs_image_file_ex_init_texture");
        image_tick = (hs_image_tick_fn)lookup_symbol(module, "gs_image_file_ex_tick");
        image_update_texture = (hs_image_update_texture_fn)lookup_symbol(module, "gs_image_file_ex_update_texture");
	} else {
		image_init_legacy = (hs_image_init_legacy_fn)lookup_symbol(module, "gs_image_file_init");
		image_free = (hs_image_free_fn)lookup_symbol(module, "gs_image_file_free");
		image_init_texture = (hs_image_init_texture_fn)lookup_symbol(module, "gs_image_file_init_texture");
        image_tick = (hs_image_tick_fn)lookup_symbol(module, "gs_image_file_tick");
        image_update_texture = (hs_image_update_texture_fn)lookup_symbol(module, "gs_image_file_update_texture");
	}

	image_api_ready = image_free && image_init_texture &&
			  (image_api_ex ? image_init_ex != NULL : image_init_legacy != NULL);
	if (!image_api_ready)
		blog(LOG_WARNING, "[hypershader] OBS image-file ABI symbols unavailable; image channels disabled");

#ifdef _WIN32
	os_dlclose(module);
#else
	dlclose(module);
#endif
	return image_api_ready;
}

bool hs_channel_image_loaded(const hs_channel_image_t *image)
{
	const struct hs_image_file_view *view = image_view_const(image);
	return view && view->loaded;
}

gs_texture_t *hs_channel_image_texture(const hs_channel_image_t *image)
{
	const struct hs_image_file_view *view = image_view_const(image);
	return view ? view->texture : NULL;
}

void hs_channel_image_free(hs_channel_image_t *image)
{
	if (!image)
		return;

	if (!image->storage)
		return;

	if (image_api_ready)
		image_free(image->storage);
	bfree(image->storage);
	image->storage = NULL;
	image->playback_frame_ns = 0;
	image->playback_triggered = false;
}

bool hs_channel_image_load(hs_channel_image_t *image, const char *path)
{
	if (!image || !path || !*path || !image_api_ready)
		return false;

	hs_channel_image_free(image);
	image->storage = bzalloc(HS_CHANNEL_IMAGE_STORAGE_SIZE);
	if (!image->storage)
		return false;

	if (image_api_ex)
		image_init_ex(image->storage, path, GS_IMAGE_ALPHA_STRAIGHT);
	else
		image_init_legacy(image->storage, path);

	const struct hs_image_file_view *view = image_view_const(image);
	if (!view || !view->loaded) {
		hs_channel_image_free(image);
		return false;
	}

	image_init_texture(image->storage);
	if (!image_view_const(image)->texture) {
		hs_channel_image_free(image);
		return false;
	}
	return true;
}

bool hs_channel_image_animated(const hs_channel_image_t *image)
{
    const struct hs_image_file_view *view = image_view_const(image);
    return view && view->loaded && view->texture && view->is_animated_gif &&
           image_tick && image_update_texture;
}

bool hs_channel_image_advance(hs_channel_image_t *image, uint64_t elapsed_ns)
{
    if (!hs_channel_image_animated(image) || !elapsed_ns)
        return false;
    if (!image_tick(image->storage, elapsed_ns))
        return false;
    image_update_texture(image->storage);
    return true;
}

bool hs_channel_image_restart(hs_channel_image_t *image)
{
    if (!hs_channel_image_animated(image))
        return false;
    if (image_api_ex) {
        hs_image_ex_view *view = image->storage;
        view->cur_time = 0;
        view->cur_frame = 0;
        view->cur_loop = 0;
    } else {
#if LIBOBS_API_MAJOR_VER < 33
        gs_image_file_t *view = image->storage;
        view->cur_time = 0;
        view->cur_frame = 0;
        view->cur_loop = 0;
#else
        /* A newer SDK does not expose the legacy decoder structure. */
        return false;
#endif
    }
    image_update_texture(image->storage);
    return true;
}

/* One animation tick per OBS frame, even if the source is rendered into
 * several scenes or graph passes. Hidden time does not accumulate. */
void hs_channel_image_play(hs_channel_image_t *image, uint64_t frame_ns,
                          enum hs_image_playback mode, bool triggered, bool visible)
{
    if (!hs_channel_image_animated(image) || !frame_ns ||
        frame_ns == image->playback_frame_ns)
        return;

    uint64_t elapsed = 0;
    if (image->playback_frame_ns && frame_ns > image->playback_frame_ns) {
        elapsed = frame_ns - image->playback_frame_ns;
        if (elapsed > 250000000ULL)
            elapsed = 0;
    }
    image->playback_frame_ns = frame_ns;

    bool rising = triggered && !image->playback_triggered;
    image->playback_triggered = triggered;
    if (mode == HS_IMAGE_RESTART && rising) {
        hs_channel_image_restart(image);
        return; /* display the first frame on activation */
    }
    if (mode == HS_IMAGE_CONTINUOUS || visible)
        hs_channel_image_advance(image, elapsed);
}
