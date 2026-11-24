#include <obs.h>
#include <graphics/graphics.h>
#include <iostream>
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (!obs_startup("en-US", nullptr, nullptr)) return 1;
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
    if (obs_reset_video(&video) != OBS_VIDEO_SUCCESS) { obs_shutdown(); return 1; }
    char *error = nullptr;
    obs_enter_graphics();
    gs_effect_t *effect = gs_effect_create_from_file(argv[1], &error);
    bool valid = effect && gs_effect_get_technique(effect, "Draw");
    if (effect) gs_effect_destroy(effect);
    obs_leave_graphics();
    std::cout << (valid ? "PASS" : "FAIL") << ": native OBS effect compilation "
              << (error ? error : "") << "\n";
    if (error) bfree(error);
    obs_shutdown();
    return valid ? 0 : 1;
}
