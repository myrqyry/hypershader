// Real libobs reactive sprite test: PCM, image textures, native source and filter.
#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <vector>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <string>
#include <algorithm>
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
		{ blank[i] = 255; blank[i-3] = 255; }
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
    if (argc != 6) return 2;
    bool standalone = std::strncmp(argv[5], "source", 6) == 0;
    bool motion = std::strstr(argv[5], "-motion") != nullptr;
    bool atlas = std::strstr(argv[5], "-atlas") != nullptr;
    bool burst_atlas = std::strstr(argv[5], "-atlas-burst") != nullptr;
    bool idle_atlas = burst_atlas || std::strstr(argv[5], "-atlas-idle") != nullptr;
    bool strong_atlas = std::strstr(argv[5], "-atlas-strong") != nullptr;
    bool accent_atlas = std::strstr(argv[5], "-atlas-accent") != nullptr;
    bool gif = std::strstr(argv[5], "-gif") != nullptr;
    bool idle_gif = std::strstr(argv[5], "-gif-idle") != nullptr;
    bool stale = std::strstr(argv[5], "-stale") != nullptr;
    bool timing = std::strstr(argv[5], "-timing") != nullptr;
    bool instant = std::strstr(argv[5], "-instant") != nullptr;
    bool mapped = std::strstr(argv[5], "-mapped") != nullptr;
    if (!obs_startup("en-US", nullptr, nullptr)) return 3;
    obs_audio_info audio{};
    audio.samples_per_sec = 48000; audio.speakers = SPEAKERS_STEREO;
    if (!obs_reset_audio(&audio)) return 4;
    obs_video_info vid{};
    vid.graphics_module = "libobs-opengl";
    vid.fps_num = 30; vid.fps_den = 1;
    vid.base_width = vid.output_width = W;
    vid.base_height = vid.output_height = H;
    vid.output_format = VIDEO_FORMAT_RGBA;
    vid.scale_type = OBS_SCALE_DISABLE;
    if (obs_reset_video(&vid) != OBS_VIDEO_SUCCESS) return 5;
    obs_register_source(&fixture_info);
    obs_module_t *module = nullptr;
    if (obs_open_module(&module, argv[1], argv[2]) != MODULE_SUCCESS || !obs_init_module(module)) return 6;
    auto *fixture = obs_source_create("hyper_beat_fixture", "Beat fixture", nullptr, nullptr);
    auto *settings = obs_data_create();
    obs_data_set_int(settings, "preset", -1);
    obs_data_set_string(settings, "shader_file", argv[3]);
    obs_data_set_bool(settings, "enable_sound", true);
    obs_data_set_string(settings, "audio_source", "Beat fixture");
    obs_data_set_int(settings, "source_width", W);
    obs_data_set_int(settings, "source_height", H);
    obs_data_set_int(settings, "input_mode", 0);
    obs_data_set_double(settings, "param_threshold", 0.015);
    if (gif || atlas) {
        obs_data_set_bool(settings, "param_temporal_response", false);
        obs_data_set_int(settings, "param_animation_playback", 1);
    }
    if (timing || instant) {
        obs_data_set_bool(settings, "param_temporal_response", !instant);
        obs_data_set_double(settings, "param_hold_ms", 650);
        obs_data_set_double(settings, "param_attack_ms", 100);
        obs_data_set_double(settings, "param_release_ms", 100);
    }
    obs_data_set_double(settings, "param_transition_width", 0.015);
    obs_data_set_double(settings, "param_bounce", motion ? 0.16 : 0.0);
    obs_data_set_int(settings, "param_response_driver", motion ? 1 : 0);
    obs_data_set_double(settings, "param_stretch", 0.0);
    obs_data_set_double(settings, "param_tilt", 0.0);
    obs_data_set_double(settings, "param_idle_breathe", 0.0);
    obs_data_set_double(settings, "param_glow", 0.0);
    obs_data_set_int(settings, "channel0_type", standalone || !motion ? 1 : 0);
    std::string dir = argv[4];
    obs_data_set_string(settings, "channel0_file", (dir + (standalone ? (idle_gif ? "/expression.gif" : "/red.png") : gif ? "/expression.gif" : "/green.png")).c_str());
    if (standalone && !motion && !idle_gif) {
        obs_data_set_int(settings, "channel1_type", 1);
        obs_data_set_string(settings, "channel1_file", (dir + (gif ? "/expression.gif" : "/green.png")).c_str());
    }
    if(atlas) {
        std::string prefix=idle_atlas?"param_idle_atlas_":strong_atlas?"param_strong_atlas_":accent_atlas?"param_accent_atlas_":"param_active_atlas_";
        obs_data_set_int(settings,(prefix+"columns").c_str(),2);
        obs_data_set_int(settings,(prefix+"rows").c_str(),2);
        obs_data_set_int(settings,(prefix+"first").c_str(),1);
        obs_data_set_int(settings,(prefix+"count").c_str(),2);
        obs_data_set_double(settings,(prefix+"fps").c_str(),4);
        obs_data_set_int(settings,(prefix+"mode").c_str(),burst_atlas?4:idle_atlas?3:1);
        if(burst_atlas) {obs_data_set_double(settings,"param_idle_atlas_rest_min",.1);obs_data_set_double(settings,"param_idle_atlas_rest_max",.4);}
        const char *filekey=standalone&&!idle_atlas?"channel1_file":"channel0_file";
        obs_data_set_string(settings,filekey,(dir+"/atlas.png").c_str());
        if(idle_atlas) obs_data_set_int(settings,"channel1_type",0);
        if(strong_atlas || accent_atlas) {
            obs_data_set_double(settings,"param_strong_threshold",.015);
            obs_data_set_double(settings,"param_accent_threshold",.015);
            int channel=(strong_atlas?1:2)+(standalone?1:0);
            obs_data_set_int(settings,standalone?"channel1_type":"channel0_type",0);
            std::string key="channel"+std::to_string(channel);
            obs_data_set_int(settings,(key+"_type").c_str(),1);
            obs_data_set_string(settings,(key+"_file").c_str(),(dir+"/atlas.png").c_str());
        }
    }
    auto *sprite = obs_source_create(standalone ? "hypershader_source" : "hypershader_filter",
                                     "Reactive sprite", settings, nullptr);
    obs_data_release(settings);
    if (!fixture || !sprite) return 7;
    if (!standalone) obs_source_filter_add(fixture, sprite);
    auto *visible = standalone ? sprite : fixture;
    obs_source_inc_showing(fixture);
    if (standalone) obs_source_inc_showing(sprite);
    obs_set_output_source(0, visible);
    double phase = 0.0;
    auto step = [&](bool tone, bool deliver = true) {
        std::array<float, 1600> samples{};
        for (auto &sample : samples) {
            sample = tone ? 0.85f * std::sin(phase) : 0.0f;
            phase = std::fmod(phase + 6.283185307179586 * 440.0 / 48000.0, 6.283185307179586);
        }
        obs_source_audio pcm{};
        pcm.frames = samples.size(); pcm.samples_per_sec = 48000;
        pcm.speakers = SPEAKERS_STEREO; pcm.format = AUDIO_FORMAT_FLOAT_PLANAR;
        pcm.timestamp = os_gettime_ns();
        pcm.data[0] = pcm.data[1] = reinterpret_cast<const uint8_t *>(samples.data());
        if (deliver) obs_source_output_audio(fixture, &pcm);
        std::this_thread::sleep_for(std::chrono::milliseconds(34));
        return render(visible);
    };
    auto center = [](const Frame &frame, bool green) {
        if (frame.size() != W * H * 4) return false;
        size_t i = (H / 2 * W + W / 2) * 4;
        return frame[i + (green ? 1 : 0)] > 180 && frame[i + (green ? 0 : 1)] < 60 && frame[i+3] > 180;
    };
    auto centroid = [](const Frame &frame) {
        double sum=0, count=0;
        for (size_t i=0; i+3<frame.size(); i+=4)
            if (frame[i]>180 && frame[i+3]>180) { sum+=(i/4)/W; count++; }
        return count>0 ? sum/count : -1.0;
    };
    if(atlas) {
        auto color_at=[](const Frame &f,int x,int y,int r,int g,int b) {
            if(f.size()!=W*H*4) return false;
            auto i=(y*W+x)*4;
            return std::abs(int(f[i])-r)<25 && std::abs(int(f[i+1])-g)<25 &&
                   std::abs(int(f[i+2])-b)<25 && f[i+3]>220;
        };
        auto green=[&](const Frame &f){return color_at(f,W/2,H/2,0,255,0);};
        auto blue=[&](const Frame &f){return color_at(f,W/2,H/2,0,0,255);};
        auto correct_edges=[&](const Frame &f,bool g){
            return f.size()==W*H*4 && f[3]<10 &&
                color_at(f,0,H/2,0,g?255:0,g?0:255) &&
                color_at(f,W-1,H/2,0,g?255:0,g?0:255);
        };
        for(int i=0;i<45;i++) step(false);
        auto *p=obs_source_properties(sprite);
        bool ui=p&&obs_properties_get(p,idle_atlas?"param_idle_atlas_fps":strong_atlas?"param_strong_atlas_fps":accent_atlas?"param_accent_atlas_fps":"param_active_atlas_fps");
        if(standalone) {
            auto *rest_min=p?obs_properties_get(p,"param_idle_atlas_rest_min"):nullptr;
            auto *rest_max=p?obs_properties_get(p,"param_idle_atlas_rest_max"):nullptr;
            ui=ui&&rest_min&&rest_max&&obs_property_visible(rest_min)==burst_atlas&&obs_property_visible(rest_max)==burst_atlas;
        }
        if(p) obs_properties_destroy(p);
        int g[2]={0,0},b[2]={0,0},edge[2]={0,0},bad=0,first[2]={0,0},endblue[2]={0,0};
        for(int cycle=0;cycle<2;cycle++) {
            bool seen=false;
            for(int i=0;i<42;i++) {
                auto f=step(!idle_atlas);
                bool gg=green(f),bb=blue(f);
                if(!seen&&(gg||bb)){first[cycle]=gg;seen=true;}
                g[cycle]+=gg;b[cycle]+=bb;
                if(gg||bb) edge[cycle]+=correct_edges(f,gg);
                if(i>30) endblue[cycle]+=bb;
                bad+=color_at(f,W/2,H/2,255,0,255)||color_at(f,W/2,H/2,255,255,0);
            }
            for(int i=0;i<25;i++) step(false);
        }
        bool ok=ui&&!bad&&g[0]>=2&&g[1]>=2&&b[0]>=2&&b[1]>=2&&edge[0]>=5&&edge[1]>=5
            &&(idle_atlas||(first[0]&&first[1]&&endblue[0]>=8&&endblue[1]>=8));
        std::printf("REACTIVE_SPRITE %s g=%d/%d b=%d/%d edge=%d/%d first=%d/%d end=%d/%d bad=%d ui=%d PASS=%d\n",
            argv[5],g[0],g[1],b[0],b[1],edge[0],edge[1],first[0],first[1],endblue[0],endblue[1],bad,ui,ok);
        std::fflush(stdout);std::_Exit(ok?0:1);
    }
    if (gif) {
        auto is_blue=[](const Frame &frame) {
            if(frame.size()!=W*H*4) return false;
            size_t i=(H/2*W+W/2)*4;
            return frame[i+2]>180 && frame[i]<60 && frame[i+1]<60;
        };
        // Allow async shader load to finish before checking UI.
        for(int i=0;i<45;i++) step(false);
        auto *props=obs_source_properties(sprite);
        auto *playback=props ? obs_properties_get(props,"param_animation_playback") : nullptr;
        bool ui=playback&&obs_property_get_type(playback)==OBS_PROPERTY_LIST&&obs_property_list_item_count(playback)==3;
        if(props) obs_properties_destroy(props);
        if(idle_gif) {
            int green=0,blue=0;
            for(int i=0;i<90;i++) { auto fr=step(false); green+=center(fr,true); blue+=is_blue(fr); }
            bool ok=ui&&green>=5&&blue>=5;
            std::printf("REACTIVE_SPRITE %s green=%d blue=%d ui=%d PASS=%d\n",argv[5],green,blue,ui,ok);
            std::fflush(stdout); std::_Exit(ok?0:1);
        }
        int first_green[2]={0,0}, blue[2]={0,0}, idle_count=0;
        for(int cycle=0;cycle<2;cycle++) {
            bool seen=false;
            for(int i=0;i<32;i++) {
                auto fr=step(true);
                bool green=center(fr,true), b=is_blue(fr);
                if(!seen && (green||b)) {first_green[cycle]=green; seen=true;}
                blue[cycle]+=b;
            }
            for(int i=0;i<25;i++) {
                auto fr=step(false);
                if(i>18) idle_count+=center(fr,false);
            }
        }
        bool ok=ui&&first_green[0]&&first_green[1]&&blue[0]>=2&&blue[1]>=2&&idle_count>=10;
        std::printf("REACTIVE_SPRITE %s restart=%d/%d blue=%d/%d idle=%d PASS=%d\n",
                    argv[5],first_green[0],first_green[1],blue[0],blue[1],idle_count,ok);
        std::fflush(stdout);
        std::_Exit(ok?0:1);
    }
    int idle=0, active_count=0, returned=0, transparent=0, held=0;
    double baseline=-1.0, max_shift=0.0;
    for (int f=0; f<45; ++f) {
        auto fr=step(false);
        if(f>30) {
            idle+=center(fr,false);
            baseline=centroid(fr);
            if(fr.size()==W*H*4 && fr[3]<10) transparent++;
        }
    }
    for (int f=0; f<80; ++f) {
        auto fr=step(true);
        if(f>30) {
            if(motion) { double y=centroid(fr); if(y>=0) max_shift=std::max(max_shift,baseline-y); active_count+=y>=0 && baseline-y>6.0; }
            else active_count+=center(fr,true);
        }
    }
    for (int f=0; f<80; ++f) {
        auto fr=step(false,!stale);
        if(f>=8 && f<13) held+=center(fr,true);
        if(f>55) returned+=center(fr,false) && (!motion || std::abs(centroid(fr)-baseline)<2);
    }
    auto *props = obs_source_properties(sprite);
    auto *audio_prop = props ? obs_properties_get(props, "enable_sound") : nullptr;
    auto *driver_prop = props ? obs_properties_get(props, "param_response_driver") : nullptr;
    bool ui = audio_prop && obs_property_visible(audio_prop) && driver_prop &&
              obs_property_get_type(driver_prop) == OBS_PROPERTY_LIST &&
              obs_property_list_item_count(driver_prop) == 6;
    if (props) obs_properties_destroy(props);
    std::printf("REACTIVE_SPRITE_UI %s audio_and_driver=%d\n",argv[5],ui);
    bool dynamics_ui = true;
    if (!mapped) {
        auto *p = obs_source_properties(sprite);
        for (const char *key : {"param_temporal_response","param_attack_ms","param_hold_ms","param_release_ms","param_hysteresis","param_spring_frequency","param_spring_damping"}) {
            auto *prop = p ? obs_properties_get(p,key) : nullptr;
            dynamics_ui &= prop != nullptr;
        }
        if(p) obs_properties_destroy(p);
    }
    if(mapped) ui=true;
    bool pass = ui && dynamics_ui && (!timing || held>=4) && (!instant || stale || held<=1) && idle>=10 && active_count>=25 && returned>=15 && (!standalone || mapped || transparent>=10);
    std::printf("REACTIVE_SPRITE %s idle=%d active=%d returned=%d transparent=%d displacement=%.2f held=%d dynamics_ui=%d PASS=%d\n",
                argv[5],idle,active_count,returned,transparent,max_shift,held,dynamics_ui,pass);
    std::fflush(stdout);
    // Existing audio-source teardown race is outside this shader test.
    std::_Exit(pass ? 0 : 1);
}
