#include <dlfcn.h>
#include <atomic>
#include <mutex>
#include <utility>
#include <cstdlib>
#include <cstdint>
#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <cstdio>
#include <thread>
#include <chrono>
#include <vector>
#include <fstream>
#include <cstring>
using namespace std::chrono_literals;
constexpr unsigned W=256,H=256;
// Native Linux test: observe real libobs staging calls, not a fake model.
static thread_local bool count_capture=false;
static thread_local unsigned captures=0;
static std::atomic<unsigned> allCaptures{0};
// Linux GL driver test: force a bounded number of zero-timeout polls to
// report NOT_READY, then forward to the real GL implementation.
static std::atomic<unsigned> forceTimeouts{0}, delayedPolls{0}, readyPolls{0}, prematureMaps{0}, waitingPolls{0}, discardedFences{0};
static std::atomic<bool> fenceReady{false};
extern "C" unsigned glClientWaitSync(void* sync,unsigned flags,uint64_t timeout){
 using Fn=unsigned(*)(void*,unsigned,uint64_t);
 static void* gl=dlopen("libOpenGL.so.0",RTLD_LAZY|RTLD_LOCAL);
 static auto real=gl?reinterpret_cast<Fn>(dlsym(gl,"glClientWaitSync")):nullptr;
 if(!real)return 0x911d; // GL_WAIT_FAILED
 if(timeout!=0) waitingPolls.fetch_add(1);
 unsigned remaining=forceTimeouts.load();
 if(remaining&&forceTimeouts.compare_exchange_strong(remaining,remaining-1)){
    fenceReady.store(false);delayedPolls.fetch_add(1);return 0x911b; // GL_TIMEOUT_EXPIRED
 }
 const auto result=real(sync,flags,timeout);
 bool ready=result==0x911a||result==0x911c;
 fenceReady.store(ready);
 if(ready)readyPolls.fetch_add(1);
 return result;
}
extern "C" void glDeleteSync(void* sync){
 using Fn=void(*)(void*);
 static void* gl=dlopen("libOpenGL.so.0",RTLD_LAZY|RTLD_LOCAL);
 static auto real=gl?reinterpret_cast<Fn>(dlsym(gl,"glDeleteSync")):nullptr;
 if(!fenceReady.load())discardedFences.fetch_add(1);
 if(real)real(sync);
}
extern "C" bool gs_stagesurface_map(gs_stagesurf_t* surface,uint8_t** pixels,uint32_t* pitch){
 using Fn=bool(*)(gs_stagesurf_t*,uint8_t**,uint32_t*);
 static auto real=reinterpret_cast<Fn>(dlsym(RTLD_NEXT,"gs_stagesurface_map"));
 if(gs_stagesurface_get_width(surface)==192&&gs_stagesurface_get_height(surface)==192&&
    std::getenv("HS_MP_TEST_EXPECT_FENCE")&&!fenceReady.load())
    prematureMaps.fetch_add(1);
 return real(surface,pixels,pitch);
}

static std::mutex heldMutex;
static bool holdResults=false;
static std::vector<std::pair<obs_task_t,void*>> heldResults;
static std::atomic<unsigned> heldCount{0};
using QueueFn=void(*)(enum obs_task_type,obs_task_t,void*,bool);
static QueueFn realQueue(){
 static auto real=reinterpret_cast<QueueFn>(dlsym(RTLD_NEXT,"obs_queue_task"));
 return real;
}
extern "C" void obs_queue_task(enum obs_task_type type,obs_task_t task,void*param,bool wait){
 if(type==OBS_TASK_GRAPHICS&&!wait){
  std::lock_guard<std::mutex> lock(heldMutex);
  Dl_info info{};
  if(holdResults&&dladdr(reinterpret_cast<void*>(task),&info)&&info.dli_fname&&
      std::strstr(info.dli_fname,"hypershader.so")){
   heldResults.emplace_back(task,param);
   heldCount.fetch_add(1);
   return;
  }
 }
 realQueue()(type,task,param,wait);
}
extern "C" void gs_stage_texture(gs_stagesurf_t* dst,gs_texture_t* src){
 using Fn=void(*)(gs_stagesurf_t*,gs_texture_t*);
 static auto real=reinterpret_cast<Fn>(dlsym(RTLD_NEXT,"gs_stage_texture"));
 if(gs_stagesurface_get_width(dst)==192&&gs_stagesurface_get_height(dst)==192){
    allCaptures.fetch_add(1);
    fenceReady.store(false);
    if(count_capture) ++captures;
 }
 real(dst,src);
}

