// Loads the actual HyperShaderFX OBS module: native source playback, absent
// tracking passthrough, and camera-output feedback under isolated libobs.
#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <thread>
#include <chrono>
#include <vector>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <mutex>
#include "interop/sight_depth_v1.h"
using namespace std::chrono_literals;
// Mirror the real main-thread dispatch used by Sight's isolated smoke.
std::mutex ui_mutex;
std::vector<std::pair<obs_task_t, void*>> ui_tasks;
void queue_ui(obs_task_t task,void* arg,bool wait){
 if(!task)return;
 if(wait){std::fprintf(stderr,"Unexpected synchronous UI task\n");std::abort();}
 std::lock_guard<std::mutex> lock(ui_mutex);ui_tasks.emplace_back(task,arg);
}
void drain_ui(){
 std::vector<std::pair<obs_task_t,void*>> tasks;
 {std::lock_guard<std::mutex> lock(ui_mutex);tasks.swap(ui_tasks);}
 for(auto& t:tasks)t.first(t.second);
}
constexpr unsigned W=320,H=192;
using Frame=std::vector<uint8_t>;
struct Fixture{gs_texture_t* tex=nullptr;};
Frame pattern(int step){
 Frame f(W*H*4);
 for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++){
  auto i=(y*W+x)*4;
  bool stripe=x>=unsigned(10+(step%11)*17)&&x<unsigned(75+(step%11)*17)&&y>40&&y<150;
  f[i]=stripe?235:uint8_t(40+x*110/W);
  f[i+1]=stripe?116:uint8_t(25+y*150/H);
  f[i+2]=stripe?52:uint8_t(70+x*60/W);
  f[i+3]=255;
 }
 return f;
}
const char* name(void*){return "moving local image";}
unsigned width(void*){return W;} unsigned height(void*){return H;}
void* create(obs_data_t*,obs_source_t*){
 auto* f=new Fixture;auto pixels=pattern(0);const uint8_t* ptr=pixels.data();
 obs_enter_graphics();f->tex=gs_texture_create(W,H,GS_RGBA,1,&ptr,GS_DYNAMIC);obs_leave_graphics();
 return f;
}
void destroy(void* data){
 auto* f=(Fixture*)data;
 obs_enter_graphics();if(f->tex)gs_texture_destroy(f->tex);obs_leave_graphics();
 delete f;
}
void video_render(void* data,gs_effect_t*){auto* f=(Fixture*)data;if(f->tex)obs_source_draw(f->tex,0,0,W,H,false);}
obs_source_info fixture_info {
 .id="original_instrument_input",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_VIDEO,
 .get_name=name,.create=create,.destroy=destroy,.get_width=width,.get_height=height,
 .video_render=video_render
};
Frame capture(obs_source_t* src){
 drain_ui();Frame pixels;obs_enter_graphics();
 auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
 auto* stage=gs_stagesurface_create(W,H,GS_RGBA);
 if(target&&stage&&gs_texrender_begin(target,W,H)){
  gs_projection_push();gs_matrix_push();gs_matrix_identity();
  gs_ortho(0,float(W),0,float(H),-100,100);
  obs_source_video_render(src);
  gs_matrix_pop();gs_projection_pop();gs_texrender_end(target);
  gs_stage_texture(stage,gs_texrender_get_texture(target));
  uint8_t* ptr=nullptr;uint32_t stride=0;
  if(gs_stagesurface_map(stage,&ptr,&stride)){
   pixels.resize(W*H*4);
   for(unsigned y=0;y<H;y++)memcpy(pixels.data()+y*W*4,ptr+y*stride,W*4);
   gs_stagesurface_unmap(stage);
  }
 }
 if(stage)gs_stagesurface_destroy(stage);
 if(target)gs_texrender_destroy(target);
 obs_leave_graphics();return pixels;
}
size_t delta(const Frame& a,const Frame& b){
 if(a.empty()||b.empty()||a.size()!=b.size())return 0;
 size_t n=0;for(size_t i=0;i<a.size();i+=4)for(int j=0;j<3;j++)n+=abs(int(a[i+j])-int(b[i+j]))>5;
 return n;
}
// Real producer model + native filter chain. Model content is intentionally
// external; this fixture is a synthetic test image, not a depth estimator.
int main(int argc,char**argv){
 if(argc!=7)return 2;
 if(!obs_startup("en-US",nullptr,nullptr))return 3;
 obs_set_ui_task_handler(queue_ui);
 obs_video_info v{};v.graphics_module="libobs-opengl";v.fps_num=30;v.fps_den=1;
 v.base_width=v.output_width=W;v.base_height=v.output_height=H;
 v.output_format=VIDEO_FORMAT_RGBA;v.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&v)!=OBS_VIDEO_SUCCESS)return 4;
 obs_register_source(&fixture_info);
 obs_module_t *sight=nullptr,*hyper=nullptr;
 if(obs_open_module(&sight,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(sight)||
    obs_open_module(&hyper,argv[3],argv[4])!=MODULE_SUCCESS||!obs_init_module(hyper))return 5;
 auto* src=obs_source_create("original_instrument_input","camera",nullptr,nullptr);
 if(!src)return 6;
 obs_source_inc_showing(src);obs_set_output_source(0,src);
 auto baseline=capture(src);
 auto* settings=obs_data_create();
 obs_data_set_string(settings,"model_path",argv[5]);
 obs_data_set_int(settings,"view_mode",2);
 obs_data_set_int(settings,"inference_edge",252);
 obs_data_set_int(settings,"inference_cadence",4);
 auto* depth=obs_source_create("sight_depth_filter","Depth analysis",settings,nullptr);
 obs_data_release(settings);
 if(!depth)return 7;
 obs_source_filter_add(src,depth);
 settings=obs_data_create();
 obs_data_set_int(settings,"preset",-1);
 obs_data_set_string(settings,"shader_file",argv[6]);
 obs_data_set_bool(settings,"sight_use_tracking",true);
 obs_data_set_double(settings,"param_effect_mix",1);
 obs_data_set_double(settings,"param_key_strength",1.0);
 obs_data_set_double(settings,"param_ambient",.35);
 obs_data_set_double(settings,"param_fill_strength",0);
 obs_data_set_int(settings,"param_light_mode",0);
 obs_data_set_double(settings,"param_light_x",1);
 auto* relight=obs_source_create("hypershader_filter","Relight",settings,nullptr);
 obs_data_release(settings);
 if(!relight)return 8;
 obs_source_filter_add(src,relight);
 Frame current;
 auto wait=[&](auto predicate,int attempts=160){
    for(int i=0;i<attempts;i++){std::this_thread::sleep_for(40ms);current=capture(src);if(predicate())return true;}
    return false;
 };
 auto fresh=[&](){
    sight_depth_v1 snap{};snap.abi_version=SIGHT_DEPTH_ABI_V1;snap.struct_size=sizeof(snap);
    calldata_t args;calldata_init(&args);
    calldata_set_int(&args,"max_age_ms",1500);calldata_set_int(&args,"known_sequence",0);
    calldata_set_ptr(&args,"snapshot",&snap);
    bool available=false;
    proc_handler_call(obs_source_get_proc_handler(depth),"sight_depth_read_v1",&args);
    calldata_get_bool(&args,"available",&available);calldata_free(&args);
    return available&&snap.sequence&&snap.width&&snap.height;
 };
 bool lit=wait([&](){return fresh()&&delta(current,baseline)>1000;},250);
 auto right=current;
 auto update=[&](const char* key,double value){
    auto* cfg=obs_data_create();obs_data_set_double(cfg,key,value);obs_source_update(relight,cfg);obs_data_release(cfg);
 };
 update("param_light_x",0.0);
 bool moved=lit&&wait([&](){return fresh()&&delta(right,current)>500;});
 auto* props=obs_source_properties(relight);
 auto visible=[&](obs_properties_t* p,const char* key,bool expected){
    auto* prop=p?obs_properties_get(p,key):nullptr;
    return prop&&obs_property_visible(prop)==expected;
 };
 bool ui=props&&obs_properties_get(props,"param_light_mode")&&obs_properties_get(props,"param_key_color")&&
    obs_properties_get(props,"param_debug_view")&&visible(props,"param_shadow_radius",false)&&visible(props,"param_ao_radius",false);
 if(props)obs_properties_destroy(props);
 auto unoccluded=current;
 update("param_shadow_radius",96);
 update("param_shadow_strength",1);
 bool shadow=wait([&](){return fresh()&&delta(current,unoccluded)>30;});
 update("param_shadow_strength",0);
 bool shadow_off=wait([&](){return fresh()&&delta(current,unoccluded)<20;});
 update("param_ao_radius",60);
 update("param_ao_strength",1);
 bool ao=wait([&](){return fresh()&&delta(current,unoccluded)>30;});
 update("param_shadow_strength",1);
 props=obs_source_properties(relight);
 ui=ui&&visible(props,"param_shadow_radius",true)&&visible(props,"param_ao_radius",true);
 if(props)obs_properties_destroy(props);
 printf("SIGHT_RELIGHT_REAL_OCCLUSION shadow=%d shadow_off=%d ao=%d ui=%d\n",shadow,shadow_off,ao,ui);
 obs_source_set_enabled(depth,false);
 bool lost=wait([&](){return current.size()==baseline.size()&&delta(current,baseline)<2;});
 obs_source_set_enabled(depth,true);
 bool restored=wait([&](){return fresh()&&delta(current,baseline)>1000;});
 update("param_effect_mix",0);
 bool dry=wait([&](){return current.size()==baseline.size()&&delta(current,baseline)<2;});
 bool pass=lit&&moved&&ui&&lost&&restored&&dry&&shadow&&shadow_off&&ao;
 printf("SIGHT_RELIGHT_REAL_MODEL lit=%d moved=%d ui=%d lost=%d restored=%d dry=%d PASS=%d\n",lit,moved,ui,lost,restored,dry,pass);
 fflush(stdout);
 obs_set_output_source(0,nullptr);obs_source_dec_showing(src);drain_ui();
 obs_source_filter_remove(src,relight);obs_source_release(relight);drain_ui();
 obs_source_filter_remove(src,depth);obs_source_release(depth);obs_source_release(src);
 drain_ui();obs_wait_for_destroy_queue();drain_ui();obs_shutdown();
 return pass?0:1;
}
