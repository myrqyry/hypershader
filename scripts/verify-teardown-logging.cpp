#include <initializer_list>
#include <obs.h>
#include <obs-module.h>
#include <util/base.h>
#include <util/bmem.h>
#include <util/platform.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
using namespace std::chrono_literals;
static std::atomic<unsigned> starts{0},ends{0};
static void log_handler(int level,const char* format,va_list args,void*){
    if(level!=LOG_DEBUG)return;
    char msg[512];vsnprintf(msg,sizeof(msg),format,args);
    if(strstr(msg,"filter_destroy: START"))++starts;
    if(strstr(msg,"filter_destroy: END"))++ends;
}
int main(int argc,char**argv){
    if(argc!=3||!obs_startup("en-US","module-config",nullptr))return 2;
    obs_video_info vi{};vi.graphics_module="libobs-opengl";vi.fps_num=30;vi.fps_den=1;
    vi.base_width=96;vi.base_height=72;vi.output_width=96;vi.output_height=72;
    vi.output_format=VIDEO_FORMAT_RGBA;vi.scale_type=OBS_SCALE_DISABLE;
    if(obs_reset_video(&vi)!=OBS_VIDEO_SUCCESS)return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    auto* dir=obs_module_get_config_path(module,"");
    auto* file=obs_module_get_config_path(module,"hypershader_debug.log");
    os_mkdirs(dir);bfree(dir);
    if(os_file_exists(file)){bfree(file);return 5;}
    log_handler_t old=nullptr;void* param=nullptr;
    base_get_log_handler(&old,&param);base_set_log_handler(log_handler,nullptr);
    for(const char* id:{"hypershader_source","hypershader_filter"}){
        auto* cfg=obs_data_create();
        obs_data_set_bool(cfg,"sight_use_tracking",true);
        obs_data_set_int(cfg,"source_width",96);obs_data_set_int(cfg,"source_height",72);
        auto* src=obs_source_create(id,"Teardown probe",cfg,nullptr);
        obs_data_release(cfg);
        if(!src)return 6;
        std::this_thread::sleep_for(200ms);
        obs_source_remove(src);obs_source_release(src);
        obs_wait_for_destroy_queue();
    }
    // Source refs may be held by asynchronous shader publication. Wait for
    // both actual destructors, not merely the original reference release.
    for(int i=0;i<100&&ends.load()<2;i++)std::this_thread::sleep_for(20ms);
    obs_wait_for_destroy_queue();
    bool sidecar=os_file_exists(file);bfree(file);
    base_set_log_handler(old,param);
    bool pass=starts==2&&ends==2&&!sidecar;
    printf("TEARDOWN_LOG starts=%u ends=%u sidecar=%d PASS=%d\n",starts.load(),ends.load(),sidecar,pass);
    obs_shutdown();return pass?0:1;
}
