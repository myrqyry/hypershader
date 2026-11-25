// Real libobs input-picker regression; native shader compilation, no Qt stubs.
#include <obs.h>
#include <graphics/graphics.h>
#include <functional>
#include <thread>
#include <chrono>
#include <string>
#include <cstdio>
#include <mutex>
#include <vector>
#include <cstdlib>
using namespace std::chrono_literals;
static std::mutex uiMutex;
static std::vector<std::pair<obs_task_t,void *>> uiTasks;
static void queueUI(obs_task_t task,void *arg,bool wait) {
    if(wait) std::abort();
    std::lock_guard<std::mutex> guard(uiMutex);
    uiTasks.emplace_back(task,arg);
}
static void drainUI() {
    std::vector<std::pair<obs_task_t,void *>> tasks;
    {std::lock_guard<std::mutex> guard(uiMutex); tasks.swap(uiTasks);}
    for(auto &task:tasks)task.first(task.second);
}

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
    if (argc != 3 || !obs_startup("en-US", nullptr, nullptr)) return 2;
    obs_set_ui_task_handler(queueUI);
    obs_video_info video{};
    video.graphics_module="libobs-opengl"; video.fps_num=30; video.fps_den=1;
    video.base_width=64; video.base_height=64; video.output_width=64; video.output_height=64;
    video.output_format=VIDEO_FORMAT_RGBA; video.scale_type=OBS_SCALE_DISABLE;
    if(obs_reset_video(&video)!=OBS_VIDEO_SUCCESS) return 3;
    obs_register_source(&source_info);
    obs_module_t *module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS || !obs_init_module(module)) return 4;
    auto *source=obs_source_create("hs_conditional_test","Test video",nullptr,nullptr);
    auto *settings=obs_data_create();
    auto *filter=obs_source_create("hypershader_filter","Input controls",settings,nullptr);
    obs_data_release(settings);
    if(!source || !filter) return 5;
    obs_source_filter_add(source,filter);
    obs_source_inc_showing(source);
    struct Case {const char *file; bool audio,sight,texture;};
    const Case cases[]={
        {"root/filter.vidicon_afterglow.effect",false,false,false},
        {"tests/input-audio.effect",true,false,false},
        {"root/filter.sight_relight.effect",false,true,false},
        {"tests/input-texture.effect",false,false,true},
        {"tests/input-named.effect",false,false,true},
        {"root/filter.face_mesh_wireframe.effect",false,true,false},
        {"tests/input-plain.effect",false,false,false},
        {"tests/input-combined.effect",true,true,true},
        {"root/filter.vidicon_afterglow.effect",false,false,false}
    };
    bool pass=true;
    for(const auto &c:cases) {
        auto path=std::string(argv[2])+"/shaders/"+c.file;
        auto *data=obs_source_get_settings(filter);
        obs_data_set_string(data,"shader_file",path.c_str());
        obs_source_update(filter,data);
        obs_data_release(data);
        for(int i=0;i<45;i++){tick(source);drainUI();std::this_thread::sleep_for(20ms);}
        auto *p=obs_source_properties(filter);
        auto visible=[&](const char *key){auto *v=obs_properties_get(p,key);return v && obs_property_visible(v);};
        auto pos=[&](const char *key){int i=0;for(auto *v=obs_properties_first(p);v;obs_property_next(&v),++i)
            if(std::string(obs_property_name(v))==key)return i;return 10000;};
        bool order=pos("audio_source")<pos("global_overrides_group") &&
            pos("sight_tracking_channel")<pos("global_overrides_group") &&
            pos("channel0_group")<pos("global_overrides_group") &&
            pos("audio_source")<pos("shader_params_group") &&
            pos("sight_tracking_channel")<pos("shader_params_group") &&
            pos("channel0_group")<pos("shader_params_group");
        bool audio=visible("audio_source"), sight=visible("sight_tracking_channel"), texture=visible("channel0_group");
        bool ok=order && audio==c.audio && sight==c.sight && texture==c.texture;
        printf("INPUT_UI %s audio=%d sight=%d texture=%d order=%d PASS=%d\n",c.file,audio,sight,texture,order,ok);
        pass &= ok;
        if (std::string(c.file)=="tests/input-texture.effect") {
            auto *group=obs_properties_get(p,"channel0_group");
            auto *sub=obs_property_group_content(group);
            auto *type=obs_properties_get(sub,"channel0_type");
            auto *cfg=obs_source_get_settings(filter);
            obs_data_set_int(cfg,"channel0_type",3);
            bool cb=obs_property_modified(type,cfg);
            bool spectrum=visible("audio_source");
            obs_data_set_int(cfg,"channel0_type",0);
            bool cb2=obs_property_modified(type,cfg);
            bool reset=!visible("audio_source");
            bool works=cb && cb2 && spectrum && reset;
            printf("INPUT_UI spectrum_selection PASS=%d\n",works);
            pass &= works;
            obs_data_release(cfg);
        }
        if (std::string(c.file)=="tests/input-named.effect") {
            auto *group=obs_properties_get(p,"channel0_group");
            bool named=group && std::string(obs_property_description(group))=="Overlay Image";
            printf("INPUT_UI named_label PASS=%d\n",named);
            pass &= named;
        }
        obs_properties_destroy(p);
    }
    obs_source_filter_remove(source,filter);
    obs_source_dec_showing(source);
    obs_source_remove(source);
    obs_source_release(filter);
    obs_source_release(source);
    drainUI();obs_wait_for_destroy_queue();drainUI();obs_shutdown();
    return pass?0:1;
}
