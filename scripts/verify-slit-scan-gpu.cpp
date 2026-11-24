// Real libobs/OpenGL regression for output-feedback Slit Scan Tape.
// Tests time-axis history, held/skipped frames, erase, reverse, horizontal scans.
#include <obs.h>
#include <graphics/graphics.h>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

constexpr unsigned W = 64, H = 32;
using Frame = std::vector<uint8_t>;

Frame solid(uint8_t r, uint8_t g, uint8_t b)
{
	Frame frame(W * H * 4);
	for (unsigned i = 0; i < W * H; ++i) {
		frame[i * 4] = r;
		frame[i * 4 + 1] = g;
		frame[i * 4 + 2] = b;
		frame[i * 4 + 3] = 255;
	}
	return frame;
}

bool set_float(gs_effect_t *effect, const char *name, float value)
{
	auto *p = gs_effect_get_param_by_name(effect, name);
	if (!p)
		return false;
	gs_effect_set_float(p, value);
	return true;
}

Frame record(gs_effect_t *effect, gs_texrender_t *target, gs_texture_t *incoming, gs_texture_t *previous, int frame,
	     bool hold = false, bool erase = false, bool reverse = false, bool horizontal = false,
	     float frame_advance = 1.f, float position = .25f)
{
	Frame out;
	auto *ip = gs_effect_get_param_by_name(effect, "image");
	auto *pp = gs_effect_get_param_by_name(effect, "iPreviousFrame");
	auto *rp = gs_effect_get_param_by_name(effect, "iResolution");
	auto *fp = gs_effect_get_param_by_name(effect, "iFrame");
	if (!ip || !pp || !rp || !fp)
		return out;
	gs_effect_set_texture(ip, incoming);
	gs_effect_set_texture(pp, previous);
	vec3 resolution{};
	vec3_set(&resolution, float(W), float(H), float(W) / float(H));
	gs_effect_set_vec3(rp, &resolution);
	gs_effect_set_int(fp, frame);
	if (!set_float(effect, "scan_width", 8.0f) || !set_float(effect, "scan_position", position) ||
	    !set_float(effect, "horizontal_scan", horizontal ? 1.f : 0.f) ||
	    !set_float(effect, "frames_per_advance", frame_advance) ||
	    !set_float(effect, "hold_tape", hold ? 1.0f : 0.0f) ||
	    !set_float(effect, "erase_tape", erase ? 1.0f : 0.0f) ||
	    !set_float(effect, "reverse_scroll", reverse ? 1.0f : 0.0f))
		return out;

	gs_set_cull_mode(GS_NEITHER);
	gs_texrender_reset(target);
	if (!gs_texrender_begin(target, W, H))
		return out;
	gs_projection_push();
	gs_matrix_push();
	gs_matrix_identity();
	gs_ortho(0, float(W), 0, float(H), -100, 100);
	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(incoming, 0, W, H);
	gs_matrix_pop();
	gs_projection_pop();
	gs_texrender_end(target);

	auto *stage = gs_stagesurface_create(W, H, GS_RGBA);
	auto *result = gs_texrender_get_texture(target);
	if (stage && result) {
		gs_stage_texture(stage, result);
		uint8_t *ptr = nullptr;
		uint32_t stride = 0;
		if (gs_stagesurface_map(stage, &ptr, &stride)) {
			out.resize(W * H * 4);
			for (unsigned y = 0; y < H; ++y)
				std::memcpy(out.data() + y * W * 4, ptr + y * stride, W * 4);
			gs_stagesurface_unmap(stage);
		}
	}
	if (stage)
		gs_stagesurface_destroy(stage);
	return out;
}

