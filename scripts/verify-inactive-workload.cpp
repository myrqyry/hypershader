#include <array>
#include <atomic>
#include <cmath>
#include "interop/sight_snapshot_v1.h"
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




static std::atomic<unsigned> reads{0};
static void read_tracking(void*,calldata_t* args){
    ++reads;
    auto* snap=static_cast<sight_snapshot_v1*>(calldata_ptr(args,"snapshot"));
    memset(snap,0,sizeof(*snap));
    snap->abi_version=SIGHT_SNAPSHOT_ABI_V1;snap->struct_size=sizeof(*snap);
    snap->kind=SIGHT_SNAPSHOT_FACE;snap->sequence=reads.load();
    snap->source_width=W;snap->source_height=H;snap->point_count=1;
    calldata_set_bool(args,"available",true);
}
static const char* reader_name(void*){return "Test Sight endpoint";}
static void* reader_create(obs_data_t*,obs_source_t* src){
    proc_handler_add(obs_source_get_proc_handler(src),
        "void sight_tracking_read_v1(string channel, int max_age_ms, ptr snapshot, out bool available)",read_tracking,nullptr);
    return new int(1);
}
static void dummy_destroy(void* data){delete static_cast<int*>(data);}
static void* pcm_create(obs_data_t*,obs_source_t*){return new int(1);}
static obs_source_info reader_info={
    .id="sight_tracking_receiver_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_VIDEO,
    .get_name=reader_name,.create=reader_create,.destroy=dummy_destroy
};
static obs_source_info pcm_info={
    .id="hs_workload_pcm",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_AUDIO,
    .get_name=reader_name,.create=pcm_create,.destroy=dummy_destroy
};
static bool audio_live(obs_source_t* effect){
    calldata_t args;calldata_init(&args);
    bool called=proc_handler_call(obs_source_get_proc_handler(effect),"hypershader_live_status",&args);
    bool live=called&&calldata_bool(&args,"audio");calldata_free(&args);return live;
}
int main(int argc,char**argv){
    if(argc!=4||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_audio_info ai{};ai.samples_per_sec=48000;ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai))return 3;
    obs_video_info vi={};vi.graphics_module="libobs-opengl";vi.fps_num=30;vi.fps_den=1;
    vi.base_width=W;vi.base_height=H;vi.output_width=W;vi.output_height=H;
    vi.output_format=VIDEO_FORMAT_RGBA;vi.scale_type=OBS_SCALE_DISABLE;
    if(obs_reset_video(&vi)!=OBS_VIDEO_SUCCESS)return 3;
    obs_register_source(&fixture_info);obs_register_source(&reader_info);obs_register_source(&pcm_info);
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    bool filterLane=!strcmp(argv[3],"filter");
    auto* pcm=obs_source_create("hs_workload_pcm","Workload PCM",nullptr,nullptr);
    obs_source_inc_showing(pcm);obs_set_output_source(1,pcm);
    auto* settings=obs_data_create();obs_data_set_int(settings,"preset",-1);
    obs_data_set_int(settings,"source_width",W);obs_data_set_int(settings,"source_height",H);
    obs_data_set_bool(settings,"sight_use_tracking",true);
    obs_data_set_bool(settings,"enable_sound",true);
    obs_data_set_string(settings,"audio_source","Workload PCM");
    obs_data_set_string(settings,"shader_file","shaders/root/workload.effect");
    auto* effect=obs_source_create(filterLane?"hypershader_filter":"hypershader_source","Workload",settings,nullptr);
    obs_data_release(settings);
    auto* visible=effect;
    if(filterLane){visible=obs_source_create("hs_transient_fixture","Workload input",nullptr,nullptr);obs_source_filter_add(visible,effect);}
    obs_source_inc_showing(visible); // preview/auxiliary visibility, no program output
    double phase=0;
    auto pump=[&](int n,bool draw,double freq){
        Stats st{};
        for(int i=0;i<n;i++){
            std::array<float,1024> wave{};
            for(float& x:wave){x=freq?0.75f*sin(phase):0;phase+=6.283185307*freq/48000.;}
            obs_source_audio frame{};frame.frames=wave.size();frame.samples_per_sec=48000;
            frame.speakers=SPEAKERS_STEREO;frame.format=AUDIO_FORMAT_FLOAT_PLANAR;frame.timestamp=os_gettime_ns();
            frame.data[0]=(const uint8_t*)wave.data();frame.data[1]=frame.data[0];obs_source_output_audio(pcm,&frame);
            if(draw)st=render(visible);
            std::this_thread::sleep_for(24ms);
        }
        return st;
    };
    pump(15,false,440);
    unsigned previewReads=reads.load();
    auto shown=pump(70,true,440);bool active=shown.red>20&&shown.green>200&&audio_live(effect);
    printf("WORKLOAD_PIXELS initial=%d,%d audio=%d reads=%u\n",shown.red,shown.green,audio_live(effect),reads.load());
    obs_source_dec_showing(visible);
    pump(50,false,880);unsigned before=reads.load();
    pump(45,false,880);unsigned hiddenReads=reads.load()-before;
    bool hiddenAudio=audio_live(effect);
    obs_source_inc_showing(visible);
    auto silent=pump(12,true,0);
    bool fresh=silent.red<=2;
    auto resumed=pump(45,true,440);bool resumedOk=resumed.red>20&&resumed.green>200&&audio_live(effect);
    obs_source_set_enabled(effect,false);pump(50,false,880);before=reads.load();
    pump(45,false,880);unsigned disabledReads=reads.load()-before;bool disabledAudio=audio_live(effect);
    obs_source_set_enabled(effect,true);auto enabled=pump(45,true,440);
    bool enabledOk=enabled.red>20&&enabled.green>200&&audio_live(effect);
    obs_source_dec_showing(visible);pump(50,false,0);
    auto manual=pump(45,true,440);bool manualOk=manual.red>20&&manual.green>200&&audio_live(effect);
    bool late=true;
    if(filterLane){
        obs_source_inc_showing(visible);
        pump(15,false,0);
        obs_source_filter_remove(visible,effect);
        pump(20,false,0);
        unsigned detached=reads.load();
        pump(15,false,0);
        late=reads.load()==detached;
        obs_source_filter_add(visible,effect);
        pump(15,false,0);
        late&=reads.load()>detached;
        obs_source_dec_showing(visible);
    }
    bool pass=active&&previewReads>0&&late&&hiddenReads==0&&!hiddenAudio&&fresh&&resumedOk&&disabledReads==0&&!disabledAudio&&enabledOk&&manualOk;
    printf("WORKLOAD lane=%s visible=%d hidden_reads=%u hidden_audio=%d fresh_silence=%d resumed=%d disabled_reads=%u disabled_audio=%d reenabled=%d direct=%d preview_reads=%u attach=%d PASS=%d\n",
        argv[3],active,hiddenReads,hiddenAudio,fresh,resumedOk,disabledReads,disabledAudio,enabledOk,manualOk,previewReads,late,pass);
    if(filterLane){obs_source_filter_remove(visible,effect);obs_source_release(visible);}
    obs_source_remove(effect);obs_source_release(effect);obs_set_output_source(1,nullptr);
    obs_source_dec_showing(pcm);obs_source_remove(pcm);obs_source_release(pcm);
    obs_wait_for_destroy_queue();obs_shutdown();return pass?0:1;
}
