// Real libobs/OpenGL test of barycentric face-surface geometry. No inference mocks.
#include <obs.h>
#include <graphics/graphics.h>
#include "mediapipe/face_surface.h"
#include <array>
#include <cmath>
#include <cstdio>

static bool sample(gs_texture_t *texture, unsigned x, unsigned y, std::array<float, 4> &out)
{
	auto *stage = gs_stagesurface_create(HS_FACE_SURFACE_SIZE, HS_FACE_SURFACE_SIZE, GS_RGBA32F);
	if (!stage)
		return false;
	gs_stage_texture(stage, texture);
	uint8_t *pixels = nullptr;
	uint32_t pitch = 0;
	bool ok = gs_stagesurface_map(stage, &pixels, &pitch);
	if (ok) {
		const float *pixel = (const float *)(pixels + y * pitch + x * sizeof(float) * 4);
		for (unsigned i = 0; i < 4; i++)
			out[i] = pixel[i];
		gs_stagesurface_unmap(stage);
	}
	gs_stagesurface_destroy(stage);
	return ok;
}
int main(int argc, char **argv)
{
	if (argc != 2 || !obs_startup("en-US", nullptr, nullptr))
		return 2;
	obs_video_info video{};
	video.graphics_module = "libobs-opengl";
	video.fps_num = 30;
	video.fps_den = 1;
	video.base_width = 256;
	video.base_height = 256;
	video.output_width = 256;
	video.output_height = 256;
	video.output_format = VIDEO_FORMAT_RGBA;
	video.scale_type = OBS_SCALE_DISABLE;
	if (obs_reset_video(&video) != OBS_VIDEO_SUCCESS) {
		obs_shutdown();
		return 3;
	}
	bool pass = false;
	obs_enter_graphics();
	hs_face_surface *surface = hs_face_surface_create(argv[1]);
	if (surface) {
		std::array<float, 468 * 4> points{};
		for (size_t i = 0; i < 468; i++) {
			points[i * 4] = -1.0f;
			points[i * 4 + 1] = -1.0f;
			points[i * 4 + 3] = 0.0f;
		}
		// Topology triangle zero is [0,11,302]. All other vertices are invalid.
		points[0] = 0.25f;
		points[1] = 0.25f;
		points[3] = 1;
		points[11 * 4] = 0.75f;
		points[11 * 4 + 1] = 0.25f;
		points[11 * 4 + 3] = 1;
		points[302 * 4] = 0.50f;
		points[302 * 4 + 1] = 0.75f;
		points[302 * 4 + 3] = 1;
		const bool drawn = hs_face_surface_render(surface, points.data(), 468);
		std::array<float, 4> center{}, outside{}, cleared{};
		const bool first = drawn && sample(hs_face_surface_texture(surface), 128, 100, center) &&
				   sample(hs_face_surface_texture(surface), 5, 5, outside);
		auto *indices = hs_face_surface_triangles(surface);
		// Check the uploaded triangle lookup using real libobs readback.
		bool indicesValid = false;
		if (indices) {
			auto *stage = gs_stagesurface_create(HS_FACE_TRIANGLE_COUNT, 1, GS_RGBA32F);
			if (stage) {
				gs_stage_texture(stage, indices);
				uint8_t *pixels = nullptr;
				uint32_t pitch = 0;
				if (gs_stagesurface_map(stage, &pixels, &pitch)) {
					const float *tri = (const float *)pixels;
					indicesValid = (tri[0] == 0 && tri[1] == 11 && tri[2] == 302);
					gs_stagesurface_unmap(stage);
				}
				gs_stagesurface_destroy(stage);
			}
		}
		const bool lost = hs_face_surface_render(surface, nullptr, 0);
		const bool second = lost && sample(hs_face_surface_texture(surface), 128, 100, cleared);
		pass = first && second && indicesValid && center[3] > 0.95f && center[0] > 0 && center[0] < 1 &&
		       center[1] >= 0 && center[2] >= 0 && center[1] + center[2] <= 1.02f && outside[3] < 0.05f &&
		       cleared[3] < 0.05f;
		printf("FACE_SURFACE_GPU drawn=%d index=%d center=[%.3f %.3f %.3f %.3f] outside_alpha=%.3f lost=%d cleared_alpha=%.3f PASS=%d\n",
		       int(drawn), int(indicesValid), center[0], center[1], center[2], center[3], outside[3], int(lost),
		       cleared[3], int(pass));
		hs_face_surface_destroy(surface);
	}
	obs_leave_graphics();
	obs_shutdown();
	return pass ? 0 : 1;
}
