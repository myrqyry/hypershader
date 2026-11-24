// Isolated libobs/plugin test: report actual native loader outcomes for third-party shaders.
#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/bmem.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <sstream>
#include <string>
#include <algorithm>
#include <vector>
namespace fs = std::filesystem;
using namespace std::chrono_literals;
static std::string escape(const std::string &input)
{
	std::string out;
	for (unsigned char c : input) {
		if (c == '\\')
			out += "\\\\";
		else if (c == '\t')
			out += "\\t";
		else if (c == '\n' || c == '\r')
			out += ' ';
		else
			out += c;
	}
	return out;
}
int main(int argc, char **argv)
{
	if (argc != 5) {
		std::cerr << "usage: MODULE DATA DIR OUTPUT\n";
		return 2;
	}
	if (!obs_startup("en-US", nullptr, nullptr))
		return 3;
	obs_video_info video{};
	video.graphics_module = "libobs-opengl";
	video.fps_num = 30;
	video.fps_den = 1;
	video.base_width = 128;
	video.base_height = 128;
	video.output_width = 128;
	video.output_height = 128;
	video.output_format = VIDEO_FORMAT_RGBA;
	video.scale_type = OBS_SCALE_DISABLE;
	if (obs_reset_video(&video) != OBS_VIDEO_SUCCESS)
		return 3;
	obs_module_t *module = nullptr;
	if (obs_open_module(&module, argv[1], argv[2]) != MODULE_SUCCESS || !obs_init_module(module))
		return 3;
	char *config = obs_module_get_config_path(module, "builder-applied");
	if (!config)
		return 3;
	fs::path directory(config);
	bfree(config);
	fs::create_directories(directory);
	std::vector<fs::path> input;
	for (const auto &e : fs::directory_iterator(argv[3]))
		if (e.is_regular_file() && e.path().extension() == ".effect")
			input.push_back(e.path());
	std::sort(input.begin(), input.end());
	std::ofstream results(argv[4]);
	results << "file\tresult\tdetail\n";
	auto *source = obs_source_create_private("hypershader_filter", "Corpus compile probe", nullptr);
	if (!source)
		return 3;
	size_t success = 0;
	for (size_t n = 0; n < input.size(); ++n) {
		char id[96];
		snprintf(id, sizeof(id), "%08x-0000-0000-0000-%012zx.effect", (unsigned)(n + 1), n + 1);
		const std::string path = "managed/builder-applied/" + std::string(id);
		fs::copy_file(input[n], directory / id, fs::copy_options::overwrite_existing);
		auto *privateData = obs_source_get_private_settings(source);
		obs_data_set_string(privateData, "hypershader_builder_compile_path", path.c_str());
		obs_data_set_string(privateData, "hypershader_builder_compile_state", "pending");
		obs_data_set_string(privateData, "hypershader_builder_compile_error", "");
		obs_data_release(privateData);
		auto *settings = obs_source_get_settings(source);
		obs_data_set_int(settings, "preset", 1);
		obs_data_set_int(settings, "preset_schema", 2);
		obs_data_set_string(settings, "shader_file", path.c_str());
		obs_source_update(source, settings);
		obs_data_release(settings);
		std::string status = "timeout", detail;
		for (int i = 0; i < 80; ++i) {
			std::this_thread::sleep_for(35ms);
			privateData = obs_source_get_private_settings(source);
			status = obs_data_get_string(privateData, "hypershader_builder_compile_state");
			detail = obs_data_get_string(privateData, "hypershader_builder_compile_error");
			std::string actual = obs_data_get_string(privateData, "hypershader_builder_compile_path");
			obs_data_release(privateData);
			if (actual == path && (status == "succeeded" || status == "failed"))
				break;
			status = "timeout";
		}
		results << input[n].filename().string() << '\t' << status << '\t' << escape(detail) << '\n';
		results.flush();
		if (status == "succeeded")
			++success;
		std::cout << input[n].filename().string() << ' ' << status << ' ' << escape(detail).substr(0, 145)
			  << '\n';
	}
	obs_source_release(source);
	obs_shutdown();
	std::cout << "TOTAL=" << input.size() << " NATIVE_COMPILED=" << success << '\n';
	return 0;
}
