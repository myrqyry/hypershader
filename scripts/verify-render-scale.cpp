#include <cmath>
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
constexpr uint32_t W=100,H=76;
using namespace std::chrono_literals;
static const char* fixture_name(void*) {return "Transient input fixture";}
static uint32_t fixture_width(void*) {return W;}
static uint32_t fixture_height(void*) {return H;}
static void* fixture_create(obs_data_t*,obs_source_t*) {
    std::vector<uint8_t> pixels(W*H*4,0);
    for(uint32_t y=0;y<H;y++)for(uint32_t x=0;x<W;x++){
        size_t i=(y*W+x)*4;pixels[i]=(x&1)?240:16;pixels[i+1]=(x&1)?64:32;pixels[i+3]=255;
    }
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

static thread_local bool measuring=false;
static std::set<std::pair<uint32_t,uint32_t>> targets;
extern "C" bool gs_texrender_begin(gs_texrender_t* t,uint32_t w,uint32_t h){
    using Fn=bool(*)(gs_texrender_t*,uint32_t,uint32_t);
    static auto real=reinterpret_cast<Fn>(dlsym(RTLD_NEXT,"gs_texrender_begin"));
    if(measuring)targets.emplace(w,h);
    return real(t,w,h);
}
struct Stats {int red=-1,green=-1,blue=-1,alpha=-1,adjacent=-1;bool valid=false;};
Stats render(obs_source_t* source) {
    Stats st;
    obs_enter_graphics();
    gs_set_cull_mode(GS_NEITHER);
    auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
    auto* stage=gs_stagesurface_create(W,H,GS_RGBA);
    if(target&&stage&&gs_texrender_begin(target,W,H)) {
        gs_projection_push();gs_matrix_push();gs_matrix_identity();
        gs_ortho(0,float(W),0,float(H),-100,100);
        measuring=true;obs_source_video_render(source);measuring=false;
        gs_matrix_pop();gs_projection_pop();
        gs_texrender_end(target);
        auto* tex=gs_texrender_get_texture(target);
        if(tex) {
            gs_stage_texture(stage,tex);
            uint8_t* pixels=nullptr;uint32_t pitch=0;
            if(gs_stagesurface_map(stage,&pixels,&pitch)) {
                uint8_t* sample=pixels+(H/2)*pitch+(W/2)*4;
                st.red=int(sample[0]);st.green=int(sample[1]);st.blue=int(sample[2]);st.alpha=int(sample[3]);st.adjacent=int(sample[4]);
                auto* corner=pixels+(H-2)*pitch+(W-2)*4;
                st.valid=abs(int(corner[0])-st.red)<=2&&abs(int(corner[1])-st.green)<=2&&corner[3]==255;
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
    if(argc!=5||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_video_info vi={};vi.graphics_module="libobs-opengl";vi.fps_num=30;vi.fps_den=1;
    vi.base_width=W;vi.base_height=H;vi.output_width=W;vi.output_height=H;
    vi.output_format=VIDEO_FORMAT_RGBA;vi.scale_type=OBS_SCALE_DISABLE;
    if(obs_reset_video(&vi)!=OBS_VIDEO_SUCCESS)return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    bool filterLane=!strcmp(argv[3],"filter");
    bool graph=strstr(argv[4],"graph")!=nullptr;
    bool feedback=strstr(argv[4],"feedback")!=nullptr;
    obs_register_source(&fixture_info);
    auto* settings=obs_data_create();
    obs_data_set_int(settings,"preset",-1);
    obs_data_set_int(settings,"source_width",W);obs_data_set_int(settings,"source_height",H);
    obs_data_set_bool(settings,"sight_use_tracking",true);
    obs_data_set_string(settings,"shader_file",argv[4]);
    auto* effect=obs_source_create(filterLane?"hypershader_filter":"hypershader_source","Scale probe",settings,nullptr);
    obs_data_release(settings);
    auto* visible=effect;
    if(filterLane){visible=obs_source_create("hs_transient_fixture","Scale input",nullptr,nullptr);obs_source_filter_add(visible,effect);}
    obs_source_inc_showing(visible);
    auto update=[&](int scale,double opacity){
        auto* cfg=obs_source_get_settings(effect);
        obs_data_set_int(cfg,"render_scale_percent",scale);
        obs_data_set_double(cfg,"compose_effect_opacity",opacity);
        obs_source_update(effect,cfg);obs_data_release(cfg);
    };
    bool pass=true;
    auto* props=obs_source_properties(effect);
    auto* group=props?obs_properties_get(props,"global_overrides_group"):nullptr;
    auto* list=group?obs_properties_get(obs_property_group_content(group),"render_scale_percent"):nullptr;
    bool ui=list&&obs_property_list_item_count(list)==4;
    if(ui){int i=0;for(int v:{25,50,75,100})ui&=obs_property_list_item_int(list,i++)==v;}
    if(props)obs_properties_destroy(props);
    pass&=ui;
    int previousScale=0;
    for(int scale:{100,75,50,25,100,123}){
        int effective=scale==123?100:scale;
        int rw=(W*effective+99)/100,rh=(H*effective+99)/100;
        int expectedRed=(int)std::lround(rw*255.0/W);
        int expectedGreen=(int)std::lround(rh*255.0/H);
        update(scale,1);
        targets.clear();Stats st{};int valid=0,firstBlue=-1;
        for(int i=0;i<60;i++){
            st=render(visible);
            if(st.valid&&abs(st.red-expectedRed)<=2&&abs(st.green-expectedGreen)<=2){
                if(!valid)firstBlue=st.blue;
                valid++;
            }
            if(valid>=3)break;
            std::this_thread::sleep_for(25ms);
        }
        bool correct=valid>=3&&obs_source_get_width(visible)==W&&obs_source_get_height(visible)==H;
        if(effective!=100)correct&=targets.count({rw,rh})!=0;
        if(graph)correct&=targets.count({(rw+1)/2,(rh+1)/2})!=0;
        if(graph||feedback){
            if(previousScale!=0&&previousScale!=effective)correct&=firstBlue>=4&&firstBlue<=6;
            if(previousScale==effective)correct&=firstBlue>=200&&firstBlue<=206;
            for(int i=0;i<45;i++)st=render(visible);
            correct&=st.blue>=200&&st.blue<=206;
        }
        previousScale=effective;
        printf("RENDER_SCALE lane=%s scale=%d expected=%dx%d pixel=%d,%d target=%zu valid=%d first_history=%d mature_history=%d PASS=%d\n",
               argv[3],scale,rw,rh,st.red,st.green,targets.count({rw,rh}),valid,firstBlue,st.blue,correct);
        pass&=correct;
    }
    update(25,0);
    int muted=0;
    for(int i=0;i<15;i++){
        auto st=render(visible);
        if(filterLane ? (abs(st.red-16)<=2&&abs(st.adjacent-240)<=2&&st.alpha==255) : (st.alpha==0))muted++;
        std::this_thread::sleep_for(25ms);
    }
    pass&=muted>=10;
    printf("RENDER_SCALE_TOTAL lane=%s graph=%d feedback=%d muted=%d ui=%d PASS=%d\n",argv[3],graph,feedback,muted,ui,pass);
    obs_source_dec_showing(visible);
    if(filterLane){obs_source_filter_remove(visible,effect);obs_source_release(visible);}
    obs_source_remove(effect);obs_source_release(effect);obs_wait_for_destroy_queue();obs_shutdown();
    return pass?0:1;
}
