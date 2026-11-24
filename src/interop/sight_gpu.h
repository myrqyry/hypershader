#pragma once
#include <obs.h>
#include <graphics/graphics.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

/* Call from inside an OBS graphics context. The caller owns *texture.
 * A zero-sized input clears the resource so tracking loss cannot retain
 * pixels from a previous snapshot. */
static inline bool hs_sight_upload_rgba32f(gs_texture_t **texture, uint32_t width,
                                           uint32_t height, const float *rgba)
{
    if (!texture) return false;
    if (!rgba || !width || !height) {
        if (*texture) {
            gs_texture_destroy(*texture);
            *texture = NULL;
        }
        return true;
    }
    if (width > 2048 || height > 4 || width > UINT32_MAX / (height * 16))
        return false;
    if (*texture && (gs_texture_get_width(*texture) != width ||
                     gs_texture_get_height(*texture) != height)) {
        gs_texture_destroy(*texture);
        *texture = NULL;
    }
    if (!*texture)
        *texture = gs_texture_create(width, height, GS_RGBA32F, 1, NULL, GS_DYNAMIC);
    if (!*texture) return false;
    gs_texture_set_image(*texture, (const uint8_t*)rgba, width * 16, false);
    return true;
}
