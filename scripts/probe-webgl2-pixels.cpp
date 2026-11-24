// Isolated render probe with a real plugin filter and real libobs GPU readback.
#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/bmem.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <iostream>
#include <vector>
#include <set>
#include <cmath>
#include <cstring>
using namespace std::chrono_literals;
namespace fs = std::filesystem;
constexpr uint32_t W = 160, H = 90;
struct TestSource {
	gs_texture_t *texture = nullptr;
};
const char *source_name(void *)
{
	return "Controlled video for import probe";
}
uint32_t source_w(void *)
{
	return W;
}
uint32_t source_h(void *)
{
	return H;
}
void *source_create(obs_data_t *, obs_source_t *)
{
	auto *s = new TestSource;
	std::vector<uint8_t> pixels(W * H * 4, 255);
	for (uint32_t y = 0; y < H; ++y)
		for (uint32_t x = 0; x < W; ++x) {
			size_t i = (y * W + x) * 4;
			pixels[i] = uint8_t(255 * x / W);
			pixels[i + 1] = uint8_t(255 * y / H);
			pixels[i + 2] = 65;
		}
	const uint8_t *data = pixels.data();
	obs_enter_graphics();
	s->texture = gs_texture_create(W, H, GS_RGBA, 1, &data, GS_DYNAMIC);
	obs_leave_graphics();
	return s;
}
void source_destroy(void *o)
{
	auto *s = (TestSource *)o;
	obs_enter_graphics();
	if (s->texture)
		gs_texture_destroy(s->texture);
	obs_leave_graphics();
	delete s;
}
void source_render(void *o, gs_effect_t *)
{
	auto *s = (TestSource *)o;
	if (s && s->texture)
		obs_source_draw(s->texture, 0, 0, W, H, false);
}
obs_source_info source_info{.id = "shader_corpus_test_image",
			    .type = OBS_SOURCE_TYPE_INPUT,
			    .output_flags = OBS_SOURCE_VIDEO,
			    .get_name = source_name,
			    .create = source_create,
			    .destroy = source_destroy,
			    .get_width = source_w,
			    .get_height = source_h,
			    .video_render = source_render};
