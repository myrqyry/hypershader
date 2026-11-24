// Real libobs shader property test; native shader compilation, no Qt stubs.
#include <obs.h>
#include <graphics/graphics.h>
#include <functional>
#include <thread>
#include <chrono>
#include <string>
#include <cstdio>
using namespace std::chrono_literals;
struct Fixture {
	gs_texture_t *tex = nullptr;
};
const char *name(void *)
{
	return "Conditional UI fixture";
}
uint32_t w(void *)
{
	return 64;
}
uint32_t h(void *)
{
	return 64;
}
void *create(obs_data_t *, obs_source_t *)
{
	auto *f = new Fixture;
	uint8_t pixels[64 * 64 * 4] = {};
	for (int i = 3; i < 64 * 64 * 4; i += 4)
		pixels[i] = 255;
	const uint8_t *p = pixels;
	obs_enter_graphics();
	f->tex = gs_texture_create(64, 64, GS_RGBA, 1, &p, GS_DYNAMIC);
	obs_leave_graphics();
	return f;
}
void destroy(void *data)
{
	auto *f = (Fixture *)data;
	obs_enter_graphics();
	if (f->tex)
		gs_texture_destroy(f->tex);
	obs_leave_graphics();
	delete f;
}
void render(void *data, gs_effect_t *)
{
	auto *f = (Fixture *)data;
	if (f->tex)
		obs_source_draw(f->tex, 0, 0, 64, 64, false);
}
obs_source_info source_info{.id = "hs_conditional_test",
			    .type = OBS_SOURCE_TYPE_INPUT,
			    .output_flags = OBS_SOURCE_VIDEO,
			    .get_name = name,
			    .create = create,
			    .destroy = destroy,
			    .get_width = w,
			    .get_height = h,
			    .video_render = render};
void tick(obs_source_t *source)
{
	obs_enter_graphics();
	auto *target = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	if (target && gs_texrender_begin(target, 64, 64)) {
		gs_projection_push();
		gs_matrix_push();
		gs_matrix_identity();
		gs_ortho(0, 64, 0, 64, -100, 100);
		obs_source_video_render(source);
		gs_matrix_pop();
		gs_projection_pop();
		gs_texrender_end(target);
	}
	if (target)
		gs_texrender_destroy(target);
	obs_leave_graphics();
}
int main(int argc, char **argv)
{
	if (argc != 4 || !obs_startup("en-US", nullptr, nullptr))
		return 2;
	obs_video_info video{};
	video.graphics_module = "libobs-opengl";
	video.fps_num = 30;
	video.fps_den = 1;
	video.base_width = 64;
	video.base_height = 64;
	video.output_width = 64;
	video.output_height = 64;
	video.output_format = VIDEO_FORMAT_RGBA;
	video.scale_type = OBS_SCALE_DISABLE;
	if (obs_reset_video(&video) != OBS_VIDEO_SUCCESS) {
		obs_shutdown();
		return 3;
	}
	obs_register_source(&source_info);
	obs_module_t *module = nullptr;
	if (obs_open_module(&module, argv[1], argv[2]) != MODULE_SUCCESS || !obs_init_module(module)) {
		obs_shutdown();
		return 4;
	}
	auto *source = obs_source_create("hs_conditional_test", "Test video", nullptr, nullptr);
	auto *settings = obs_data_create();
	obs_data_set_string(settings, "shader_file", argv[3]);
	auto *filter = obs_source_create("hypershader_filter", "Controls", settings, nullptr);
	obs_data_release(settings);
	if (!source || !filter) {
		obs_shutdown();
		return 5;
	}
	obs_source_filter_add(source, filter);
	obs_source_inc_showing(source);
	for (int i = 0; i < 85; i++) {
		tick(source);
		std::this_thread::sleep_for(20ms);
	}
	auto *properties = obs_source_properties(filter);
	std::function<obs_property_t *(obs_properties_t *, const char *)> find =
		[&](obs_properties_t *root, const char *key) -> obs_property_t * {
		if (!root)
			return nullptr;
		if (auto *p = obs_properties_get(root, key))
			return p;
		for (auto *it = obs_properties_first(root); it; obs_property_next(&it))
			if (obs_property_get_type(it) == OBS_PROPERTY_GROUP)
				if (auto *child = find(obs_property_group_content(it), key))
					return child;
		return nullptr;
	};
	auto *reveal = find(properties, "param_reveal");
	auto *detail = find(properties, "param_detail");
	auto *mode = find(properties, "param_mode");
	auto *advanced = find(properties, "param_advanced");
	auto *missing = find(properties, "param_missing");
	auto *always = find(properties, "param_always");
	bool present = reveal && detail && mode && advanced && missing && always;
	bool initial = present && !obs_property_visible(detail) && obs_property_visible(advanced) &&
		       !obs_property_visible(missing) && obs_property_visible(always);
	bool toggled = false;
	if (present) {
		auto *data = obs_source_get_settings(filter);
		obs_data_set_bool(data, "param_reveal", true);
		bool update1 = obs_property_modified(reveal, data);
		bool after1 = obs_property_visible(detail);
		obs_data_set_int(data, "param_mode", 0);
		bool update2 = obs_property_modified(mode, data);
		bool after2 = !obs_property_visible(advanced);
		obs_data_set_bool(data, "param_reveal", false);
		bool update3 = obs_property_modified(reveal, data);
		bool after3 = !obs_property_visible(detail);
		toggled = update1 && after1 && update2 && after2 && update3 && after3 && !obs_property_visible(missing);
		obs_data_release(data);
	}
	bool pass = present && initial && toggled;
	printf("CONDITIONAL_UI real=%d initial=%d toggled=%d PASS=%d\n", int(present), int(initial), int(toggled),
	       int(pass));
	if (properties)
		obs_properties_destroy(properties);
	obs_source_filter_remove(source, filter);
	obs_source_release(filter);
	obs_source_dec_showing(source);
	obs_source_release(source);
	obs_shutdown();
	return pass ? 0 : 1;
}
