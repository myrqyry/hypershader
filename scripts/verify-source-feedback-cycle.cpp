// Real libobs standalone hypershader source regression: validate that
// feedback output keeps updating after both history textures are reused.
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
struct Stats {int red=-1;bool valid=false;};
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
                st.red=int(sample[0]);
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
int main(int argc,char** argv){
    if(argc!=4) {fprintf(stderr,"usage: %s MODULE MODULE_DATA EFFECT_PATH\n",argv[0]);return 2;}
    if(!obs_startup("en-US",nullptr,nullptr))return 3;
    obs_video_info vi={};
    vi.graphics_module="libobs-opengl";vi.fps_num=30;vi.fps_den=1;
    vi.base_width=W;vi.base_height=H;vi.output_width=W;vi.output_height=H;
    vi.output_format=VIDEO_FORMAT_RGBA;vi.scale_type=OBS_SCALE_DISABLE;
    if(obs_reset_video(&vi)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 4;}
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||
        !obs_init_module(module)){
        fprintf(stderr,"hypershader module loading failed\n");
        obs_shutdown();return 5;
    }
    auto* settings=obs_data_create();
    obs_data_set_int(settings,"preset",-1);
    obs_data_set_int(settings,"source_width",W);
    obs_data_set_int(settings,"source_height",H);
    obs_data_set_string(settings,"shader_file",argv[3]);
    auto* source=obs_source_create("hypershader_source","Feedback source test",settings,nullptr);
    obs_data_release(settings);
    if(!source){fprintf(stderr,"SOURCE_CREATION_FAILED\n");obs_shutdown();return 6;}
    obs_source_inc_showing(source);
    obs_set_output_source(0,source);
    Stats current{};
    std::set<int> levels;
    for(int i=0;i<100;i++){
        std::this_thread::sleep_for(45ms);
        current=render(source);
        if(current.valid && i>20)levels.insert(current.red);
    }
    const int minLevel=levels.empty()?0:*levels.begin();
    const int maxLevel=levels.empty()?0:*levels.rbegin();
    bool pass=levels.size()>=8 && maxLevel-minLevel>=18;
    printf("SOURCE_FEEDBACK_CYCLE levels=%zu min=%d max=%d PASS=%d\n",
        levels.size(),minLevel,maxLevel,pass);
    obs_set_output_source(0,nullptr);
    obs_source_dec_showing(source);
    obs_source_release(source);
    obs_shutdown();
    return pass?0:1;
}