bool pixel(const Frame &f, unsigned x, unsigned y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	if (f.size() != W * H * 4 || x >= W || y >= H)
		return false;
	auto i = 4 * (y * W + x);
	const uint8_t target[] = {r, g, b, a};
	for (int c = 0; c < 4; ++c)
		if (std::abs(int(f[i + c]) - int(target[c])) > 5)
			return false;
	return true;
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	if (!obs_startup("en-US", nullptr, nullptr))
		return 2;
	obs_video_info v{};
	v.graphics_module = "libobs-opengl";
	v.fps_num = 30;
	v.fps_den = 1;
	v.base_width = W;
	v.base_height = H;
	v.output_width = W;
	v.output_height = H;
	v.output_format = VIDEO_FORMAT_RGBA;
	v.scale_type = OBS_SCALE_DISABLE;
	if (obs_reset_video(&v) != OBS_VIDEO_SUCCESS) {
		obs_shutdown();
		return 2;
	}
	bool pass = false;
	obs_enter_graphics();
	char *errors = nullptr;
	auto *fx = gs_effect_create_from_file(argv[1], &errors);
	if (!fx)
		std::fprintf(stderr, "SLIT_SCAN_COMPILE_FAILED: %s\n", errors ? errors : "(no log)");
	else {
		auto *a = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		auto *b = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		Frame transparent(W * H * 4, 0);
		const uint8_t *blank_bytes = transparent.data();
		auto *blank = gs_texture_create(W, H, GS_RGBA, 1, &blank_bytes, 0);
		auto red = solid(220, 25, 30), green = solid(25, 220, 30);
		auto blue = solid(25, 30, 220), yellow = solid(220, 220, 25);
		const uint8_t *data = red.data();
		auto *input = gs_texture_create(W, H, GS_RGBA, 1, &data, GS_DYNAMIC);

		auto f0 = record(fx, a, input, blank, 0);
		gs_texture_set_image(input, green.data(), W * 4, false);
		auto f1 = record(fx, b, input, gs_texrender_get_texture(a), 1);
		gs_texture_set_image(input, blue.data(), W * 4, false);
		auto f2 = record(fx, a, input, gs_texrender_get_texture(b), 2);
		auto f3 = record(fx, b, input, gs_texrender_get_texture(a), 3, true);
		auto f4 = record(fx, a, input, gs_texrender_get_texture(b), 4, false, true);
		gs_texture_set_image(input, yellow.data(), W * 4, false);
		auto f5 = record(fx, b, input, gs_texrender_get_texture(a), 5, false, false, true);
		gs_texture_set_image(input, red.data(), W * 4, false);
		auto f6 = record(fx, a, input, gs_texrender_get_texture(b), 7, false, false, false, false, 2.f);
		gs_texture_set_image(input, green.data(), W * 4, false);
		auto f7 = record(fx, b, input, gs_texrender_get_texture(a), 8, false, false, false, false, 2.f);
		gs_texture_set_image(input, blue.data(), W * 4, false);
		auto h0 = record(fx, a, input, blank, 0, false, false, false, true);
		gs_texture_set_image(input, yellow.data(), W * 4, false);
		auto h1 = record(fx, b, input, gs_texrender_get_texture(a), 1, false, false, false, true);

		// Inspect the real incoming image at two different slit positions;
		// a constant-color source would not detect broken scan_position.
		Frame gradient(W * H * 4);
		for (unsigned y = 0; y < H; y++)
			for (unsigned x = 0; x < W; x++) {
				size_t i = (y * W + x) * 4;
				gradient[i] = uint8_t(3 * x);
				gradient[i + 1] = uint8_t(5 * y);
				gradient[i + 2] = 64;
				gradient[i + 3] = 255;
			}
		gs_texture_set_image(input, gradient.data(), W * 4, false);
		auto near = record(fx, a, input, blank, 0, false, false, false, false, 1.f, .15f);
		auto far = record(fx, b, input, blank, 0, false, false, false, false, 1.f, .85f);
		// A full-width slit near either border must sample adjacent source
		// pixels rather than repeatedly clamping to a single edge texel.
		auto leftEdge = record(fx, a, input, blank, 0, false, false, false, false, 1.f, 0.f);
		auto rightEdge = record(fx, b, input, blank, 0, false, false, false, false, 1.f, 1.f);
		auto topEdge = record(fx, a, input, blank, 0, false, false, false, true, 1.f, 0.f);
		auto bottomEdge = record(fx, b, input, blank, 0, false, false, false, true, 1.f, 1.f);
		size_t testPixel = 4 * (16 * W + 60);
		bool scanPosition = near.size() == gradient.size() && far.size() == gradient.size() &&
				    near[testPixel] < 50 && far[testPixel] > 140 && pixel(near, 20, 16, 0, 0, 0, 0) &&
				    pixel(far, 20, 16, 0, 0, 0, 0);

		auto redAt = [](const Frame &f, unsigned x, unsigned y) -> int {
			if (f.size() != W * H * 4)
				return -1;
			return int(f[4 * (y * W + x)]);
		};
		auto greenAt = [](const Frame &f, unsigned x, unsigned y) -> int {
			if (f.size() != W * H * 4)
				return -1;
			return int(f[4 * (y * W + x) + 1]);
		};
		bool scanEdges = redAt(leftEdge, 57, 16) > redAt(leftEdge, 56, 16) &&
				 redAt(rightEdge, 63, 16) > redAt(rightEdge, 62, 16) &&
				 greenAt(topEdge, 32, 25) > greenAt(topEdge, 32, 24) &&
				 greenAt(bottomEdge, 32, 31) > greenAt(bottomEdge, 32, 30);

		bool recorded = pixel(f0, 60, 16, 220, 25, 30, 255) && pixel(f1, 60, 16, 25, 220, 30, 255) &&
				pixel(f2, 60, 16, 25, 30, 220, 255);
		bool history = pixel(f0, 4, 16, 0, 0, 0, 0) && pixel(f1, 52, 16, 220, 25, 30, 255) &&
			       pixel(f1, 40, 16, 0, 0, 0, 0) && pixel(f2, 44, 16, 220, 25, 30, 255) &&
			       pixel(f2, 52, 16, 25, 220, 30, 255) && pixel(f2, 32, 16, 0, 0, 0, 0);
		bool held = pixel(f3, 44, 16, 220, 25, 30, 255) && pixel(f3, 52, 16, 25, 220, 30, 255) &&
			    pixel(f3, 60, 16, 25, 30, 220, 255);
		bool erased = pixel(f4, 44, 16, 0, 0, 0, 0) && pixel(f4, 60, 16, 0, 0, 0, 0);
		bool reversed = pixel(f5, 4, 16, 220, 220, 25, 255) && pixel(f5, 60, 16, 0, 0, 0, 0);
		bool paced = pixel(f6, 4, 16, 220, 220, 25, 255) && pixel(f6, 60, 16, 0, 0, 0, 0) &&
			     pixel(f7, 60, 16, 25, 220, 30, 255);
		bool horizontal = pixel(h0, 32, 28, 25, 30, 220, 255) && pixel(h0, 32, 16, 0, 0, 0, 0) &&
				  pixel(h1, 32, 20, 25, 30, 220, 255) && pixel(h1, 32, 28, 220, 220, 25, 255);
		pass = recorded && history && held && erased && reversed && paced && horizontal && scanPosition &&
		       scanEdges;
		std::printf(
			"SLIT_SCAN records=%d history=%d hold=%d erase=%d reverse=%d pace=%d horizontal=%d scan_position=%d scan_edges=%d\n",
			recorded, history, held, erased, reversed, paced, horizontal, scanPosition, scanEdges);
		std::printf("SLIT_SCAN_REAL_GPU_PASS=%d\n", pass);
		gs_texture_destroy(input);
		gs_texture_destroy(blank);
		gs_texrender_destroy(a);
		gs_texrender_destroy(b);
		gs_effect_destroy(fx);
	}
	if (errors)
		bfree(errors);
	obs_leave_graphics();
	obs_shutdown();
	return pass ? 0 : 1;
}