struct Result {
	size_t distinct = 0;
	float mean = 0;
	float mean_difference = 0;
	size_t nonzero = 0;
};
Result render(obs_source_t *source)
{
	Result out;
	obs_enter_graphics();
	auto *target = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	auto *stage = gs_stagesurface_create(W, H, GS_RGBA);
	if (target && stage && gs_texrender_begin(target, W, H)) {
		gs_projection_push();
		gs_matrix_push();
		gs_matrix_identity();
		gs_ortho(0, float(W), 0, float(H), -100, 100);
		obs_source_video_render(source);
		gs_matrix_pop();
		gs_projection_pop();
		gs_texrender_end(target);
		if (auto *tex = gs_texrender_get_texture(target)) {
			gs_stage_texture(stage, tex);
			uint8_t *data = nullptr;
			uint32_t pitch = 0;
			if (gs_stagesurface_map(stage, &data, &pitch)) {
				uint64_t sum = 0, difference = 0;
				std::set<uint32_t> bins;
				for (uint32_t y = 0; y < H; y += 3)
					for (uint32_t x = 0; x < W; x += 3) {
						const uint8_t *pixel = data + y * pitch + x * 4;
						sum += unsigned(pixel[0]) + pixel[1] + pixel[2];
						difference += std::abs(int(pixel[0]) - int(255 * x / W)) +
							      std::abs(int(pixel[1]) - int(255 * y / H)) +
							      std::abs(int(pixel[2]) - 65);
						bins.insert((uint32_t(pixel[0] >> 4) << 8) |
							    (uint32_t(pixel[1] >> 4) << 4) | (uint32_t(pixel[2] >> 4)));
						if (pixel[0] || pixel[1] || pixel[2])
							out.nonzero++;
					}
				out.mean = float(sum) / float(((H + 2) / 3) * ((W + 2) / 3) * 3);
				out.mean_difference = float(difference) / float(((H + 2) / 3) * ((W + 2) / 3) * 3);
				out.distinct = bins.size();
				gs_stagesurface_unmap(stage);
			}
		}
	}
	if (stage)
		gs_stagesurface_destroy(stage);
	if (target)
		gs_texrender_destroy(target);
	obs_leave_graphics();
	return out;
}
int main(int argc, char **argv)
{
	if (argc != 4)
		return 2;
	if (!obs_startup("en-US", nullptr, nullptr))
		return 2;
	obs_video_info video{};
	video.graphics_module = "libobs-opengl";
	video.fps_num = 30;
	video.fps_den = 1;
	video.base_width = W;
	video.base_height = H;
	video.output_width = W;
	video.output_height = H;
	video.output_format = VIDEO_FORMAT_RGBA;
	video.scale_type = OBS_SCALE_DISABLE;
	if (obs_reset_video(&video) != OBS_VIDEO_SUCCESS)
		return 2;
	obs_register_source(&source_info);
	obs_module_t *module = nullptr;
	if (obs_open_module(&module, argv[1], argv[2]) != MODULE_SUCCESS || !obs_init_module(module))
		return 2;
	char *dirRaw = obs_module_get_config_path(module, "builder-applied");
	if (!dirRaw)
		return 2;
	fs::path dir(dirRaw);
	bfree(dirRaw);
	fs::create_directories(dir);
	bool ok = true;
	for (int n = 3; n < argc; ++n) {
		std::string managedName = "abcdef01-0000-0000-0000-000000000000.effect";
		fs::copy_file(argv[n], dir / managedName, fs::copy_options::overwrite_existing);
		std::string logical = "managed/builder-applied/" + managedName;
		auto *source = obs_source_create("shader_corpus_test_image", "Corpus frame", nullptr, nullptr);
		auto *filter = obs_source_create("hypershader_filter", "Corpus fixture", nullptr, nullptr);
		obs_source_filter_add(source, filter);
		obs_source_inc_showing(source);
		auto *privateData = obs_source_get_private_settings(filter);
		obs_data_set_string(privateData, "hypershader_builder_compile_path", logical.c_str());
		obs_data_set_string(privateData, "hypershader_builder_compile_state", "pending");
		obs_data_set_string(privateData, "hypershader_builder_compile_error", "");
		obs_data_release(privateData);
		auto *settings = obs_source_get_settings(filter);
		obs_data_set_int(settings, "preset", 1);
		obs_data_set_int(settings, "preset_schema", 2);
		obs_data_set_string(settings, "shader_file", logical.c_str());
		obs_source_update(filter, settings);
		obs_data_release(settings);
		std::string state = "timeout", error = "";
		for (int tries = 0; tries < 100; ++tries) {
			std::this_thread::sleep_for(40ms);
			auto *priv = obs_source_get_private_settings(filter);
			state = obs_data_get_string(priv, "hypershader_builder_compile_state");
			error = obs_data_get_string(priv, "hypershader_builder_compile_error");
			obs_data_release(priv);
			if (state == "succeeded" || state == "failed")
				break;
		}
		Result a = render(source);
		std::cout << "RENDER file=" << fs::path(argv[n]).filename() << " compile=" << state
			  << " bins=" << a.distinct << " mean=" << a.mean << " nonzero=" << a.nonzero
			  << " input_delta=" << a.mean_difference << "\n";
		ok &= state == "succeeded" && a.distinct >= 5 && a.nonzero > 0 && a.mean_difference > 2;
		obs_source_dec_showing(source);
		obs_source_filter_remove(source, filter);
		obs_source_release(filter);
		obs_source_release(source);
	}
	obs_shutdown();
	std::cout << "CORPUS_PIXEL_PASS=" << ok << "\n";
	return ok ? 0 : 1;
}
