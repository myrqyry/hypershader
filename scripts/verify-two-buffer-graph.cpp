// End-to-end HyperShaderFX/libobs graphics regression: real module,
// async shader loader, moving pixel fixture, feedback and clean I-frame.
// No mock plugin or fake P-frame results.
#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <vector>
#include <cstdio>
#include <string>
#include <thread>
#include <chrono>
#include <iostream>
#include <cstring>
#include <mutex>
using namespace std::chrono_literals;
constexpr unsigned W=240,H=136;
using Frame=std::vector<uint8_t>;
struct TestSource {gs_texture_t* texture=nullptr;uint32_t width=W,height=H;};
TestSource* active=nullptr;
Frame make_frame(int x){
 Frame pixels(W*H*4);
 for(unsigned y=0;y<H;y++)for(unsigned z=0;z<W;z++){
  size_t i=4*(y*W+z);
  bool moving=z>=unsigned(x)&&z<unsigned(x+48)&&y>=33&&y<111;
  pixels[i]=moving?230:uint8_t((z*3+y)%125);
  pixels[i+1]=moving?45:uint8_t((z+y)%93);
  pixels[i+2]=moving?12:uint8_t((y*3+z)%110);
  pixels[i+3]=255;
 }
 return pixels;
}
const char* source_name(void*){return "Temporary moving video fixture";}
uint32_t source_width(void* data){return static_cast<TestSource*>(data)->width;}
uint32_t source_height(void* data){return static_cast<TestSource*>(data)->height;}
void* source_create(obs_data_t*,obs_source_t*){
 auto* s=new TestSource;
 auto frame=make_frame(10);
 const uint8_t* pixels=frame.data();
 obs_enter_graphics();
 s->texture=gs_texture_create(W,H,GS_RGBA,1,&pixels,GS_DYNAMIC);
 obs_leave_graphics(); active=s;return s;
}
void source_destroy(void* data){
 auto* s=(TestSource*)data;
 obs_enter_graphics();
 if(s->texture)gs_texture_destroy(s->texture);
 obs_leave_graphics();active=nullptr;delete s;
}
void source_render(void* data,gs_effect_t*){
 auto* s=(TestSource*)data;
 if(s->texture)obs_source_draw(s->texture,0,0,s->width,s->height,false);
}
obs_source_info source_info{
 .id="hyper_pframe_test_source",
 .type=OBS_SOURCE_TYPE_INPUT,
 .output_flags=OBS_SOURCE_VIDEO,
 .get_name=source_name,
 .create=source_create,
 .destroy=source_destroy,
 .get_width=source_width,
 .get_height=source_height,
 .video_render=source_render,
};
Frame render(obs_source_t* source) {
 Frame frame;
 obs_enter_graphics();
 auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
 auto* stage=gs_stagesurface_create(W,H,GS_RGBA);
 if(target&&stage&&gs_texrender_begin(target,W,H)){
  gs_projection_push();gs_matrix_push();gs_matrix_identity();
  gs_ortho(0,float(W),0,float(H),-100,100);
  obs_source_video_render(source);
  gs_matrix_pop();gs_projection_pop();gs_texrender_end(target);
  if(auto* texture=gs_texrender_get_texture(target)){
   gs_stage_texture(stage,texture);
   uint8_t* data=nullptr;uint32_t pitch=0;
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
size_t diff(const Frame& a,const Frame& b) {
 if(a.size()!=b.size())return size_t(-1);
 size_t total=0;
 for(size_t i=0;i<a.size();i+=4)
  for(int k=0;k<3;k++)total+=(abs(int(a[i+k])-int(b[i+k]))>7);
 return total;
}

struct UiTask {obs_task_t task;void* param;};
static std::mutex ui_mutex;
static std::vector<UiTask> ui_tasks;
static void queue_ui(obs_task_t task,void* param,bool wait) {
 if(wait){task(param);return;}
 std::lock_guard<std::mutex> lock(ui_mutex);
 ui_tasks.push_back({task,param});
}
static void drain_ui() {
 for(;;){
  std::vector<UiTask> pending;
  {std::lock_guard<std::mutex> lock(ui_mutex);pending.swap(ui_tasks);}
  if(pending.empty())return;
  for(auto &item:pending)item.task(item.param);
 }
}

int main(int argc,char** argv) {
 if(argc<4||argc>6)return 2;
 const std::string mode=argc>=5?argv[4]:"default";
 if(mode=="channels"&&argc!=6)return 2;
 if(!obs_startup("en-US",nullptr,nullptr))return 1;
 obs_set_ui_task_handler(queue_ui);
 obs_video_info info{};
 info.graphics_module="libobs-opengl";
 info.fps_num=30;info.fps_den=1;
 info.base_width=W;info.base_height=H;info.output_width=W;info.output_height=H;
 info.output_format=VIDEO_FORMAT_RGBA;info.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&info)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 1;}
 obs_register_source(&source_info);
 obs_module_t* module=nullptr;
 int open=obs_open_module(&module,argv[1],argv[2]);
 if(open!=MODULE_SUCCESS||!obs_init_module(module)){obs_shutdown();return 1;}
 auto* source=obs_source_create("hyper_pframe_test_source","Buffer fixture",nullptr,nullptr);
 auto* primary=active;
 obs_source_t* live=nullptr;
 TestSource* liveData=nullptr;
 auto fill=[&](TestSource* fixture,uint8_t r,uint8_t g,uint8_t b,unsigned w,unsigned h){
  Frame pixels(W*H*4);
  for(size_t i=0;i<pixels.size();i+=4){pixels[i]=r;pixels[i+1]=g;pixels[i+2]=b;pixels[i+3]=255;}
  obs_enter_graphics();
  fixture->width=w;fixture->height=h;
  gs_texture_set_image(fixture->texture,pixels.data(),W*4,false);
  obs_leave_graphics();
 };
 if(mode=="live"||mode=="live-source"){
  live=obs_source_create("hyper_pframe_test_source","Live texture fixture",nullptr,nullptr);
  liveData=active;
  if(!live||!liveData)return 1;
  fill(liveData,64,128,191,120,68);
  obs_source_inc_showing(live);
 }
 auto* data=obs_data_create();
 obs_data_set_string(data,"shader_file",argv[3]);
 if(mode=="live"||mode=="live-source"){
  obs_data_set_int(data,"channel0_type",2);
  obs_data_set_string(data,"channel0_source",obs_source_get_name(live));
  obs_data_set_string(data,"channel0_uuid",obs_source_get_uuid(live));
 }
 if(mode=="channels"){
  obs_data_set_int(data,"channel0_type",1);
  obs_data_set_string(data,"channel0_file",argv[5]);
 }
 if(mode=="live-source"){
  obs_data_set_int(data,"source_width",W);
  obs_data_set_int(data,"source_height",H);
 }
 auto* filter=obs_source_create(mode=="live-source"?"hypershader_source":"hypershader_filter","Two buffers",data,nullptr);
 obs_data_release(data);
 if(!source||!filter){obs_shutdown();return 1;}
 if(mode!="live-source")obs_source_filter_add(source,filter);
 obs_source_inc_showing(mode=="live-source"?filter:source);
 auto fixed=make_frame(30);
 if(mode=="channels"||mode=="live"||mode=="live-source"){
  auto check=[&](int expected){
   size_t valid=0;
   for(int i=0;i<40;i++){
    auto out=render(mode=="live-source"?filter:source);drain_ui();
    if(i>20&&out.size()==W*H*4){
     bool match=true;
     for(size_t j=0;j<out.size();j+=4){
      if(abs(int(out[j])-expected)>3||abs(int(out[j+1])-expected)>3||
         abs(int(out[j+2])-expected)>3||out[j+3]!=255){match=false;break;}
     }
     if(match)++valid;
    }
    std::this_thread::sleep_for(40ms);
   }
   return valid;
  };
  if(mode=="live"||mode=="live-source"){
   size_t present=check(255);
   fill(liveData,191,64,128,60,34);
   size_t changed=check(191);
   obs_source_dec_showing(live);
   size_t hidden=check(64);
   obs_source_inc_showing(live);
   size_t shown=check(191);
   auto select=[&](obs_source_t* selected){
    auto* opts=obs_source_get_settings(filter);
    std::string id="hs-source:v1:";
    id+=obs_source_get_uuid(selected);
    obs_data_set_string(opts,"channel0_source",id.c_str());
    obs_data_set_string(opts,"channel0_uuid",obs_source_get_uuid(selected));
    obs_source_update(filter,opts);obs_data_release(opts);
   };
   auto* owner=mode=="live-source"?filter:source;
   select(owner);
   size_t self=check(64);
   // A scene containing the consumer must also be rejected.
   auto* recursiveScene=obs_scene_create("Recursive scene fixture");
   auto* recursiveSource=obs_scene_get_source(recursiveScene);
   auto* recursiveItem=obs_scene_add(recursiveScene,owner);
   obs_source_inc_showing(recursiveSource);
   select(recursiveSource);
   size_t cycle=check(64);
   obs_source_dec_showing(recursiveSource);
   obs_sceneitem_remove(recursiveItem);
   obs_source_remove(recursiveSource);
   obs_scene_release(recursiveScene);
   // A distinct scene is a real composite source and can be captured.
   auto* otherScene=obs_scene_create("Independent scene fixture");
   auto* otherSource=obs_scene_get_source(otherScene);
   fill(liveData,128,191,64,W,H);
   auto* otherItem=obs_scene_add(otherScene,live);
   obs_source_inc_showing(otherSource);
   select(otherSource);
   size_t scene=check(128);
   obs_source_dec_showing(otherSource);
   obs_sceneitem_remove(otherItem);
   obs_source_remove(otherSource);
   obs_scene_release(otherScene);
   fill(liveData,191,64,128,60,34);
   select(live);
   size_t restored=check(191);
   obs_source_remove(live);
   size_t removed=check(64);
   obs_source_dec_showing(live);
   obs_source_release(live);
   bool pass=present==19&&changed==19&&hidden==19&&shown==19&&self==19&&cycle==19&&scene==19&&restored==19&&removed==19;
   obs_source_dec_showing(owner);
   if(mode!="live-source")obs_source_filter_remove(source,filter);
   if(mode=="live-source")obs_source_remove(filter);
   obs_source_remove(source);
   obs_source_release(filter);obs_source_release(source);drain_ui();
   obs_wait_for_destroy_queue();
   std::printf("TWO_BUFFER_LIVE present=%zu resized=%zu hidden=%zu shown=%zu self=%zu cycle=%zu scene=%zu restored=%zu removed=%zu PASS=%d\n",
               present,changed,hidden,shown,self,cycle,scene,restored,removed,int(pass));
   std::printf("TWO_BUFFER_RUNTIME_PASS=%d\n",int(pass));
   std::fflush(stdout);
   obs_shutdown();
   return pass?0:1;
  }
  size_t present=check(255);
  auto* opts=obs_source_get_settings(filter);
  obs_data_set_int(opts,"channel0_type",0);
  obs_source_update(filter,opts);obs_data_release(opts);
  size_t absent=check(64);
  opts=obs_source_get_settings(filter);
  obs_data_set_int(opts,"channel0_type",1);
  obs_data_set_string(opts,"channel0_file",argv[5]);
  obs_source_update(filter,opts);obs_data_release(opts);
  size_t restored=check(255);
  bool pass=present==19&&absent==19&&restored==19;
  obs_source_dec_showing(source);obs_source_filter_remove(source,filter);
  obs_source_release(filter);obs_source_release(source);drain_ui();obs_shutdown();
  std::cout<<"TWO_BUFFER_CHANNELS present="<<present<<" absent="<<absent<<" restored="<<restored<<" PASS="<<pass<<"\n";
  std::cout<<"TWO_BUFFER_RUNTIME_PASS="<<pass<<"\n";
  return pass?0:1;
 }
 if(mode=="budget"){
  obs_enter_graphics();
  gs_texture_set_image(primary->texture,fixed.data(),W*4,false);
  obs_leave_graphics();
  size_t bypass=0;
  for(int i=0;i<40;i++){
   auto out=render(source);drain_ui();
   if(i>20&&diff(out,fixed)==0)++bypass;
   std::this_thread::sleep_for(50ms);
  }
  obs_enter_graphics();
  primary->width=W/2;primary->height=H/2;
  obs_leave_graphics();
  const size_t center=4*(H/4*W+W/4);
  int seed=-1,mature=-1;
  size_t valid=0;
  for(int i=0;i<30;i++){
   auto out=render(source);drain_ui();
   if(out.size()==W*H*4){
    if(i==0)seed=out[center];
    mature=out[center];
    if(out[center+2]<=3)++valid;
   }
   std::this_thread::sleep_for(50ms);
  }
  obs_enter_graphics();
  primary->width=W;primary->height=H;
  obs_leave_graphics();
  size_t again=0;
  for(int i=0;i<8;i++){
   auto out=render(source);drain_ui();
   if(diff(out,fixed)==0)++again;
   std::this_thread::sleep_for(50ms);
  }
  obs_enter_graphics();
  primary->width=W/2;primary->height=H/2;
  obs_leave_graphics();
  auto restarted=render(source);drain_ui();
  int reset=restarted.size()==W*H*4?restarted[center]:999;
  bool pass=bypass==19&&seed>=0&&seed<15&&mature>seed+20&&valid==30&&again==8&&reset<15;
  obs_source_dec_showing(source);obs_source_filter_remove(source,filter);
  obs_source_release(filter);obs_source_release(source);drain_ui();obs_shutdown();
  std::cout<<"TWO_BUFFER_BUDGET bypass="<<bypass<<" seed="<<seed<<" mature="<<mature<<" valid="<<valid
           <<" bypass_again="<<again<<" reset="<<reset<<" PASS="<<pass<<"\n";
  std::cout<<"TWO_BUFFER_RUNTIME_PASS="<<pass<<"\n";
  return pass?0:1;
 }
 if(mode=="invalid"){
  size_t unchanged=0;
  obs_enter_graphics();
  gs_texture_set_image(primary->texture,fixed.data(),W*4,false);
  obs_leave_graphics();
  for(int i=0;i<40;i++){
   auto pixels=render(source);drain_ui();
   if(i>20&&diff(pixels,fixed)==0)++unchanged;
   std::this_thread::sleep_for(50ms);
  }
  bool pass=unchanged==19;
  obs_source_dec_showing(source);
  obs_source_filter_remove(source,filter);
  obs_source_release(filter);obs_source_release(source);
  drain_ui();obs_shutdown();
  std::cout<<"TWO_BUFFER_INVALID unchanged="<<unchanged<<" PASS="<<pass<<"\n";
  std::cout<<"TWO_BUFFER_RUNTIME_PASS="<<pass<<"\n";
  return pass?0:1;
 }

 int firstA=-1, firstB=-1, lastA=-1,lastB=-1, samples=0;
 int risingA=0, risingB=0, badPixels=0;
 for(int i=0;i<78;i++){
  obs_enter_graphics();
  if(active&&active->texture)gs_texture_set_image(active->texture,fixed.data(),W*4,false);
  obs_leave_graphics();
  auto pixels=render(source);drain_ui();
  if(pixels.size()==W*H*4&&i>=28){
   size_t pos=4*(H/2*W+W/2);
   int a=pixels[pos],b=pixels[pos+1];
   bool bad=false;
   for(size_t j=2;j<pixels.size();j+=4)if(pixels[j]>3){bad=true;break;}
   if(bad)++badPixels;
   if(samples==0){firstA=a;firstB=b;}
   if(samples>0){if(a>lastA+1)risingA++;if(b>lastB+1)risingB++;}
   lastA=a;lastB=b;samples++;
  }
  std::this_thread::sleep_for(55ms);
 }
 std::cout<<"TWO_BUFFER_OBS samples="<<samples<<" first_A="<<firstA<<" last_A="<<lastA
          <<" first_B="<<firstB<<" last_B="<<lastB
          <<" rising_A="<<risingA<<" rising_B="<<risingB<<" bad_contract_frames="<<badPixels<<"\n";
 bool pass=samples>20&&firstA>5&&firstB>5&&lastA>firstA+15&&lastB>firstB+10
            &&risingA>2&&risingB>2&&lastA>lastB&&badPixels==0;
 auto* source2=obs_source_create("hyper_pframe_test_source","Independent buffer fixture",nullptr,nullptr);
 auto* settings2=obs_data_create();
 obs_data_set_string(settings2,"shader_file",argv[3]);
 auto* filter2=obs_source_create("hypershader_filter","Fresh two buffers",settings2,nullptr);
 obs_data_release(settings2);
 if(!source2||!filter2)pass=false;
 int freshA=-1,freshB=-1;
 if(source2&&filter2) {
  obs_source_filter_add(source2,filter2);
  obs_source_inc_showing(source2);
  for(int n=0;n<16;n++){
   auto out=render(source2);drain_ui();
   if(n==15 && out.size()==W*H*4) {
    size_t pos=4*(H/2*W+W/2);
    freshA=out[pos];freshB=out[pos+1];
   }
   std::this_thread::sleep_for(55ms);
  }
  obs_source_dec_showing(source2);
  obs_source_filter_remove(source2,filter2);
 }
 if(filter2)obs_source_release(filter2);
 if(source2)obs_source_release(source2);
 bool independent=freshA>0&&freshB>0&&freshA+30<lastA&&freshB+20<lastB;
 pass = pass && independent;
 std::cout<<"TWO_BUFFER_ISOLATION fresh_A="<<freshA<<" mature_A="<<lastA
          <<" fresh_B="<<freshB<<" mature_B="<<lastB
          <<" PASS="<<independent<<"\n";
 obs_data_t* changed=obs_source_get_settings(filter);
 obs_data_set_double(changed,"param_output_gain",0.0);
 obs_source_update(filter,changed);
 obs_data_release(changed);
 int mutedA=999,mutedB=999;
 for(int n=0;n<14;n++){
  auto out=render(source);drain_ui();
  if(n==13 && out.size()==W*H*4){
   size_t pos=4*(H/2*W+W/2);
   mutedA=out[pos];mutedB=out[pos+1];
  }
  std::this_thread::sleep_for(50ms);
 }
 bool muted=mutedA<6&&mutedB<6;
 pass=pass&&muted;
 std::cout<<"TWO_BUFFER_CONTROL muted_A="<<mutedA
          <<" muted_B="<<mutedB<<" PASS="<<muted<<"\n";
 if(mode=="scaled"){
  auto* restored=obs_source_get_settings(filter);
  obs_data_set_double(restored,"param_output_gain",1.0);
  obs_data_set_double(restored,"compose_effect_opacity",0.0);
  obs_source_update(filter,restored);obs_data_release(restored);
  size_t passthrough=0;
  for(int n=0;n<8;n++){
   auto out=render(source);drain_ui();
   if(diff(out,fixed)==0)++passthrough;
   std::this_thread::sleep_for(50ms);
  }
  auto* visible=obs_source_get_settings(filter);
  obs_data_set_double(visible,"compose_effect_opacity",1.0);
  obs_source_update(filter,visible);obs_data_release(visible);
  size_t oldCenter=4*(H/2*W+W/2);
  int resumedA=-1;
  size_t resumedFrames=0;
  for(int n=0;n<12;n++){
   auto out=render(source);drain_ui();
   if(out.size()==W*H*4){
    resumedA=out[oldCenter];
    if(resumedA>lastA&&out[oldCenter+2]<=3)++resumedFrames;
   }
   std::this_thread::sleep_for(50ms);
  }
  // OBS delivers property updates asynchronously. Require multiple visible
  // frames with mature history, not a synchronous first post-update draw.
  bool composited=passthrough>=6&&resumedFrames>=8;
  pass=pass&&composited;
  std::cout<<"TWO_BUFFER_COMPOSITE passthrough="<<passthrough
           <<" resumed_A="<<resumedA<<" resumed_frames="<<resumedFrames
           <<" PASS="<<composited<<"\n";
  // Resize to odd extents. History must reset rather than sample old sizes.
  obs_enter_graphics();
  primary->width=W-3;primary->height=H-3;
  obs_leave_graphics();
  auto first=render(source);drain_ui();
  size_t center=4*((H-3)/2*W+(W-3)/2);
  int reset=first.size()==W*H*4?first[center]:999;
  int blue=first.size()==W*H*4?first[center+2]:999;
  int later=-1;
  for(int n=0;n<18;n++){
   auto out=render(source);drain_ui();
   if(out.size()==W*H*4){
    later=out[center];
    blue=std::max(blue,int(out[center+2]));
   }
   std::this_thread::sleep_for(50ms);
  }
  bool resized=reset<15&&later>reset+15&&blue<=3;
  pass=pass&&resized;
  std::cout<<"TWO_BUFFER_RESIZE reset_A="<<reset<<" later_A="<<later<<" blue="<<blue<<" PASS="<<resized<<"\n";
 }
 obs_source_dec_showing(source);
 obs_source_filter_remove(source,filter);
 obs_source_release(filter);
 obs_source_release(source);
 drain_ui();
 obs_shutdown();
 std::cout<<"TWO_BUFFER_RUNTIME_PASS="<<(pass?1:0)<<"\n";
 return pass?0:1;
}
