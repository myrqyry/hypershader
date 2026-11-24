// Real libobs/OpenGL verification for the native CRT power transition.
#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <vector>
#include <cstdio>
#include <string>
#include <thread>
#include <chrono>
#include <iostream>
#include <cstring>
#include <cmath>
#include <mutex>
using namespace std::chrono_literals;
constexpr unsigned W=240,H=136;
using Frame=std::vector<uint8_t>;
struct TestSource {gs_texture_t* texture=nullptr;};
TestSource* active=nullptr;
Frame make_frame(int x) {
 Frame pixels(W*H*4);
 for(unsigned y=0;y<H;y++)for(unsigned z=0;z<W;z++){
  size_t i=4*(y*W+z);
  bool moving=z>=unsigned(x)&&z<unsigned(x+48)&&y>=33&&y<111;
  pixels[i]=moving?230:uint8_t(25+(z*3+y)%105);
  pixels[i+1]=moving?45:uint8_t(20+(z+y)%83);
  pixels[i+2]=moving?12:uint8_t(28+(y*3+z)%96);
  pixels[i+3]=255;
 }
 return pixels;
}
const char* source_name(void*){return "CRT transition fixture";}
uint32_t source_width(void*){return W;}
uint32_t source_height(void*){return H;}
void* source_create(obs_data_t*,obs_source_t*){
 auto* s=new TestSource;auto f=make_frame(12);const uint8_t* pixels=f.data();
 obs_enter_graphics();s->texture=gs_texture_create(W,H,GS_RGBA,1,&pixels,GS_DYNAMIC);obs_leave_graphics();
 active=s;return s;
}
void source_destroy(void* data){
 auto* s=(TestSource*)data;obs_enter_graphics();if(s->texture)gs_texture_destroy(s->texture);obs_leave_graphics();
 active=nullptr;delete s;
}
void source_render(void* data,gs_effect_t*){auto* s=(TestSource*)data;if(s->texture)obs_source_draw(s->texture,0,0,W,H,false);}
obs_source_info source_info{
 .id="hyper_crt_test_source",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_VIDEO,
 .get_name=source_name,.create=source_create,.destroy=source_destroy,
 .get_width=source_width,.get_height=source_height,.video_render=source_render,
};
Frame render(obs_source_t* source) {
 Frame frame;obs_enter_graphics();auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
 auto* stage=gs_stagesurface_create(W,H,GS_RGBA);
 if(target&&stage&&gs_texrender_begin(target,W,H)){
  gs_projection_push();gs_matrix_push();gs_matrix_identity();gs_ortho(0,float(W),0,float(H),-100,100);
  obs_source_video_render(source);gs_matrix_pop();gs_projection_pop();gs_texrender_end(target);
  if(auto* texture=gs_texrender_get_texture(target)){
   gs_stage_texture(stage,texture);uint8_t* data=nullptr;uint32_t pitch=0;
   if(gs_stagesurface_map(stage,&data,&pitch)){
    frame.resize(W*H*4);
    for(unsigned y=0;y<H;y++)std::memcpy(frame.data()+y*W*4,data+y*pitch,W*4);
    gs_stagesurface_unmap(stage);
   }
  }
 }
 if(stage)gs_stagesurface_destroy(stage);
 if(target)gs_texrender_destroy(target);
 obs_leave_graphics();
 return frame;
}
uint64_t diff(const Frame&a,const Frame&b){
 if(a.size()!=b.size())return UINT64_MAX;
 uint64_t v=0;
 for(size_t i=0;i<a.size();i++)v+=std::abs(int(a[i])-int(b[i]));
 return v;
}
double mean_rgb(const Frame&f,unsigned y0,unsigned y1){
 if(f.empty()||y0>=y1||y1>H)return 0;
 uint64_t sum=0,n=0;
 for(unsigned y=y0;y<y1;y++)for(unsigned x=0;x<W;x++){
  size_t i=4*(y*W+x);sum+=f[i]+f[i+1]+f[i+2];n+=3;
 }
 return n?double(sum)/n:0;
}
struct UiTask {obs_task_t task=nullptr;void* param=nullptr;};
static std::mutex ui_tasks_mutex;
static std::vector<UiTask> ui_tasks;
static void test_ui_task_handler(obs_task_t task,void* param,bool wait){
 if(wait){task(param);return;}
 std::lock_guard<std::mutex> lock(ui_tasks_mutex);
 ui_tasks.push_back({task,param});
}
static void drain_ui_tasks(){
 for(;;){
  std::vector<UiTask> pending;
  {
   std::lock_guard<std::mutex> lock(ui_tasks_mutex);
   if(ui_tasks.empty())break;
   pending.swap(ui_tasks);
  }
  for(auto &item:pending)item.task(item.param);
 }
}
int main(int argc,char**argv){
 if(argc!=4)return 2;
 if(!obs_startup("en-US",nullptr,nullptr))return 2;
 obs_set_ui_task_handler(test_ui_task_handler);
 obs_video_info info{};info.graphics_module="libobs-opengl";info.fps_num=30;info.fps_den=1;
 info.base_width=W;info.base_height=H;info.output_width=W;info.output_height=H;
 info.output_format=VIDEO_FORMAT_RGBA;info.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&info)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 3;}
 obs_register_source(&source_info);
 obs_module_t* module=nullptr;
 if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module)){obs_shutdown();return 4;}
 auto* srcA=obs_source_create("hyper_crt_test_source","CRT A",nullptr,nullptr);auto* aData=active;
 auto* srcB=obs_source_create("hyper_crt_test_source","CRT B",nullptr,nullptr);auto* bData=active;
 Frame frameA=make_frame(14),frameB=make_frame(153);
 obs_enter_graphics();gs_texture_set_image(aData->texture,frameA.data(),W*4,false);
 gs_texture_set_image(bData->texture,frameB.data(),W*4,false);obs_leave_graphics();
 auto* options=obs_data_create();obs_data_set_string(options,"shader_file",argv[3]);obs_data_set_double(options,"speed",0);
 const struct {const char*name;double value;} defaults[]={
  {"glow_strength",0.9},{"scanline_strength",0.22},{"line_thickness",2.0},
  {"flicker",0.08},{"afterglow",0.55}
 };
 for(auto c:defaults){std::string n=std::string("param_")+c.name;obs_data_set_double(options,n.c_str(),c.value);}
 auto* transition=obs_source_create("hypershader_transition","CRT Power",options,nullptr);obs_data_release(options);
 if(!transition){obs_shutdown();return 5;}
 obs_transition_set_size(transition,W,H);obs_source_inc_showing(transition);obs_transition_set(transition,srcA);
 for(int i=0;i<55;i++){(void)render(transition);drain_ui_tasks();std::this_thread::sleep_for(18ms);}
 const bool started=obs_transition_start(transition,OBS_TRANSITION_MODE_MANUAL,5000,srcB);
 auto capture=[&](float t,int frames){obs_transition_set_manual_time(transition,t);Frame out;
  for(int i=0;i<frames;i++){out=render(transition);drain_ui_tasks();std::this_thread::sleep_for(24ms);}return out;};
 const Frame at0=capture(0.000001f,5),at20=capture(.20f,7),at40=capture(.40f,7);
 const Frame at50=capture(.50f,7),at60=capture(.60f,7),at80=capture(.80f,7),at1=capture(.9995f,7);
 const uint64_t startDelta=diff(at0,frameA),endDelta=diff(at1,frameB),sceneDelta=diff(frameA,frameB);
 const uint64_t midA=diff(at50,frameA),midB=diff(at50,frameB);
 const uint64_t steps=diff(at20,at40)+diff(at40,at50)+diff(at50,at60)+diff(at60,at80);
 const double lineCenter=mean_rgb(at40,H/2-3,H/2+4);
 const double lineOuter=(mean_rgb(at40,0,24)+mean_rgb(at40,H-24,H))/2.0;
 const double midpointMean=mean_rgb(at50,0,H);
 auto* props=obs_source_properties(transition);
 bool controls=props&&obs_properties_get(props,"param_glow_strength")&&
  obs_properties_get(props,"param_scanline_strength")&&obs_properties_get(props,"param_line_thickness")&&
  obs_properties_get(props,"param_flicker")&&obs_properties_get(props,"param_afterglow")&&
  obs_properties_get(props,"param_phosphor_color");
 if(props)obs_properties_destroy(props);
 auto* altered=obs_source_get_settings(transition);
 obs_data_set_double(altered,"param_glow_strength",1.8);obs_data_set_double(altered,"param_line_thickness",6.0);
 obs_source_update(transition,altered);obs_data_release(altered);
 const Frame thickLine=capture(.40f,8);const uint64_t controlDelta=diff(at40,thickLine);
 const bool pass=started&&controls&&sceneDelta>50000&&startDelta<1400&&endDelta<1400&&
  midA>50000&&midB>50000&&steps>30000&&lineCenter>lineOuter+20.0&&midpointMean<45.0&&controlDelta>1500;
 std::cout<<"CRT_POWER_TRANSITION started="<<started<<" controls="<<controls
  <<" scene_delta="<<sceneDelta<<" start_delta="<<startDelta<<" end_delta="<<endDelta
  <<" mid_from_a="<<midA<<" mid_from_b="<<midB<<" stepping="<<steps
  <<" line_center="<<lineCenter<<" line_outer="<<lineOuter<<" midpoint_mean="<<midpointMean
  <<" control_delta="<<controlDelta<<" PASS="<<pass<<"\n";
 obs_source_dec_showing(transition);
 obs_transition_clear(transition);
 obs_source_release(transition);
 obs_source_release(srcA);
 obs_source_release(srcB);
 drain_ui_tasks();
 obs_shutdown();
 return pass?0:1;
}
