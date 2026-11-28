#include <dlfcn.h>
#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <util/platform.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <mutex>
constexpr uint32_t W=240,H=136;
static thread_local bool measuring=false;
static thread_local int failAt=0;
static thread_local int failed=0;
extern "C" bool gs_texrender_begin(gs_texrender_t* target,uint32_t w,uint32_t h){
    using Fn=bool(*)(gs_texrender_t*,uint32_t,uint32_t);
    static auto real=reinterpret_cast<Fn>(dlsym(RTLD_NEXT,"gs_texrender_begin"));
    if(measuring && failAt>0 && --failAt==0){++failed;return false;}
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

struct Stats {int red=-1,green=-1,alpha=-1;bool valid=false;};
Stats render(obs_source_t* source) {
    Stats st;
    obs_enter_graphics();
    gs_set_cull_mode(GS_NEITHER);
    auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
    auto* stage=gs_stagesurface_create(W,H,GS_RGBA);
    if(target&&stage&&gs_texrender_begin(target,W,H)) {
        gs_projection_push();gs_matrix_push();gs_matrix_identity();
        gs_ortho(0,float(W),0,float(H),-100,100);
        struct vec4 clear;vec4_zero(&clear);gs_clear(GS_CLEAR_COLOR,&clear,0,0);
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
                st.red=int(sample[0]);st.green=int(sample[1]);st.alpha=int(sample[3]);
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



struct UiTask {obs_task_t task;void* param;};
static std::mutex uiMutex;
static std::vector<UiTask> uiTasks;
static void uiHandler(obs_task_t task,void* param,bool wait){
    if(wait){task(param);return;}
    std::lock_guard<std::mutex> lock(uiMutex);uiTasks.push_back({task,param});
}
static void drainUi(){
    std::vector<UiTask> pending;
    {std::lock_guard<std::mutex> lock(uiMutex);pending.swap(uiTasks);}
    for(auto t:pending)t.task(t.param);
}
int main(int argc,char**argv){
    if(argc!=3||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_set_ui_task_handler(uiHandler);
    obs_video_info vi{};vi.graphics_module="libobs-opengl";vi.fps_num=30;vi.fps_den=1;
    vi.base_width=W;vi.base_height=H;vi.output_width=W;vi.output_height=H;
    vi.output_format=VIDEO_FORMAT_RGBA;vi.scale_type=OBS_SCALE_DISABLE;
    if(obs_reset_video(&vi)!=OBS_VIDEO_SUCCESS)return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    obs_register_source(&fixture_info);
    struct Lane {obs_source_t* effect;obs_source_t* visible;};
    auto create=[&](const char* name,bool filter,int scale=100){
        auto* cfg=obs_data_create();
        obs_data_set_int(cfg,"source_width",W);obs_data_set_int(cfg,"source_height",H);
        obs_data_set_int(cfg,"render_scale_percent",scale);
        obs_data_set_string(cfg,"shader_file","shaders/root/graph.effect");
        auto* effect=obs_source_create(filter?"hypershader_filter":"hypershader_source",name,cfg,nullptr);
        obs_data_release(cfg);
        auto* visible=effect;
        if(filter){visible=obs_source_create("hs_transient_fixture","White input",nullptr,nullptr);obs_source_filter_add(visible,effect);}
        obs_source_inc_showing(visible);
        return Lane{effect,visible};
    };
    auto update=[](Lane l,int scale,const char* shader=nullptr){
        auto* cfg=obs_source_get_settings(l.effect);
        obs_data_set_int(cfg,"render_scale_percent",scale);
        if(shader)obs_data_set_string(cfg,"shader_file",shader);
        obs_source_update(l.effect,cfg);obs_data_release(cfg);
        std::this_thread::sleep_for(160ms);
    };
    auto destroy=[](Lane l){
        obs_source_dec_showing(l.visible);
        if(l.visible!=l.effect){obs_source_filter_remove(l.visible,l.effect);obs_source_release(l.visible);}
        auto* weak=obs_source_get_weak_source(l.effect);
        obs_source_remove(l.effect);obs_source_release(l.effect);
        for(int i=0;i<30&&!obs_weak_source_expired(weak);i++){drainUi();obs_wait_for_destroy_queue();std::this_thread::sleep_for(10ms);}
        bool expired=obs_weak_source_expired(weak);obs_weak_source_release(weak);obs_wait_for_destroy_queue();
        return expired;
    };
    auto active=[](Stats s){return s.valid&&s.alpha>240&&s.green<220&&s.red>0;};
    auto white=[](Stats s){return s.valid&&s.red>245&&s.green>245&&s.alpha>245;};
    auto empty=[](Stats s){return s.valid&&s.alpha==0;};
    auto wait=[&](Lane l,auto predicate){
        for(int i=0;i<60;i++){
            Stats s=render(l.visible);
            if(predicate(s))return true;
            std::this_thread::sleep_for(20ms);
        }
        return false;
    };
    auto a=create("A source",false);
    bool admitted=wait(a,active);
    auto b=create("B filter",true);
    // Wait until compiled, not just the precompile passthrough.
    std::this_thread::sleep_for(400ms);
    bool denied=true;
    for(int i=0;i<8;i++)denied&=white(render(b.visible));
    update(a,50);bool reduced=wait(a,active);
    update(b,50);bool sharing=wait(b,active)&&wait(a,active);
    update(a,100);bool growRejected=wait(a,empty)&&wait(b,active);
    bool destroyedB=destroy(b);
    bool recovered=wait(a,active);
    auto c=create("C source",false);
    std::this_thread::sleep_for(400ms);
    bool deniedSource=true;for(int i=0;i<8;i++)deniedSource&=empty(render(c.visible));
    update(a,100,"shaders/root/plain.effect");
    bool swapReleased=wait(a,[](Stats s){return s.valid&&s.green>100&&s.green<160&&s.alpha>240;})&&wait(c,active);
    destroy(a);destroy(c);
    auto d=create("D after teardown",false);
    bool teardown=wait(d,active);destroy(d);
    auto e=create("E failure",false);
    std::this_thread::sleep_for(400ms);
    failAt=2;
    auto broken=render(e.visible);
    bool injected=failed==1&&empty(broken);
    failAt=0;
    auto f=create("F after failure",false);
    bool failureReleased=wait(f,active)&&wait(e,empty);
    destroy(e);destroy(f);drainUi();
    bool pass=admitted&&denied&&reduced&&sharing&&growRejected&&recovered&&deniedSource&&swapReleased&&teardown&&destroyedB&&injected&&failureReleased;
    printf("AGGREGATE admitted=%d filter_denied=%d reduced=%d shared=%d growth_denied=%d recovered=%d source_denied=%d swap_released=%d teardown=%d destroyed=%d injected=%d failure_released=%d PASS=%d\n",admitted,denied,reduced,sharing,growRejected,recovered,deniedSource,swapReleased,teardown,destroyedB,injected,failureReleased,pass);
    obs_shutdown();return pass?0:1;
}
