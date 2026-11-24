// Real libobs/OpenGL regression for HyperShaderFX's output-feedback Warp Tape.
// Tests column history, hold, erase and reverse in a GPU ping-pong loop.
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

Frame solid(uint8_t r, uint8_t g, uint8_t b) {
    Frame frame(W * H * 4);
    for (unsigned i = 0; i < W * H; ++i) {
        frame[i * 4] = r;
        frame[i * 4 + 1] = g;
        frame[i * 4 + 2] = b;
        frame[i * 4 + 3] = 255;
    }
    return frame;
}

bool set_float(gs_effect_t *effect, const char *name, float value) {
    auto *p = gs_effect_get_param_by_name(effect, name);
    if (!p) return false;
    gs_effect_set_float(p, value);
    return true;
}

Frame record(gs_effect_t *effect, gs_texrender_t *target,
             gs_texture_t *incoming, gs_texture_t *previous, int frame,
             bool hold = false, bool erase = false, bool reverse = false) {
    Frame out;
    auto *ip = gs_effect_get_param_by_name(effect, "image");
    auto *pp = gs_effect_get_param_by_name(effect, "iPreviousFrame");
    auto *rp = gs_effect_get_param_by_name(effect, "iResolution");
    auto *fp = gs_effect_get_param_by_name(effect, "iFrame");
    if (!ip || !pp || !rp || !fp) return out;
    gs_effect_set_texture(ip, incoming);
    gs_effect_set_texture(pp, previous);
    vec3 resolution{};
    vec3_set(&resolution, float(W), float(H), float(W) / float(H));
    gs_effect_set_vec3(rp, &resolution);
    gs_effect_set_int(fp, frame);
    if (!set_float(effect, "columns_per_frame", 8.0f) ||
        !set_float(effect, "hold_tape", hold ? 1.0f : 0.0f) ||
        !set_float(effect, "erase_tape", erase ? 1.0f : 0.0f) ||
        !set_float(effect, "reverse_tape", reverse ? 1.0f : 0.0f) ||
        !set_float(effect, "tape_fade", 0.0f))
        return out;

    gs_set_cull_mode(GS_NEITHER);
    gs_texrender_reset(target);
    if (!gs_texrender_begin(target, W, H)) return out;
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
    if (stage) gs_stagesurface_destroy(stage);
    return out;
}

bool pixel(const Frame &f, unsigned x, uint8_t r, uint8_t g,
           uint8_t b, uint8_t a) {
    if (f.size() != W * H * 4) return false;
    auto i = 4 * (16 * W + x);
    const uint8_t target[] = {r, g, b, a};
    for (int c = 0; c < 4; ++c)
        if (std::abs(int(f[i + c]) - int(target[c])) > 5) return false;
    return true;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (!obs_startup("en-US", nullptr, nullptr)) return 2;
    obs_video_info v{};
    v.graphics_module = "libobs-opengl";
    v.fps_num = 30; v.fps_den = 1;
    v.base_width = W; v.base_height = H;
    v.output_width = W; v.output_height = H;
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
    if (!fx) {
        std::fprintf(stderr, "WARP_TAPE_COMPILE_FAILED: %s\n", errors ? errors : "(no log)");
    } else {
        auto *a = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
        auto *b = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
        Frame transparent(W * H * 4, 0);
        const uint8_t *blank_bytes = transparent.data();
        auto *blank = gs_texture_create(W, H, GS_RGBA, 1, &blank_bytes, 0);
        auto red = solid(220, 25, 30);
        auto green = solid(25, 220, 30);
        auto blue = solid(25, 30, 220);
        auto yellow = solid(220, 220, 25);
        const uint8_t *rgb = red.data();
        auto *input = gs_texture_create(W, H, GS_RGBA, 1, &rgb, GS_DYNAMIC);

        auto f0 = record(fx, a, input, blank, 0);
        gs_texture_set_image(input, green.data(), W * 4, false);
        auto f1 = record(fx, b, input, gs_texrender_get_texture(a), 1);
        gs_texture_set_image(input, blue.data(), W * 4, false);
        auto f2 = record(fx, a, input, gs_texrender_get_texture(b), 2);
        auto f3 = record(fx, b, input, gs_texrender_get_texture(a), 3, true);
        auto f4 = record(fx, a, input, gs_texrender_get_texture(b), 4, false, true);
        gs_texture_set_image(input, yellow.data(), W * 4, false);
        auto f5 = record(fx, b, input, gs_texrender_get_texture(a), 5, false, false, true);

        auto show = [](const char *label, const Frame &f) {
            std::printf("%s size=%zu ", label, f.size());
            for (int x : {4, 12, 20, 36, 40}) {
                size_t i = 4 * (16 * W + x);
                if (i + 3 < f.size())
                    std::printf("x%d=%u,%u,%u,%u ", x, f[i], f[i + 1], f[i + 2], f[i + 3]);
            }
            std::printf("\n");
        };
        show("f0", f0); show("f1", f1); show("f2", f2);
        show("f3", f3); show("f4", f4); show("f5", f5);

        bool wrote = pixel(f0, 4, 220, 25, 30, 255) &&
                     pixel(f1, 12, 25, 220, 30, 255) &&
                     pixel(f2, 20, 25, 30, 220, 255);
        bool retained = pixel(f2, 4, 220, 25, 30, 255) &&
                        pixel(f2, 12, 25, 220, 30, 255) &&
                        pixel(f2, 36, 0, 0, 0, 0);
        bool held = pixel(f3, 4, 220, 25, 30, 255) &&
                    pixel(f3, 20, 25, 30, 220, 255);
        bool erased = pixel(f4, 4, 0, 0, 0, 0) &&
                      pixel(f4, 20, 0, 0, 0, 0);
        bool reversed = pixel(f5, 36, 220, 220, 25, 255) &&
                        pixel(f5, 4, 0, 0, 0, 0);
        pass = wrote && retained && held && erased && reversed;
        std::printf("WARP_TAPE wrote=%d retained=%d held=%d erased=%d reversed=%d\n",
                    wrote, retained, held, erased, reversed);

        gs_texture_destroy(input);
        gs_texture_destroy(blank);
        gs_texrender_destroy(a);
        gs_texrender_destroy(b);
        gs_effect_destroy(fx);
    }
    if (errors) bfree(errors);
    obs_leave_graphics();
    obs_shutdown();
    std::printf("WARP_TAPE_GPU_PASS=%d\n", pass);
    return pass ? 0 : 1;
}
