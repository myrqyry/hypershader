// Real libobs/plugin completion test: isolated source and managed revisions.
#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/bmem.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
using namespace std::chrono_literals;
namespace fs = std::filesystem;
static bool check(obs_source_t *source, const std::string &path, const char *expected, bool errorExpected) {
    for (int i = 0; i < 160; ++i) {
        obs_data_t *privateData = obs_source_get_private_settings(source);
        const std::string state = obs_data_get_string(privateData, "hypershader_builder_compile_state");
        const std::string actualPath = obs_data_get_string(privateData, "hypershader_builder_compile_path");
        const std::string diagnostics = obs_data_get_string(privateData, "hypershader_builder_compile_error");
        obs_data_release(privateData);
        if (actualPath == path && state == expected) {
            const bool passed = errorExpected
                ? diagnostics.find(".effect (") != std::string::npos // native parser diagnostic, not path error
                : diagnostics.empty();
            std::cout << "COMPILE_FEEDBACK state=" << state << " path=" << actualPath
                      << " error=" << diagnostics.substr(0, 120) << " PASS=" << passed << "\n";
            return passed;
        }
        std::this_thread::sleep_for(40ms);
    }
    std::cerr << "Timed out waiting for runtime compilation of " << path << "\n";
    return false;
}
static void apply(obs_source_t *source, const std::string &path, const std::string &expectedPath = "") {
    obs_data_t *privateData = obs_source_get_private_settings(source);
    obs_data_set_string(privateData, "hypershader_builder_compile_path",
                        expectedPath.empty() ? path.c_str() : expectedPath.c_str());
    obs_data_set_string(privateData, "hypershader_builder_compile_state", "pending");
    obs_data_set_string(privateData, "hypershader_builder_compile_error", "");
    obs_data_release(privateData);
    obs_data_t *settings = obs_source_get_settings(source);
    obs_data_set_string(settings, "shader_file", path.c_str());
    obs_data_set_int(settings, "preset", 1);
    obs_data_set_int(settings, "preset_schema", 2);
    obs_source_update(source, settings);
    obs_data_release(settings);
}
int main(int argc, char **argv) {
    if (argc != 4) return 2;
    if (!obs_startup("en-US", nullptr, nullptr)) return 2;
    obs_video_info video{};
    video.graphics_module = "libobs-opengl";
    video.fps_num = 30; video.fps_den = 1;
    video.base_width = 128; video.base_height = 128;
    video.output_width = 128; video.output_height = 128;
    video.output_format = VIDEO_FORMAT_RGBA;
    video.scale_type = OBS_SCALE_DISABLE;
    if (obs_reset_video(&video) != OBS_VIDEO_SUCCESS) return 2;
    obs_module_t *module = nullptr;
    if (obs_open_module(&module, argv[1], argv[2]) != MODULE_SUCCESS || !obs_init_module(module)) return 2;
    char *folder = obs_module_get_config_path(module, "builder-applied");
    if (!folder) return 2;
    fs::path dir(folder);
    bfree(folder);
    fs::create_directories(dir);
    fs::copy_file(argv[3], dir / "11111111-1111-1111-1111-111111111111.effect", fs::copy_options::overwrite_existing);
    std::ofstream(dir / "22222222-2222-2222-2222-222222222222.effect") << "this is not a valid OBS effect;\n";
    auto *source = obs_source_create_private("hypershader_filter", "Builder feedback test", nullptr);
    if (!source) return 2;
    const std::string invalid = "managed/builder-applied/22222222-2222-2222-2222-222222222222.effect";
    const std::string valid = "managed/builder-applied/11111111-1111-1111-1111-111111111111.effect";
    apply(source, invalid);
    bool ok = check(source, invalid, "failed", true);
    apply(source, valid);
    ok = check(source, valid, "succeeded", false) && ok;
    // A late/out-of-order compile must not mark a different revision complete.
    fs::copy_file(argv[3], dir / "33333333-3333-3333-3333-333333333333.effect",
                  fs::copy_options::overwrite_existing);
    const std::string current = "managed/builder-applied/33333333-3333-3333-3333-333333333333.effect";
    const std::string expected = "managed/builder-applied/44444444-4444-4444-4444-444444444444.effect";
    apply(source, current, expected);
    std::this_thread::sleep_for(950ms);
    obs_data_t *privateData = obs_source_get_private_settings(source);
    const bool staleSafe = std::string(obs_data_get_string(privateData, "hypershader_builder_compile_state")) == "pending" &&
                           std::string(obs_data_get_string(privateData, "hypershader_builder_compile_path")) == expected &&
                           std::string(obs_data_get_string(privateData, "hypershader_builder_compile_error")).empty();
    obs_data_release(privateData);
    std::cout << "COMPILE_FEEDBACK_STALE_ISOLATED_PASS=" << staleSafe << "\n";
    ok &= staleSafe;
    obs_source_release(source);
    obs_shutdown();
    std::cout << "BUILDER_NATIVE_COMPILE_FEEDBACK_PASS=" << ok << "\n";
    return ok ? 0 : 1;
}
