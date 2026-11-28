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
        obs_source_video_render(source);
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
    obs_data_set_string(settings,"shader_file","shaders/root/plain.effect");
    auto* source=obs_source_create("hypershader_source","ECS lifecycle test",settings,nullptr);
    obs_data_release(settings);
    if(!source)return 5;
    obs_source_inc_showing(source);obs_set_output_source(0,source);
    auto wait=[&](int red,bool moving){
        std::set<int> greens;
        for(int i=0;i<60;i++){
            auto st=render(source);
            if(st.valid&&abs(st.red-red)<=2) {
                greens.insert(st.green);
                if(!moving||greens.size()>=3)return true;
            }
            std::this_thread::sleep_for(35ms);
        }
        return false;
    };
    auto select=[&](const char* path){
        auto* cfg=obs_source_get_settings(source);
        obs_data_set_string(cfg,"shader_file",path);
        obs_source_update(source,cfg);obs_data_release(cfg);
    };
    bool plain=wait(51,false);
    printf("ECS_PLAIN_READY=%d\n",plain);fflush(stdout);
    select("shaders/root/entity.effect");
    bool entities=wait(175,true);
    printf("ECS_ENTITY_READY=%d\n",entities);fflush(stdout);
    select("shaders/root/plain.effect");
    bool away=wait(51,false);
    printf("ECS_AWAY_READY=%d\n",away);fflush(stdout);
    select("shaders/root/entity.effect");
    bool again=wait(175,true);
    printf("ECS_RETURN_READY=%d\n",again);fflush(stdout);
    obs_set_output_source(0,nullptr);obs_source_dec_showing(source);
    obs_source_remove(source);obs_source_release(source);
    obs_wait_for_destroy_queue();obs_shutdown();
    bool pass=plain&&entities&&away&&again;
    printf("ECS_PIXELS plain=%d moving=%d away=%d return=%d PASS=%d\n",plain,entities,away,again,pass);
    return pass?0:1;
}
