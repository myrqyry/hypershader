#include "face_surface.h"
#include "face_surface_triangles.h"

#include <obs-module.h>
#include <graphics/vec3.h>
#include <util/bmem.h>
#include <math.h>
#include <string.h>

/* The topology comes from Shadertastic (GPL-2.0-or-later), with no OpenCV
 * dependency. All drawing occurs on OBS's graphics thread. */
struct hs_face_surface {
	gs_texrender_t *render;
	gs_texture_t *triangles;
	gs_vertbuffer_t *vertices;
	gs_effect_t *effect;
};

static struct gs_vb_data *face_vertex_data(void)
{
	const size_t capacity = HS_FACE_TRIANGLE_COUNT * 3;
	struct gs_vb_data *data = gs_vbdata_create();
	if (!data)
		return NULL;
	data->num = capacity;
	data->points = bzalloc(capacity * sizeof(*data->points));
	data->num_tex = 2;
	data->tvarray = bzalloc(2 * sizeof(*data->tvarray));
	if (!data->points || !data->tvarray)
		goto error;
	data->tvarray[0].width = 2;
	data->tvarray[1].width = 1;
	data->tvarray[0].array = bzalloc(capacity * 2 * sizeof(float));
	data->tvarray[1].array = bzalloc(capacity * sizeof(float));
	if (!data->tvarray[0].array || !data->tvarray[1].array)
		goto error;
	return data;
error:
	gs_vbdata_destroy(data);
	return NULL;
}

hs_face_surface *hs_face_surface_create(const char *effect_path)
{
	if (!effect_path || !*effect_path)
		return NULL;
	hs_face_surface *surface = bzalloc(sizeof(*surface));
	if (!surface)
		return NULL;
	surface->render = gs_texrender_create(GS_RGBA32F, GS_ZS_NONE);
	surface->effect = gs_effect_create_from_file(effect_path, NULL);
	struct gs_vb_data *data = face_vertex_data();
	if (data)
		surface->vertices = gs_vertexbuffer_create(data, GS_DYNAMIC);
	if (!surface->vertices && data)
		gs_vbdata_destroy(data);
	surface->triangles = gs_texture_create(HS_FACE_TRIANGLE_COUNT, 1, GS_RGBA32F, 1, NULL, GS_DYNAMIC);
	if (!surface->render || !surface->effect || !surface->vertices || !surface->triangles) {
		hs_face_surface_destroy(surface);
		return NULL;
	}

	float *indices = bmalloc(HS_FACE_TRIANGLE_COUNT * 4 * sizeof(float));
	if (!indices) {
		hs_face_surface_destroy(surface);
		return NULL;
	}
	for (size_t i = 0; i < HS_FACE_TRIANGLE_COUNT; ++i) {
		indices[i * 4 + 0] = (float)hs_face_triangle_indices[i][0];
		indices[i * 4 + 1] = (float)hs_face_triangle_indices[i][1];
		indices[i * 4 + 2] = (float)hs_face_triangle_indices[i][2];
		indices[i * 4 + 3] = 0.0f;
	}
	gs_texture_set_image(surface->triangles, (const uint8_t *)indices, HS_FACE_TRIANGLE_COUNT * 16, false);
	bfree(indices);
	return surface;
}

void hs_face_surface_destroy(hs_face_surface *surface)
{
	if (!surface)
		return;
	if (surface->effect)
		gs_effect_destroy(surface->effect);
	if (surface->vertices)
		gs_vertexbuffer_destroy(surface->vertices);
	if (surface->triangles)
		gs_texture_destroy(surface->triangles);
	if (surface->render)
		gs_texrender_destroy(surface->render);
	bfree(surface);
}

bool hs_face_surface_render(hs_face_surface *surface, const float *landmarks, size_t count)
{
	if (!surface || !surface->render)
		return false;
	size_t vertex_count = 0;
	if (landmarks && count >= 468) {
		struct gs_vb_data *data = gs_vertexbuffer_get_data(surface->vertices);
		float *bary = data->tvarray[0].array;
		float *tri = data->tvarray[1].array;
		for (size_t i = 0; i < HS_FACE_TRIANGLE_COUNT; ++i) {
			const uint16_t *ids = hs_face_triangle_indices[i];
			bool valid = true;
			for (size_t j = 0; j < 3; ++j) {
				const float *v = landmarks + (size_t)ids[j] * 4;
				if (!isfinite(v[0]) || !isfinite(v[1]) || v[3] <= 0 || v[0] < 0 || v[0] > 1 ||
				    v[1] < 0 || v[1] > 1) {
					valid = false;
					break;
				}
			}
			if (!valid)
				continue;
			for (size_t j = 0; j < 3; ++j) {
				const float *v = landmarks + (size_t)ids[j] * 4;
				struct vec3 *p = &data->points[vertex_count];
				p->x = v[0];
				p->y = v[1];
				p->z = 0;
				bary[vertex_count * 2 + 0] = j == 0 ? 1.0f : 0.0f;
				bary[vertex_count * 2 + 1] = j == 1 ? 1.0f : 0.0f;
				tri[vertex_count] = ((float)i + 0.5f) / (float)HS_FACE_TRIANGLE_COUNT;
				vertex_count++;
			}
		}
		gs_vertexbuffer_flush(surface->vertices);
	}

	gs_texrender_reset(surface->render);
	if (!gs_texrender_begin(surface->render, HS_FACE_SURFACE_SIZE, HS_FACE_SURFACE_SIZE))
		return false;
	gs_projection_push();
	gs_matrix_push();
	gs_matrix_identity();
	/* MediaPipe coordinates are normalized in the video image, y increasing downward. */
	gs_ortho(0.0f, 1.0f, 1.0f, 0.0f, -100.0f, 100.0f);
	const struct vec4 clear = {0, 0, 0, 0};
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	if (vertex_count) {
		gs_blend_state_push();
		gs_enable_blending(false);
		enum gs_cull_mode previous_cull = gs_get_cull_mode();
		gs_set_cull_mode(GS_NEITHER);
		gs_load_vertexbuffer(surface->vertices);
		gs_load_indexbuffer(NULL);
		while (gs_effect_loop(surface->effect, "Draw"))
			gs_draw(GS_TRIS, 0, (uint32_t)vertex_count);
		gs_load_vertexbuffer(NULL);
		gs_set_cull_mode(previous_cull);
		gs_blend_state_pop();
	}
	gs_matrix_pop();
	gs_projection_pop();
	gs_texrender_end(surface->render);
	return true;
}

gs_texture_t *hs_face_surface_texture(const hs_face_surface *surface)
{
	return surface && surface->render ? gs_texrender_get_texture(surface->render) : NULL;
}

gs_texture_t *hs_face_surface_triangles(const hs_face_surface *surface)
{
	return surface ? surface->triangles : NULL;
}
