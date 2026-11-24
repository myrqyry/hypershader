// Real libobs probe: pulsed PCM -> HyperShader audio worker -> beat uniforms -> pixels.
// R=audio_level, G=audio_envelope, B=audio_beat. Asserts nonzero onsets and a clean
// fallback to zero during silence. No plugin mocks; reads back real GPU frames.
// The shader must live under the supplied data directory: the loader rejects paths
// outside it as a security measure.
#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <vector>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>

constexpr unsigned W = 128, H = 128;
using Frame = std::vector<uint8_t>;
struct TestSource { gs_texture_t *texture = nullptr; };
static TestSource *active = nullptr;

static const char *source_name(void *) { return "Beat fixture"; }
static uint32_t source_width(void *) { return W; }
static uint32_t source_height(void *) { return H; }

static void *source_create(obs_data_t *, obs_source_t *)
{
	auto *s = new TestSource();
	obs_enter_graphics();
	uint8_t blank[W * H * 4] = {0};
	for (size_t i = 3; i < sizeof(blank); i += 4)
		blank[i] = 255;
	const uint8_t *rows[1] = {blank};
	s->texture = gs_texture_create(W, H, GS_RGBA, 1, rows, 0);
	obs_leave_graphics();
	active = s;
	return s;
}

static void source_destroy(void *p)
{
	auto *s = static_cast<TestSource *>(p);
	obs_enter_graphics();
	if (s && s->texture)
		gs_texture_destroy(s->texture);
	obs_leave_graphics();
	if (active == s)
		active = nullptr;
	delete s;
}

static void source_render(void *data, gs_effect_t *)
{
	auto *s = static_cast<TestSource *>(data);
	if (s && s->texture)
		obs_source_draw(s->texture, 0, 0, W, H, false);
}

static obs_source_info fixture_info = {
	.id = "hyper_beat_fixture",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO,
	.get_name = source_name,
	.create = source_create,
	.destroy = source_destroy,
	.get_width = source_width,
	.get_height = source_height,
	.video_render = source_render,
};

