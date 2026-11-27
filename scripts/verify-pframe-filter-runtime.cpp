// End-to-end hypershader/libobs graphics regression: real module,
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
using namespace std::chrono_literals;
constexpr unsigned W=240,H=136;
using Frame=std::vector<uint8_t>;
struct TestSource {gs_texture_t* texture=nullptr;};
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
uint32_t source_width(void*){return W;}
uint32_t source_height(void*){return H;}
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
 if(s->texture)obs_source_draw(s->texture,0,0,W,H,false);
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
int main(int argc,char** argv) {
 if(argc!=4)return 2;
 if(!obs_startup("en-US",nullptr,nullptr))return 1;
 obs_video_info info{};
 info.graphics_module="libobs-opengl";
 info.fps_num=30;info.fps_den=1;
 info.base_width=W;info.base_height=H;info.output_width=W;info.output_height=H;
 info.output_format=VIDEO_FORMAT_RGBA;info.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&info)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 1;}
 obs_register_source(&source_info);
 obs_module_t* module=nullptr;
 int open=obs_open_module(&module,argv[1],argv[2]);
 std::cerr<<"plugin_open_result="<<open<<"\n";
 if(open!=MODULE_SUCCESS||!obs_init_module(module)){obs_shutdown();return 1;}
 std::cerr<<"filter_name="<<obs_source_get_display_name("hypershader_filter")<<"\n";
 auto* source=obs_source_create("hyper_pframe_test_source","Moving test video",nullptr,nullptr);
 auto* data=obs_data_create();
 obs_data_set_string(data,"shader_file",argv[3]);
 const bool zero_loss=std::getenv("HYPER_TEST_ZERO_LOSS")!=nullptr;
 obs_data_set_double(data,"param_pframe_loss",zero_loss?0.0:.99);
 obs_data_set_double(data,"param_missing_residual",1.0);
 obs_data_set_double(data,"param_macroblock_size",32.0);
 obs_data_set_double(data,"param_vector_carry",1.0);
 obs_data_set_double(data,"param_residual_recovery",.0);
 obs_data_set_double(data,"param_iframe_interval",0.0);
 obs_data_set_double(data,"param_force_keyframe",0.0);
 auto* effect=obs_source_create("hypershader_filter","PFrame Loss",data,nullptr);
 obs_data_release(data);
 if(!source||!effect){std::cerr<<"create failure\n";obs_shutdown();return 1;}
 obs_source_filter_add(source,effect);
 obs_source_inc_showing(source);
 size_t lastdiff=0,maxdiff=0,clean=0,large=0;
 for(int i=0;i<75;i++){
  int x=10+(i%27)*5;
  auto now=make_frame(x);
  obs_enter_graphics();
  if(active && active->texture)gs_texture_set_image(active->texture,now.data(),W*4,false);
  obs_leave_graphics();
  auto pixels=render(source);
  size_t d=diff(pixels,now);
  if(i==15) {
    auto* properties=obs_source_properties(effect);
    auto* loss=properties?obs_properties_get(properties,"param_pframe_loss"):nullptr;
    auto* recovery=properties?obs_properties_get(properties,"param_force_keyframe"):nullptr;
    std::cout<<"PFRAME_RUNTIME_CONTROLS="<<(loss&&recovery)<<"\n";
    if(properties)obs_properties_destroy(properties);
    if(!loss||!recovery) {
      obs_source_dec_showing(source);
      obs_source_filter_remove(source,effect);
      obs_source_release(effect);obs_source_release(source);
      obs_shutdown();
      return 1;
    }
  }
  if(i>35&&d>1000)large++;
  if(d!=size_t(-1)&&d>maxdiff)maxdiff=d;
  if(i%15==0)std::cout<<"frame="<<i<<" live_diff="<<d<<"\n";
  lastdiff=d;
  std::this_thread::sleep_for(65ms);
 }
 obs_data_t* updated=obs_source_get_settings(effect);
 obs_data_set_double(updated,"param_force_keyframe",1.);
 obs_source_update(effect,updated);
 obs_data_release(updated);
 std::this_thread::sleep_for(300ms);
 for(int i=0;i<4;i++){
  auto current=make_frame(99);
  obs_enter_graphics();gs_texture_set_image(active->texture,current.data(),W*4,false);obs_leave_graphics();
  clean=diff(render(source),current);
  std::this_thread::sleep_for(60ms);
 }
 obs_data_t* bypass=obs_source_get_settings(effect);
 obs_data_set_double(bypass,"param_force_keyframe",0.);
 obs_data_set_double(bypass,"param_pframe_loss",0.);
 obs_source_update(effect,bypass);
 obs_data_release(bypass);
 size_t zero_delta=size_t(-1);
 for(int i=0;i<4;i++) {
  auto current=make_frame(99);
  obs_enter_graphics();
  gs_texture_set_image(active->texture,current.data(),W*4,false);
  obs_leave_graphics();
  zero_delta=diff(render(source),current);
 }
 std::cout<<"PFRAME_REAL_PLUGIN max_corruption="<<maxdiff<<" late_distorted_frames="<<large
          <<" clean_keyframe_diff="<<clean<<" zero_loss_diff="<<zero_delta<<"\n";
 obs_source_dec_showing(source);
 obs_source_filter_remove(source,effect);
 obs_source_release(effect);obs_source_release(source);
 obs_shutdown();
 bool pass=clean==0 && zero_delta==0 &&
             (zero_loss ? (maxdiff==0 && large==0) :
                           (maxdiff>2000 && large>20 && maxdiff<W*H*3*8/10));
 if(pass)std::cout<<"PASS: real hypershader filter loaded, rendered, reset, and bypassed\n";
 return pass?0:1;
}
