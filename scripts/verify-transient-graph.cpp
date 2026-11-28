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
constexpr uint32_t W=96,H=72;
static thread_local bool measuring=false;
static std::set<gs_texrender_t*> surfaces;
extern "C" bool gs_texrender_begin(gs_texrender_t* target,uint32_t w,uint32_t h){
    using Fn=bool(*)(gs_texrender_t*,uint32_t,uint32_t);
    static auto real=reinterpret_cast<Fn>(dlsym(RTLD_NEXT,"gs_texrender_begin"));
    if(measuring&&w==W/2&&h==H/2)surfaces.insert(target);
    return real(target,w,h);
}
using namespace std::chrono_literals;
static const char* fixture_name(void*) {return "Transient input fixture";}
static uint32_t fixture_width(void*) {return W;}
static uint32_t fixture_height(void*) {return H;}
static void* fixture_create(obs_data_t*,obs_source_t*) {
    std::vector<uint8_t> pixels(W*H*4,255);
    const uint8_t* data=pixels.data();
    obs_enter_graphics();
    auto* tex=gs_texture_create(W,H,GS_RGBA,1,&data,0);
    obs_leave_graphics();
    return tex;
}
static void fixture_destroy(void* p) {
    obs_enter_graphics();gs_texture_destroy(static_cast<gs_texture_t*>(p));obs_leave_graphics();
}
static void fixture_render(void* p,gs_effect_t*) {
    obs_source_draw(static_cast<gs_texture_t*>(p),0,0,W,H,false);
}
static obs_source_info fixture_info={
    .id="hs_transient_fixture",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_VIDEO,
    .get_name=fixture_name,.create=fixture_create,.destroy=fixture_destroy,
    .get_width=fixture_width,.get_height=fixture_height,.video_render=fixture_render
};

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
    if(argc!=4||!obs_startup("en-US",nullptr,nullptr))return 2;
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
    obs_data_set_string(settings,"shader_file","shaders/root/transient.effect");
    const bool filterLane=std::getenv("HS_TRANSIENT_TEST_FILTER")!=nullptr;
    auto* source=obs_source_create(filterLane?"hypershader_filter":"hypershader_source","Transient graph probe",settings,nullptr);
    obs_data_release(settings);
    if(!source)return 5;
    obs_source_t* visible=source;
    if(filterLane) {
        obs_register_source(&fixture_info);
        visible=obs_source_create("hs_transient_fixture","Transient input",nullptr,nullptr);
        if(!visible)return 6;
        obs_source_filter_add(visible,source);
    }
    obs_source_inc_showing(visible);
    int valid=0,first=-1,last=-1;
    for(int i=0;i<70;i++){
        auto st=render(visible);
        if(st.valid&&st.red>0&&abs(st.green-153)<=2){
            if(first<0)first=st.red;
            last=st.red;valid++;
        }
        std::this_thread::sleep_for(20ms);
    }
    bool pass=valid>=25&&last>first+15&&surfaces.size()==static_cast<size_t>(atoi(argv[3]));
    printf("TRANSIENT_GRAPH valid=%d first=%d last=%d physical_targets=%zu PASS=%d\n",
           valid,first,last,surfaces.size(),pass);
    if(atoi(argv[3])==4) {
        for(const char* path:{"shaders/root/bad-end.effect","shaders/root/bad-future.effect","shaders/root/bad-history.effect"}) {
            auto* cfg=obs_source_get_settings(source);
            obs_data_set_string(cfg,"shader_file",path);
            obs_source_update(source,cfg);obs_data_release(cfg);
            int retained=0;
            for(int i=0;i<20;i++){
                auto st=render(visible);
                retained+=st.valid&&abs(st.red-204)<=2&&abs(st.green-153)<=2;
                std::this_thread::sleep_for(25ms);
            }
            bool ok=retained>=18;
            printf("TRANSIENT_GRAPH_REJECT %s retained=%d PASS=%d\n",path,retained,ok);
            pass&=ok;
        }
    }
    obs_source_dec_showing(visible);
    if(filterLane){obs_source_filter_remove(visible,source);obs_source_release(visible);}
    obs_source_remove(source);obs_source_release(source);
    obs_wait_for_destroy_queue();obs_shutdown();
    return pass?0:1;
}