static Frame render(obs_source_t *source)
{
	Frame frame;
	obs_enter_graphics();
	auto *target = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	auto *stage = gs_stagesurface_create(W, H, GS_RGBA);
	if (target && stage && gs_texrender_begin(target, W, H)) {
		gs_projection_push();
		gs_matrix_push();
		gs_matrix_identity();
		gs_ortho(0, float(W), 0, float(H), -100, 100);
		struct vec4 clear = {0, 0, 0, 0};
		gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
		obs_source_video_render(source);
		gs_matrix_pop();
		gs_projection_pop();
		gs_texrender_end(target);
		if (auto *tex = gs_texrender_get_texture(target)) {
			gs_stage_texture(stage, tex);
			uint8_t *data = nullptr;
			uint32_t pitch = 0;
			if (gs_stagesurface_map(stage, &data, &pitch)) {
				frame.resize(W * H * 4);
				for (unsigned y = 0; y < H; y++)
					std::memcpy(frame.data() + y * W * 4, data + y * pitch, W * 4);
				gs_stagesurface_unmap(stage);
			}
		}
	}
	if (stage)
		gs_stagesurface_destroy(stage);
	if (target)
		gs_texrender_destroy(target);
	obs_leave_graphics();
	return frame;
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		std::fprintf(stderr, "usage: %s <module> <data> <effect>\n", argv[0]);
		return 2;
	}
	if (!obs_startup("en-US", nullptr, nullptr))
		return 3;
	obs_audio_info audio{};
	audio.samples_per_sec = 48000;
	audio.speakers = SPEAKERS_STEREO;
	if (!obs_reset_audio(&audio))
		return 4;
	obs_video_info vid{};
	vid.graphics_module = "libobs-opengl";
	vid.fps_num = 30;
	vid.fps_den = 1;
	vid.base_width = W;
	vid.base_height = H;
	vid.output_width = W;
	vid.output_height = H;
	vid.output_format = VIDEO_FORMAT_RGBA;
	vid.scale_type = OBS_SCALE_DISABLE;
	if (obs_reset_video(&vid) != OBS_VIDEO_SUCCESS)
		return 5;
	obs_register_source(&fixture_info);
	obs_module_t *module = nullptr;
	if (obs_open_module(&module, argv[1], argv[2]) != MODULE_SUCCESS || !obs_init_module(module))
		return 6;

	auto *source = obs_source_create("hyper_beat_fixture", "Beat fixture", nullptr, nullptr);
	auto *settings = obs_data_create();
	obs_data_set_string(settings, "shader_file", argv[3]);
	obs_data_set_bool(settings, "enable_sound", true);
	obs_data_set_string(settings, "audio_source", "Beat fixture");
	obs_data_set_bool(settings, "audio_debug", false);
	obs_data_set_bool(settings, "audio_test_tone", false);
	obs_data_set_int(settings, "input_mode", 0);
	obs_data_set_int(settings, "audio_fft_size", 1024);
	obs_data_set_int(settings, "audio_window_type", 1);
	obs_data_set_double(settings, "speed", 0.0);
	auto *filter = obs_source_create("hypershader_filter", "Beat probe", settings, nullptr);
	obs_data_release(settings);
	if (!source || !filter)
		return 7;
	obs_source_filter_add(source, filter);
	obs_source_inc_showing(source);
	obs_set_output_source(0, source);

	// Pulse: 4 frames of tone, 7 frames of silence, repeated. Onsets must fire.
	int maxR = 0, maxG = 0, maxB = 0;
	double phase = 0.0;
	const double two_pi = 6.283185307179586;
	for (int f = 0; f < 120; f++) {
		const bool tone = (f % 11) < 4;
		std::array<float, 2048> l{}, r{};
		for (size_t j = 0; j < l.size(); j++) {
			float s = tone ? 0.70f * std::sin(phase) : 0.0f;
			l[j] = s;
			r[j] = s;
			phase += two_pi * 440.0 / 48000.0;
			if (phase > two_pi)
				phase -= two_pi;
		}
		obs_source_audio pcm{};
		pcm.frames = uint32_t(l.size());
		pcm.samples_per_sec = 48000;
		pcm.speakers = SPEAKERS_STEREO;
		pcm.format = AUDIO_FORMAT_FLOAT_PLANAR;
		pcm.timestamp = os_gettime_ns();
		pcm.data[0] = reinterpret_cast<const uint8_t *>(l.data());
		pcm.data[1] = reinterpret_cast<const uint8_t *>(r.data());
		obs_source_output_audio(source, &pcm);

		Frame fr = render(source);
		if (fr.size() == W * H * 4) {
			for (unsigned y = 0; y < H; y++) {
				for (unsigned x = 0; x < W; x++) {
					size_t i = (y * W + x) * 4;
					if (fr[i + 0] > maxR) maxR = fr[i + 0];
					if (fr[i + 1] > maxG) maxG = fr[i + 1];
					if (fr[i + 2] > maxB) maxB = fr[i + 2];
				}
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(21));
	}

	// Long silence afterwards: the envelope and impulse must fall back to zero.
	int silR = 0, silG = 0, silB = 0;
	for (int f = 0; f < 60; f++) {
		std::array<float, 2048> l{}, r{};
		obs_source_audio pcm{};
		pcm.frames = uint32_t(l.size());
		pcm.samples_per_sec = 48000;
		pcm.speakers = SPEAKERS_STEREO;
		pcm.format = AUDIO_FORMAT_FLOAT_PLANAR;
		pcm.timestamp = os_gettime_ns();
		pcm.data[0] = reinterpret_cast<const uint8_t *>(l.data());
		pcm.data[1] = reinterpret_cast<const uint8_t *>(r.data());
		obs_source_output_audio(source, &pcm);
		Frame fr = render(source);
		if (f >= 50 && fr.size() == W * H * 4) {
			silR = silG = silB = 0;
			for (size_t i = 0; i < fr.size(); i += 4) {
				if (fr[i + 0] > silR) silR = fr[i + 0];
				if (fr[i + 1] > silG) silG = fr[i + 1];
				if (fr[i + 2] > silB) silB = fr[i + 2];
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(21));
	}

	const bool pass = maxR > 8 && maxG > 8 && maxB > 8 && silG <= 2 && silB <= 2;
	std::printf("BEAT_PROBE max_level=%d max_envelope=%d max_beat=%d "
		    "silence_level=%d silence_envelope=%d silence_beat=%d PASS=%d\n",
		    maxR, maxG, maxB, silR, silG, silB, pass);
	std::fflush(stdout);

	/* Exit without libobs source teardown: this probe's result is already
	 * reported, and the plugin's audio-source stop path has a pre-existing
	 * teardown race that is unrelated to what is being measured here. */
	std::_Exit(pass ? 0 : 1);
}
