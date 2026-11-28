#include <dlfcn.h>
#include <fstream>
#include <filesystem>
#include <sys/stat.h>
#include <fcntl.h>
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





static std::atomic<unsigned> parentReads{0},leafReads{0};
extern "C" FILE* fopen(const char* path,const char* mode){
    using Fn=FILE*(*)(const char*,const char*);
    static auto real=reinterpret_cast<Fn>(dlsym(RTLD_NEXT,"fopen"));
    if(path&&mode&&mode[0]=='r'){
        if(strstr(path,"cache_parent.glsl"))++parentReads;
        if(strstr(path,"cache_leaf.glsl"))++leafReads;
    }
    return real(path,mode);
}
static void write(const std::string& path,const std::string& content){
    std::ofstream f(path,std::ios::binary|std::ios::trunc);f<<content;
}
int main(int argc,char**argv){
    if(argc!=3||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_video_info vi{};vi.graphics_module="libobs-opengl";vi.fps_num=30;vi.fps_den=1;
    vi.base_width=W;vi.base_height=H;vi.output_width=W;vi.output_height=H;
    vi.output_format=VIDEO_FORMAT_RGBA;vi.scale_type=OBS_SCALE_DISABLE;
    if(obs_reset_video(&vi)!=OBS_VIDEO_SUCCESS)return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    const std::string dir=argv[2],leaf=dir+"/stdlib/cache_leaf.glsl",parent=dir+"/stdlib/cache_parent.glsl";
    auto create=[&](const char* name,const char* file="shaders/root/cache_a.effect"){
        auto* cfg=obs_data_create();
        obs_data_set_string(cfg,"shader_file",file);
        obs_data_set_int(cfg,"source_width",W);obs_data_set_int(cfg,"source_height",H);
        obs_data_set_bool(cfg,"sight_use_tracking",true);
        auto* source=obs_source_create("hypershader_source",name,cfg,nullptr);
        obs_data_release(cfg);obs_source_inc_showing(source);
        return source;
    };
    auto wait=[&](obs_source_t* source,int expected,int blue=77){
        Stats st{};
        for(int i=0;i<100;i++){
            st=render(source);
            if(abs(int(st.red)-expected)<=2&&st.green>200&&abs(int(st.blue)-blue)<=2)return true;
            std::this_thread::sleep_for(20ms);
        }
        printf("CACHE_PIXEL expected=%d,%d actual=%d,%d,%d\n",expected,blue,st.red,st.green,st.blue);
        return false;
    };
    auto destroy=[](obs_source_t* source){obs_source_dec_showing(source);obs_source_remove(source);obs_source_release(source);};
    auto* a=create("first");
    bool initial=wait(a,51);
    unsigned firstParent=parentReads.load(),firstLeaf=leafReads.load();
    auto* b=create("second","shaders/root/cache_b.effect");
    bool repeat=wait(b,51);
    unsigned repeatParent=parentReads.load()-firstParent,repeatLeaf=leafReads.load()-firstLeaf;
    // Keep byte length and mtime unchanged; ctime/identity must detect edits.
    struct stat st{};stat(leaf.c_str(),&st);
    write(leaf,"// @default 0.7 @label \"Cache Gain\"\nuniform float cache_gain;\nfloat cache_value(){return 0.8;}\n");
    struct timespec times[2]={st.st_atim,st.st_mtim};
    utimensat(AT_FDCWD,leaf.c_str(),times,0);
    auto* c=create("edited");
    bool edit=wait(c,204,179),peer=wait(a,51);
    std::filesystem::remove(leaf);
    auto* missing=create("missing");
    std::this_thread::sleep_for(500ms);
    auto missingPixel=render(missing);
    bool removed=missingPixel.green<10;
    write(leaf,"// @default 0.4 @label \"Cache Gain\"\nuniform float cache_gain;\nfloat cache_value(){return 0.4;}\n");
    auto* restored=create("restored");
    bool recovery=wait(restored,102,102);
    // Replace the unchanged parent with a different dependency and use
    // an explicit include order which previously duplicated definitions.
    write(dir+"/stdlib/cache_other.glsl","// @default 0.6 @label \"Cache Gain\"\nuniform float cache_gain;\nfloat cache_value(){return 0.6;}\n");
    write(parent,"#include \"cache_other.glsl\"\nfloat cache_parent(){return cache_value();}\n");
    auto* changed=create("parent changed","shaders/root/cache_c.effect");
    bool nested=wait(changed,153,153);
    // A fresh, revisioned managed file uses the same compiler as Builder
    // Apply. The root remains uncached and dependency text is reusable.
    const std::string revision="11111111-1111-1111-1111-111111111111.effect";
    auto* managedDir=obs_module_get_config_path(module,"builder-applied");
    std::filesystem::create_directories(managedDir);
    std::filesystem::copy_file(dir+"/shaders/root/cache_a.effect",std::filesystem::path(managedDir)/revision,
        std::filesystem::copy_options::overwrite_existing);
    bfree(managedDir);
    const std::string logical="managed/builder-applied/"+revision;
    unsigned preManaged=parentReads.load();
    auto* managed=create("managed revision",logical.c_str());
    bool managedOk=wait(managed,153,153)&&parentReads.load()==preManaged;
    // More unique library keys than the entry cap should evict old keys
    // rather than grow forever. Each fixture stays below metadata guard cap.
    for(int group=0;group<2;group++){
        std::string root;
        for(int i=0;i<35;i++){
            const std::string name="cache_fill_"+std::to_string(group*35+i)+".glsl";
            write(dir+"/stdlib/"+name,"// unique cache key\n");
            root+="#include \""+name+"\"\n";
        }
        root+="uniform float4x4 ViewProj;\nstruct VertData{float4 pos:POSITION;float2 uv:TEXCOORD0;};\n"
              "VertData VS(VertData v){v.pos=mul(float4(v.pos.xyz,1),ViewProj);return v;}\n"
              "float4 Shade(VertData v):TARGET{return float4(0.2,1,0.3,1);}\n"
              "technique Draw{pass{vertex_shader=VS(v);pixel_shader=Shade(v);}}\n";
        const std::string filename="shaders/root/fill_"+std::to_string(group)+".effect";
        write(dir+"/"+filename,root);
        auto* filler=create("fill",filename.c_str());
        if(!wait(filler,51))nested=false;
        destroy(filler);
    }
    unsigned preEviction=parentReads.load();
    auto* evicted=create("evicted entry");
    bool bounded=wait(evicted,153,153)&&parentReads.load()>preEviction;
    bool pass=initial&&repeat&&firstParent>0&&firstLeaf>0&&repeatParent==0&&repeatLeaf==0&&edit&&peer&&removed&&recovery&&nested&&managedOk&&bounded;
    printf("STDLIB_CACHE initial=%d repeated=%d cold_parent=%u cold_leaf=%u warm_parent=%u warm_leaf=%u edited=%d peer=%d deleted=%d restored=%d nested=%d managed=%d eviction=%d PASS=%d\n",initial,repeat,firstParent,firstLeaf,repeatParent,repeatLeaf,edit,peer,removed,recovery,nested,managedOk,bounded,pass);
    for(auto* src:{a,b,c,missing,restored,changed,managed,evicted})destroy(src);
    obs_wait_for_destroy_queue();obs_shutdown();return pass?0:1;
}
