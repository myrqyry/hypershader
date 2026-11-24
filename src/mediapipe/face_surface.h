#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <graphics/graphics.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HS_FACE_SURFACE_SIZE 256
#define HS_FACE_TRIANGLE_COUNT 898

/* All functions require an OBS graphics context. Owned per filter instance. */
typedef struct hs_face_surface hs_face_surface;

hs_face_surface *hs_face_surface_create(const char *effect_path);
void hs_face_surface_destroy(hs_face_surface *surface);

/* Landmarks are count RGBA32F records (x, y, z, visibility).
 * Draws 468-landmark triangles into a GPU barycentric surface. NULL clears
 * the surface on detection loss. This is never run on the inference thread. */
bool hs_face_surface_render(hs_face_surface *surface, const float *landmarks, size_t count);

gs_texture_t *hs_face_surface_texture(const hs_face_surface *surface);
/* Static per-triangle lookup: (landmark_id_0, _1, _2, 0). */
gs_texture_t *hs_face_surface_triangles(const hs_face_surface *surface);

#ifdef __cplusplus
}
#endif
