#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdlib>
using namespace std::chrono_literals;
using Frame=std::vector<uint8_t>;
static bool draw(obs_source_t* source,Frame& frame,unsigned w,unsigned h){
    obs_enter_graphics();
    auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
    auto* stage=gs_stagesurface_create(w,h,GS_RGBA);
    bool ok=false;
    if(target&&stage&&gs_texrender_begin(target,w,h)){
        gs_projection_push();gs_matrix_push();gs_matrix_identity();gs_ortho(0,float(w),0,float(h),-100,100);
        obs_source_video_render(source);
        gs_matrix_pop();gs_projection_pop();gs_texrender_end(target);
        gs_stage_texture(stage,gs_texrender_get_texture(target));
        uint8_t* bytes=nullptr;uint32_t pitch=0;
        if(gs_stagesurface_map(stage,&bytes,&pitch)){
            if(pitch>=w*4){frame.resize(w*h*4);for(unsigned y=0;y<h;y++)memcpy(frame.data()+y*w*4,bytes+y*pitch,w*4);ok=true;}
            gs_stagesurface_unmap(stage);
        }
    }
    if(stage)gs_stagesurface_destroy(stage);
    if(target)gs_texrender_destroy(target);
    obs_leave_graphics();return ok;
}
static size_t difference(const Frame& a,const Frame& b){
    if(a.empty()||a.size()!=b.size())return 0;
    size_t n=0;for(size_t i=0;i<a.size();i++)n+=std::abs(int(a[i])-int(b[i]))>3;
    return n;
}
static bool load(const char* lib,const char* data){
    obs_module_t* module=nullptr;
    return obs_open_module(&module,lib,data)==MODULE_SUCCESS&&obs_init_module(module);
}
int main(int argc,char**argv){
    // Sight module/data, HyperShader module/data, image module/data,
    // gesture model, runtime, fixture and effect.
    if(argc!=11||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_video_info v{};v.graphics_module="libobs-opengl";v.fps_num=30;v.fps_den=1;
    v.base_width=v.output_width=640;v.base_height=v.output_height=360;
    v.output_format=VIDEO_FORMAT_RGBA;v.scale_type=OBS_SCALE_DISABLE;
    if(obs_reset_video(&v)!=OBS_VIDEO_SUCCESS)return 3;
    if(!load(argv[1],argv[2])||!load(argv[3],argv[4])||!load(argv[5],argv[6]))return 4;
    auto* d=obs_data_create();obs_data_set_string(d,"file",argv[9]);
    auto* source=obs_source_create("image_source","Ripple fixture",d,nullptr);obs_data_release(d);
    if(!source)return 5;
    const auto w=obs_source_get_base_width(source),h=obs_source_get_base_height(source);
    Frame baseline,current,previous;
    if(!w||!h||!draw(source,baseline,w,h))return 6;
    d=obs_data_create();
    obs_data_set_string(d,"tracking_mode","hand");
    obs_data_set_string(d,"hand_model_path",argv[7]);obs_data_set_string(d,"hand_mediapipe_runtime",argv[8]);
    obs_data_set_string(d,"hand_signal_channel","ripples/test");obs_data_set_bool(d,"hand_draw_overlay",false);
    obs_data_set_int(d,"hand_inference_cadence",1);
    auto* producer=obs_source_create("sight_unified_tracking_filter","Hand inference",d,nullptr);obs_data_release(d);
    d=obs_data_create();obs_data_set_string(d,"shader_file",argv[10]);
    obs_data_set_bool(d,"sight_use_tracking",true);obs_data_set_string(d,"sight_tracking_channel","ripples/test");
    obs_data_set_double(d,"touch_strength",2.75);obs_data_set_double(d,"touch_radius",.31);
    obs_data_set_double(d,"touch_speed",.8);obs_data_set_double(d,"touch_waves",2.0);
    auto* filter=obs_source_create("hypershader_filter","Hand ripple shader",d,nullptr);obs_data_release(d);
    if(!producer||!filter)return 7;
    // No producer: no distortion. Then start the actual native inference.
    obs_source_filter_add(source,filter);obs_source_inc_showing(source);obs_set_output_source(0,source);
    bool absent=false;
    for(int i=0;i<20;i++){std::this_thread::sleep_for(50ms);if(draw(source,current,w,h)&&difference(baseline,current)==0)absent=true;}
    obs_source_filter_remove(source,filter);
    obs_source_filter_add(source,producer);obs_source_filter_add(source,filter);
    unsigned active=0,moving=0;size_t maximum=0;
    for(int i=0;i<90;i++){
        std::this_thread::sleep_for(65ms);
        if(!draw(source,current,w,h))break;
        size_t diff=difference(baseline,current),step=difference(previous,current);
        active+=diff>250;moving+=step>250;maximum=std::max(maximum,diff);previous=current;
    }
    obs_source_filter_remove(source,producer);obs_source_release(producer);
    std::this_thread::sleep_for(850ms);
    bool expired=false;
    for(int i=0;i<20;i++){
        std::this_thread::sleep_for(50ms);
        if(draw(source,current,w,h)&&difference(baseline,current)==0){expired=true;break;}
    }
    bool pass=absent&&active>=12&&moving>=12&&expired;
    printf("HYPERSHADER_SIGHT_RIPPLES_REAL absent=%d active=%u evolving=%u diff=%zu expired=%d pass=%d\n",absent,active,moving,maximum,expired,pass);
    obs_set_output_source(0,nullptr);obs_source_dec_showing(source);
    obs_source_filter_remove(source,filter);obs_source_release(filter);obs_source_release(source);
    obs_wait_for_destroy_queue();obs_shutdown();return pass?0:1;
}
