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
#include "verify-motion-filter-ui.hpp"
#include "verify-gnomalab-slit-ui.hpp"
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
 if(argc!=4 && argc!=5) return 2;
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
 if(open!=MODULE_SUCCESS||!obs_init_module(module)){obs_shutdown();return 1;}
 auto* source=obs_source_create("hyper_pframe_test_source","Effects fixture",nullptr,nullptr);
 auto* opts=obs_data_create();
 obs_data_set_string(opts,"shader_file",argv[3]);
 auto* filter=obs_source_create("hypershader_filter","Effects",opts,nullptr);
 obs_data_release(opts);
 if(!source||!filter){obs_shutdown();return 1;}
 obs_source_filter_add(source,filter);
 obs_source_inc_showing(source);
 const bool slit_mode=argc==5 && std::string(argv[4])=="slit";
 bool slit_ui=true;
 Frame baseline=make_frame(16), early,late,peak,muted;
 bool controls=false;
 for(int i=0;i<85;i++){
  Frame pixels = i>=26 && i<35 ? make_frame(155) : baseline;
  obs_enter_graphics();
  if(active&&active->texture)
   gs_texture_set_image(active->texture,pixels.data(),W*4,false);
  obs_leave_graphics();
  Frame out=render(source);
  if(i==25){
   if(slit_mode){
    slit_ui=verify_slit_ui(filter);
    auto* settings=obs_source_get_settings(filter);
    // Regression: the minimum supported 0.0001 rate is deeply subpixel at 240px. It must
    // still inject source texels even with RGB temporal splitting disabled.
    obs_data_set_double(settings,"param_speed",0.0001);
    obs_data_set_double(settings,"param_rgbSplit",0.0);
    obs_data_set_double(settings,"param_mapInfluence",0.45);
    obs_source_update(filter,settings);
    obs_data_release(settings);
   }
   auto* properties=obs_source_properties(filter);
   controls=properties&&obs_properties_get(properties,"param_effect_mix");
   if(properties)obs_properties_destroy(properties);
   controls=verify_motion_filter_ui(filter,argv[3])&&controls&&slit_ui;
  }
  if(i==34)peak=out;
  if(i==45)early=out;
  if(i==74)late=out;
  std::this_thread::sleep_for(46ms);
 }
 auto sumabs=[](const Frame& a,const Frame& b) {
  if(a.size()!=b.size())return size_t(0);
  size_t total=0;
  for(size_t i=0;i<a.size();i+=4)for(int c=0;c<3;c++)
   total+=abs(int(a[i+c])-int(b[i+c]));
  return total;
 };
 auto sample=[](const Frame& f,int x,int y){
  if(f.size()!=W*H*4)return std::string("invalid");
  size_t i=4*(y*W+x);
  return std::to_string(f[i])+","+std::to_string(f[i+1])+","+std::to_string(f[i+2]);
 };
 std::cout<<"CREATIVE_FRAME_DEBUG pulse_mid="<<sample(peak,172,65)
          <<" tail_mid="<<sample(early,172,65)
          <<" late_mid="<<sample(late,172,65)
          <<" abs_temporal="<<sumabs(early,late)<<"\n";
 const size_t output_change=diff(late,baseline);
 const size_t temporal_change=diff(early,late);
 const size_t stimulus_change=diff(peak,late);
 bool mode_ok=true;
 if(argc==5){
  const std::string mode=argv[4];
  const char* key=mode=="clean"?"param_force_clean":
                  mode=="mask"?"param_show_mask":
                  mode=="vectors"?"param_show_vectors":
                  mode=="slit"?"param_cleanBuffer":nullptr;
  if(!key)mode_ok=false;
  else {
   auto* props=obs_source_properties(filter);
   const bool has_control=props && obs_properties_get(props,key);
   if(props)obs_properties_destroy(props);
   auto* mode_settings=obs_source_get_settings(filter);
   obs_data_set_bool(mode_settings,key,true);
   if(mode=="mask" || mode=="slit")obs_data_set_double(mode_settings,"param_effect_mix",1.0);
   if(mode=="vectors")obs_data_set_double(mode_settings,"param_vector_brightness",2.0);
   obs_source_update(filter,mode_settings);
   obs_data_release(mode_settings);
   Frame mode_frame;
   for(int i=0;i<14;i++){
    mode_frame=render(source);
    std::this_thread::sleep_for(42ms);
   }
   const size_t mode_delta=mode=="clean"?diff(mode_frame,baseline):diff(mode_frame,late);
   bool opaque=true,monochrome=true;
   for(size_t i=0;i+3<mode_frame.size();i+=4){
    if(mode_frame[i+3]!=baseline[i+3])opaque=false;
    if(std::abs(int(mode_frame[i])-int(mode_frame[i+1]))>2 ||
       std::abs(int(mode_frame[i+1])-int(mode_frame[i+2]))>2)monochrome=false;
   }
   size_t brightness_delta=0;
   bool black=true;
   if(mode=="slit") {
     for(size_t i=0;i+3<mode_frame.size();i+=4)
       for(int c=0;c<3;c++) black &= mode_frame[i+c]<=3;
   }
   if(mode=="vectors"){
     auto* off_settings=obs_source_get_settings(filter);
     obs_data_set_double(off_settings,"param_vector_brightness",0.0);
     obs_source_update(filter,off_settings);
     obs_data_release(off_settings);
     Frame off;
     for(int i=0;i<6;i++){
       off=render(source);
       std::this_thread::sleep_for(42ms);
     }
     brightness_delta=diff(mode_frame,off);
   }
   mode_ok=has_control && opaque && (mode=="clean"?mode_delta<50:
                          mode=="mask"?mode_delta>600 && monochrome:
                          mode=="slit"?mode_delta>600 && black:
                          mode_delta>600 && brightness_delta>600);
   std::cout<<"MODE_VERIFY="<<mode<<" control="<<has_control
            <<" delta="<<mode_delta<<" brightness_delta="<<brightness_delta
            <<" alpha="<<opaque
            <<" monochrome="<<monochrome<<" PASS="<<mode_ok<<"\n";
  }
 }
 obs_data_t* settings=obs_source_get_settings(filter);
 obs_data_set_double(settings,"param_effect_mix",0.0);
 obs_source_update(filter,settings);
 obs_data_release(settings);
 for(int i=0;i<22;i++){
  muted=render(source);
  std::this_thread::sleep_for(42ms);
 }
 const size_t bypass_change=diff(muted,baseline);
 const bool ok=controls && mode_ok && output_change>600 && temporal_change>250 &&
               stimulus_change>300 && bypass_change<50;
 std::cout<<"NEW_GRAPH_SHADER="<<argv[3]
          <<" controls="<<controls
          <<" output_delta="<<output_change
          <<" temporal_delta="<<temporal_change
          <<" stimulus_delta="<<stimulus_change
          <<" bypass_delta="<<bypass_change
          <<" PASS="<<ok<<"\n";
 obs_source_dec_showing(source);
 obs_source_filter_remove(source,filter);
 obs_source_release(filter);
 obs_source_release(source);
 obs_shutdown();
 return ok?0:1;
}
