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
using namespace std::chrono_literals;
static const char* fixture_name(void*) {return "Transient input fixture";}
static uint32_t fixture_width(void*) {return W;}
static uint32_t fixture_height(void*) {return H;}
static void* fixture_create(obs_data_t*,obs_source_t*) {
    std::vector<uint8_t> pixels(W*H*4,0);for(size_t i=3;i<pixels.size();i+=4)pixels[i]=255;
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

struct Stats {int red=-1,green=-1,blue=-1;bool valid=false;};
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
                st.red=int(sample[0]);st.green=int(sample[1]);st.blue=int(sample[2]);
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



static obs_hotkey_id reset_key(obs_source_t* source,const char* name="effect.reset_history") {
    struct Key {obs_source_t* source;const char* name;obs_hotkey_id id=OBS_INVALID_HOTKEY_ID;};
    Key k{source,name};
    obs_enum_hotkeys([](void* data,obs_hotkey_id id,obs_hotkey_t* key){
        auto* k=static_cast<Key*>(data);
        if(obs_hotkey_get_registerer_type(key)==OBS_HOTKEY_REGISTERER_SOURCE &&
           obs_weak_source_references_source(static_cast<obs_weak_source_t*>(obs_hotkey_get_registerer(key)),k->source)&&
           !strcmp(obs_hotkey_get_name(key),k->name)) k->id=id;
        return true;
    },&k);
    return k.id;
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
    obs_register_source(&fixture_info);
    struct Instance {obs_source_t* effect;obs_source_t* visible;};
    auto make=[&](const char* name){
        auto* settings=obs_data_create();
        obs_data_set_int(settings,"preset",-1);
        obs_data_set_int(settings,"source_width",W);obs_data_set_int(settings,"source_height",H);
        obs_data_set_bool(settings,"sight_use_tracking",true);
        obs_data_set_double(settings,"param_gain",0.6);
        obs_data_set_string(settings,"shader_file",argv[4]);
        auto* effect=obs_source_create(filterLane?"hypershader_filter":"hypershader_source",name,settings,nullptr);
        obs_data_release(settings);
        auto* visible=effect;
        if(filterLane) {
            visible=obs_source_create("hs_transient_fixture",name,nullptr,nullptr);
            obs_source_filter_add(visible,effect);
        }
        obs_source_inc_showing(visible);
        return Instance{effect,visible};
    };
    auto a=make("Reset A"),b=make("Reset B");
    auto mature=[&](Instance& x){
        for(int i=0;i<70;i++){
            auto st=render(x.visible);
            if(st.valid&&st.red>=200&&abs(st.green-153)<=2)return true;
            std::this_thread::sleep_for(25ms);
        }
        return false;
    };
    // Shader installation is asynchronous; make the edit after the
    // actual compiled fixture is rendering, rather than testing its default.
    auto set_gain=[&](Instance& x){
        for(int i=0;i<60;i++){
            if(render(x.visible).red>0)break;
            std::this_thread::sleep_for(25ms);
        }
        auto* cfg=obs_source_get_settings(x.effect);
        obs_data_set_double(cfg,"param_gain",0.6);
        obs_source_update(x.effect,cfg);obs_data_release(cfg);
    };
    set_gain(a);set_gain(b);
    bool initial=mature(a)&&mature(b);
    auto proc=[&](){calldata_t cd;calldata_init(&cd);
        bool called=proc_handler_call(obs_source_get_proc_handler(a.effect),"hypershader_reset_history",&cd);
        calldata_free(&cd);return called;};
    auto check_reset=[&](const char* name,bool requested){
        auto first=render(a.visible);auto second=render(a.visible);auto peer=render(b.visible);
        bool good=requested&&first.valid&&first.red>=8&&first.red<=12&&second.red>=18&&second.red<=24&&
            abs(first.green-153)<=2&&peer.red>=200&&
            (strstr(argv[4],"graph")==nullptr||(first.blue==0&&abs(second.blue-26)<=2));
        printf("HISTORY_RESET %s requested=%d first=%d second=%d peer=%d green=%d graph_frame_rgb=%d,%d PASS=%d\n",
            name,requested,first.red,second.red,peer.red,first.green,first.blue,second.blue,good);
        return good;
    };
    bool procOk=check_reset("proc",proc());
    bool remature=mature(a);
    auto* props=obs_source_properties(a.effect);
    auto* group=props?obs_properties_get(props,"global_overrides_group"):nullptr;
    auto* button=group?obs_properties_get(obs_property_group_content(group),"reset_history"):nullptr;
    if(button)obs_property_button_clicked(button,a.effect);
    bool buttonOk=check_reset("button",button!=nullptr);
    if(props)obs_properties_destroy(props);
    remature&=mature(a);
    auto key=reset_key(a.effect),peerKey=reset_key(b.effect);
    bool hasKey=key!=OBS_INVALID_HOTKEY_ID&&peerKey!=OBS_INVALID_HOTKEY_ID&&key!=peerKey;
    obs_hotkey_enable_callback_rerouting(true);
    if(hasKey)obs_hotkey_trigger_routed_callback(key,false);
    bool releaseIgnored=render(a.visible).red>=200;
    if(hasKey){obs_hotkey_trigger_routed_callback(key,true);obs_hotkey_trigger_routed_callback(key,false);}
    bool keyOk=check_reset("hotkey",hasKey);
    bool cancelOk=false;
    if(hasKey&&mature(a)){
        auto cancel=reset_key(a.effect,"effect.cancel_history_reset");
        if(cancel!=OBS_INVALID_HOTKEY_ID){
            proc();
            obs_hotkey_trigger_routed_callback(cancel,true);
            obs_hotkey_trigger_routed_callback(cancel,false);
            cancelOk=render(a.visible).red>=200;
        }
    }
    bool saved=false;
    if(hasKey){
        obs_key_combination_t combo{};combo.key=OBS_KEY_F10;
        obs_hotkey_load_bindings(key,&combo,1);
        auto* data=obs_hotkeys_save_source(a.effect);
        auto* arr=obs_data_get_array(data,"effect.reset_history");
        saved=arr&&obs_data_array_count(arr)==1;
        if(arr)obs_data_array_release(arr);obs_data_release(data);
    }
    auto destroy=[&](Instance& x){obs_source_dec_showing(x.visible);
        if(filterLane){obs_source_filter_remove(x.visible,x.effect);obs_source_release(x.visible);}
        obs_source_remove(x.effect);obs_source_release(x.effect);};
    destroy(a);destroy(b);obs_wait_for_destroy_queue();obs_shutdown();
    bool pass=initial&&remature&&procOk&&buttonOk&&keyOk&&releaseIgnored&&saved&&cancelOk;
    printf("HISTORY_RESET_TOTAL lane=%s initial=%d release=%d saved=%d cancel=%d PASS=%d\n",argv[3],initial,releaseIgnored,saved,cancelOk,pass);
    return pass?0:1;
}