struct Fixture {gs_texture_t *texture=nullptr;};
Fixture *active=nullptr;
const char* fixture_name(void*){return "MediaPipe face fixture";}
void* fixture_create(obs_data_t*,obs_source_t*){
 auto* f=new Fixture();
 obs_enter_graphics();
 f->texture=gs_texture_create(W,H,GS_RGBA,1,nullptr,GS_DYNAMIC);
 obs_leave_graphics();active=f;return f;
}
void fixture_destroy(void*p){
 auto* f=(Fixture*)p;
 obs_enter_graphics();
 if(f->texture)gs_texture_destroy(f->texture);
 obs_leave_graphics();active=nullptr;delete f;
}
uint32_t fixture_width(void*){return W;}
uint32_t fixture_height(void*){return H;}
void fixture_render(void*p,gs_effect_t*){auto*f=(Fixture*)p; if(f->texture)obs_source_draw(f->texture,0,0,W,H,false);}
obs_source_info fixture_info={
 .id="mp_audit_fixture",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_VIDEO,
 .get_name=fixture_name,.create=fixture_create,.destroy=fixture_destroy,
 .get_width=fixture_width,.get_height=fixture_height,.video_render=fixture_render
};
static unsigned last_bright_pixels=0;
float render(obs_source_t*source){
 float sample=-1;
 obs_enter_graphics();
 auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
 auto* stage=gs_stagesurface_create(W,H,GS_RGBA);
 if(target&&stage&&gs_texrender_begin(target,W,H)){
    gs_projection_push();gs_matrix_push();gs_matrix_identity();
    gs_ortho(0,float(W),0,float(H),-100,100);
    struct vec4 clear={0,0,0,0};
    gs_clear(GS_CLEAR_COLOR,&clear,0,0);
    obs_source_video_render(source);
    gs_matrix_pop();gs_projection_pop();gs_texrender_end(target);
    if(auto* tex=gs_texrender_get_texture(target)){
      gs_stage_texture(stage,tex);
      uint8_t* pix=nullptr;uint32_t pitch=0;
      if(gs_stagesurface_map(stage,&pix,&pitch)){
        sample=float(pix[(H/2)*pitch+(W/2)*4])/255.0f;
        last_bright_pixels=0;
        for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++){
          const uint8_t *pixel=pix+y*pitch+x*4;
          if(pixel[0]>25 || pixel[1]>25 || pixel[2]>25) last_bright_pixels++;
        }
        gs_stagesurface_unmap(stage);
      }
    }
 }
 if(stage)gs_stagesurface_destroy(stage);
 if(target)gs_texrender_destroy(target);
 obs_leave_graphics();return sample;
}
int main(int argc,char**argv){
 if(argc!=5)return 2;
 std::ifstream input(argv[4],std::ios::binary);
 std::vector<uint8_t> face(W*H*4);
 if(!input.read(reinterpret_cast<char*>(face.data()),face.size()))return 3;
 if(!obs_startup("en-US",nullptr,nullptr))return 4;
 obs_video_info video{}; video.graphics_module="libobs-opengl";video.fps_num=30;video.fps_den=1;
 video.base_width=W;video.base_height=H;video.output_width=W;video.output_height=H;
 video.output_format=VIDEO_FORMAT_RGBA;video.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&video)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 5;}
 obs_register_source(&fixture_info);
 obs_module_t* module=nullptr;
 if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS || !obs_init_module(module)){
    obs_shutdown();return 6;
 }
 auto* videoSource=obs_source_create("mp_audit_fixture","Public face input",nullptr,nullptr);
 auto* opts=obs_data_create();obs_data_set_string(opts,"shader_file",argv[3]);
 obs_data_set_int(opts,"mp_controlnet_model",2);
 if(std::getenv("HS_MP_TEST_COMPOSITE"))
    obs_data_set_double(opts,"compose_base_opacity",0.0);
 auto* filter=obs_source_create("hypershader_filter","Face GPU readback",opts,nullptr);
 obs_data_release(opts);
 if(!videoSource||!filter){obs_shutdown();return 7;}
 obs_source_filter_add(videoSource,filter);obs_source_inc_showing(videoSource);
 obs_set_output_source(0,videoSource);
 std::vector<uint8_t> blank(W*H*4,0);
 for(size_t i=3;i<blank.size();i+=4)blank[i]=255;
 obs_enter_graphics();gs_texture_set_image(active->texture,face.data(),W*4,false);obs_leave_graphics();
 const bool expectFence=std::getenv("HS_MP_TEST_EXPECT_FENCE")!=nullptr;
 // Hold GPU readiness longer than the bounded capture lifetime, so
 // the stale fence must be destroyed and a new capture can recover.
 if(expectFence)forceTimeouts.store(16);
 float initial=0,faceLast=0;
 bool modeUi=false;
 for(int i=0;i<90;i++){float x=render(videoSource);if(i==20)initial=x;if(i>70)faceLast=x;std::this_thread::sleep_for(35ms);}
 obs_enter_graphics();gs_texture_set_image(active->texture,blank.data(),W*4,false);obs_leave_graphics();
 // Hold actual model result uploads while OBS keeps rendering. This
 // deliberately creates downstream backpressure, not simulated inference.
 {std::lock_guard<std::mutex> lock(heldMutex);holdResults=true;}
 for(int i=0;i<50&&heldCount.load()==0;i++){
    render(videoSource);
    std::this_thread::sleep_for(35ms);
 }
 const unsigned stagedBefore=allCaptures.load();
 for(int i=0;i<8;i++){
    render(videoSource);
    std::this_thread::sleep_for(35ms);
 }
 const unsigned extraStages=allCaptures.load()-stagedBefore;
 unsigned results=0;
 std::vector<std::pair<obs_task_t,void*>> release;
 {std::lock_guard<std::mutex> lock(heldMutex);
    holdResults=false;results=heldCount.load();release.swap(heldResults);}
 for(auto&entry:release) realQueue()(OBS_TASK_GRAPHICS,entry.first,entry.second,false);
 const bool backpressure=results==1&&extraStages==0;
 printf("MP_CAPTURE_BACKPRESSURE held_results=%u extra_stages=%u PASS=%d\n",results,extraStages,backpressure);
 // Six draws at one video timestamp must not queue six identical
 // inference captures. The graphics lock prevents an OBS video tick
 // from interleaving; explicitly verify the timestamp remained stable.
 obs_enter_graphics();
 const uint64_t frame=obs_get_video_frame_time();
 count_capture=true;
 for(int i=0;i<6;i++) render(videoSource);
 count_capture=false;
 const bool sameFrame=frame==obs_get_video_frame_time();
 obs_leave_graphics();
 const bool admitted=sameFrame&&captures<=1;
 printf("MP_CAPTURE_ADMISSION draws=6 same_frame=%d stages=%u PASS=%d\n",sameFrame,captures,admitted);
 float blankLast=0;
 unsigned blankBright=0;
 for(int i=0;i<90;i++){
    float x=render(videoSource);
    if(i>70){blankLast=x;blankBright=last_bright_pixels;}
    std::this_thread::sleep_for(35ms);
 }
 auto* properties=obs_source_properties(filter);
 auto* modes=properties?obs_properties_get(properties,"mp_controlnet_model"):nullptr;
 if (modes) {
    modeUi=obs_property_list_item_disabled(modes,2)&&
        obs_property_list_item_disabled(modes,3)&&
        obs_property_list_item_disabled(modes,4)&&
        obs_property_list_item_disabled(modes,5)&&
        !obs_property_list_item_disabled(modes,1);
 }
 if(properties)obs_properties_destroy(properties);
 // The face fixture is centered in its native 256x256 frame. Capturing
 // only its upper-left 192x192 corner shifts the detected nose far right.
 const bool fenceWorks=!expectFence||(delayedPolls.load()==16&&discardedFences.load()>0&&readyPolls.load()>0&&
      prematureMaps.load()==0&&waitingPolls.load()==0);
 printf("MP_CAPTURE_FENCE required=%d delayed=%u ready=%u premature_maps=%u blocking_polls=%u expired_fences=%u PASS=%d\n",
        expectFence,delayedPolls.load(),readyPolls.load(),prematureMaps.load(),waitingPolls.load(),discardedFences.load(),fenceWorks);
 bool pass=admitted&&backpressure&&fenceWorks&&faceLast>0.25f&&faceLast<0.75f&&blankLast<0.05f&&blankBright<10&&modeUi;
 printf("MP_FILTER_GPU initial=%.4f face=%.4f blank=%.4f blank_bright=%u mode_ui=%d PASS=%d\n",initial,faceLast,blankLast,blankBright,modeUi,pass);
 obs_set_output_source(0,nullptr);

 obs_source_dec_showing(videoSource);
 obs_source_filter_remove(videoSource,filter);
 obs_source_release(filter);obs_source_release(videoSource);
 obs_shutdown();
 return pass?0:1;
}
