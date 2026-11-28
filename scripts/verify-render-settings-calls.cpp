#include <dlfcn.h>
#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <set>
// Interpose the real libobs settings calls only while the probe thread draws.
static thread_local bool measuring=false;
static thread_local unsigned settings_calls=0, value_calls=0;
extern "C" obs_data_t *obs_source_get_settings(const obs_source_t *source) {
    using Fn=obs_data_t*(*)(const obs_source_t*);
    static auto real=reinterpret_cast<Fn>(dlsym(RTLD_NEXT,"obs_source_get_settings"));
    if(measuring) ++settings_calls;
    return real(source);
}
#define HS_COUNT_VALUE(TYPE,NAME) \
extern "C" TYPE NAME(obs_data_t *data,const char *key) { \
    using Fn=TYPE(*)(obs_data_t*,const char*); \
    static auto real=reinterpret_cast<Fn>(dlsym(RTLD_NEXT,#NAME)); \
    if(measuring) ++value_calls; \
    return real(data,key); \
}
HS_COUNT_VALUE(double,obs_data_get_double)
HS_COUNT_VALUE(long long,obs_data_get_int)
HS_COUNT_VALUE(bool,obs_data_get_bool)
HS_COUNT_VALUE(const char*,obs_data_get_string)
HS_COUNT_VALUE(bool,obs_data_has_user_value)
constexpr uint32_t W=96,H=72;
using namespace std::chrono_literals;
struct Stats {int red=-1,green=-1;bool valid=false;};
Stats render(obs_source_t* source) {
    Stats st;
    obs_enter_graphics();
    gs_set_cull_mode(GS_NEITHER);
    auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
    auto* stage=gs_stagesurface_create(W,H,GS_RGBA);
    if(target&&stage&&gs_texrender_begin(target,W,H)) {
        gs_projection_push();gs_matrix_push();gs_matrix_identity();
        gs_ortho(0,float(W),0,float(H),-100,100);
        measuring=true;
        obs_source_video_render(source);
        measuring=false;
        gs_matrix_pop();gs_projection_pop();
        gs_texrender_end(target);
        auto* tex=gs_texrender_get_texture(target);
        if(tex) {
            gs_stage_texture(stage,tex);
            uint8_t* pixels=nullptr;uint32_t pitch=0;
            if(gs_stagesurface_map(stage,&pixels,&pitch)) {
                uint8_t* sample=pixels+(H/2)*pitch+(W/2)*4;
                st.red=int(sample[0]);st.green=int(sample[1]);
                st.valid=true;
                gs_stagesurface_unmap(stage);
            }
        }
    }
    if(stage)gs_stagesurface_destroy(stage);
    if(target)gs_texrender_destroy(target);
    obs_leave_graphics();
    return st;
}


int main(int argc,char**argv){
    if(argc!=3||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_video_info vi={};
    vi.graphics_module="libobs-opengl";vi.fps_num=30;vi.fps_den=1;
    vi.base_width=W;vi.base_height=H;vi.output_width=W;vi.output_height=H;
    vi.output_format=VIDEO_FORMAT_RGBA;vi.scale_type=OBS_SCALE_DISABLE;
    if(obs_reset_video(&vi)!=OBS_VIDEO_SUCCESS)return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    auto* settings=obs_data_create();
    obs_data_set_int(settings,"preset",-1);
    obs_data_set_int(settings,"source_width",W);obs_data_set_int(settings,"source_height",H);
    obs_data_set_bool(settings,"sight_use_tracking",true);
    obs_data_set_string(settings,"shader_file","shaders/root/cache_probe.effect");
    obs_data_set_double(settings,"param_amount",.25);
    obs_data_set_int(settings,"param_idle_atlas_columns",2);
    auto* source=obs_source_create("hypershader_source","Settings call probe",settings,nullptr);
    obs_data_release(settings);
    if(!source)return 5;
    obs_source_inc_showing(source);obs_set_output_source(0,source);
    bool ready=false;
    for(int i=0;i<90;i++){
        auto st=render(source);
        if(st.valid&&abs(st.red-64)<=2&&abs(st.green-128)<=2){ready=true;break;}
        std::this_thread::sleep_for(25ms);
    }
    settings_calls=value_calls=0;
    unsigned valid=0;
    Stats last;
    for(int i=0;i<60;i++){
        auto st=render(source);
        last=st;
        valid+=st.valid&&abs(st.red-64)<=2&&abs(st.green-128)<=2;
        std::this_thread::sleep_for(5ms);
    }
    bool pass=ready&&valid==60&&settings_calls==0&&value_calls==0;
    printf("RENDER_SETTINGS_CALLS draws=%u red=%d green=%d ready=%d settings=%u values=%u PASS=%d\n",valid,last.red,last.green,ready,settings_calls,value_calls,pass);
    obs_set_output_source(0,nullptr);obs_source_dec_showing(source);
    obs_source_remove(source);obs_source_release(source);
    obs_wait_for_destroy_queue();obs_shutdown();
    return pass?0:1;
}
