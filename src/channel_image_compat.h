#pragma once

#include <obs-module.h>
#include <graphics/image-file.h>

#include <stdbool.h>
#include <stddef.h>

#define HS_CHANNEL_IMAGE_STORAGE_SIZE 65536

typedef struct hs_channel_image {
	void *storage;
	uint64_t playback_frame_ns;
	bool playback_triggered;
} hs_channel_image_t;

bool hs_channel_image_runtime_init(void);
bool hs_channel_image_loaded(const hs_channel_image_t *image);
gs_texture_t *hs_channel_image_texture(const hs_channel_image_t *image);
bool hs_channel_image_load(hs_channel_image_t *image, const char *path);
void hs_channel_image_free(hs_channel_image_t *image);

/* Caller owns the graphics context. Static images are harmless no-ops. */
bool hs_channel_image_animated(const hs_channel_image_t *image);
bool hs_channel_image_advance(hs_channel_image_t *image, uint64_t elapsed_ns);
bool hs_channel_image_restart(hs_channel_image_t *image);

enum hs_image_playback {
    HS_IMAGE_CONTINUOUS = 0,
    HS_IMAGE_RESTART = 1,
    HS_IMAGE_RESUME = 2,
};
void hs_channel_image_play(hs_channel_image_t *image, uint64_t frame_ns,
                          enum hs_image_playback mode, bool triggered, bool visible);
