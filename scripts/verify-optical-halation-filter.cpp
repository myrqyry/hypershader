// Real HyperShader async filter test: native shader loading, property
// controls, bright moving frames, halo generation and shader bypass.
// No plugin mocks or fabricated image results.
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
Frame make_frame(int x) {
 Frame pixels(W*H*4,0);
 for(unsigned y=0;y<H;++y)for(unsigned z=0;z<W;++z) {
  size_t i=4*(y*W+z);
  bool bright=z>=unsigned(x)&&z<unsigned(x+15)&&y>=58&&y<73;
  pixels[i]=bright?250:12;
  pixels[i+1]=bright?244:12;
  pixels[i+2]=bright?222:12;
  pixels[i+3]=255;
 }
 return pixels;
}
const char* source_name(void*){return "Bright moving video fixture";}
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
 .id="hyper_halation_test_source",
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
  for(int k=0;k<3;k++)total+=(abs(int(a[i+k])-int(b[i+k]))>2);
 return total;
}

int main(int argc,char** argv) {
 if(argc!=4)return 2;
 if(!obs_startup("en-US",nullptr,nullptr))return 1;
 obs_video_info info{};
 info.graphics_module="libobs-opengl";
 info.fps_num=30;info.fps_den=1;
 info.base_width=W;info.base_height=H;
 info.output_width=W;info.output_height=H;
 info.output_format=VIDEO_FORMAT_RGBA;info.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&info)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 1;}
 obs_register_source(&source_info);
 obs_module_t* module=nullptr;
 int open=obs_open_module(&module,argv[1],argv[2]);
 if(open!=MODULE_SUCCESS||!obs_init_module(module)){obs_shutdown();return 1;}
 auto* source=obs_source_create("hyper_halation_test_source","Bright moving video",nullptr,nullptr);
 auto* data=obs_data_create();
 const bool disabled=std::getenv("HYPER_TEST_ZERO_HALATION")!=nullptr;
 obs_data_set_string(data,"shader_file",argv[3]);
 obs_data_set_double(data,"param_bloom_intensity",disabled?0.0:1.5);
 obs_data_set_double(data,"param_bloom_threshold",.72);
 obs_data_set_double(data,"param_bloom_knee",.18);
 obs_data_set_double(data,"param_bloom_radius",5.);
 obs_data_set_double(data,"param_halation_warmth",.80);
 obs_data_set_double(data,"param_halo_spread",3.);
 auto* effect=obs_source_create("hypershader_filter","Optical Halation",data,nullptr);
 obs_data_release(data);
 if(!source||!effect){std::cerr<<"Create failure\n";obs_shutdown();return 1;}
 obs_source_filter_add(source,effect);
 obs_source_inc_showing(source);
 size_t total=0,late=0,dim=0;
 bool controls=false;
 for(int i=0;i<45;i++) {
  auto now=make_frame(80+(i%15)*3);
  obs_enter_graphics();
  if(active&&active->texture)gs_texture_set_image(active->texture,now.data(),W*4,false);
  obs_leave_graphics();
  auto pixels=render(source);
  size_t d=diff(pixels,now);
  if(i==20) {
   auto* props=obs_source_properties(effect);
   auto* intensity=props?obs_properties_get(props,"param_bloom_intensity"):nullptr;
   auto* spread=props?obs_properties_get(props,"param_halo_spread"):nullptr;
   controls=intensity&&spread;
   std::cout<<"HALATION_CONTROLS="<<controls<<"\n";
   if(props)obs_properties_destroy(props);
  }
  if(i>20) {
   if(d>90)++late;
   total=std::max(total,d);
   if(pixels.size()==now.size()) {
    size_t warm_pixels=0;
    for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++){
     size_t j=4*(y*W+x);
     if(now[j]==12 && pixels[j]>now[j]+3 &&
        pixels[j]>pixels[j+1]+1)++warm_pixels;
    }
    if(warm_pixels>20)++dim;
   }
  }
  if(i%15==0)std::cout<<"HALATION_FRAME="<<i<<" DIFF="<<d<<"\n";
  std::this_thread::sleep_for(65ms);
 }
 obs_data_t* settings=obs_source_get_settings(effect);
 obs_data_set_double(settings,"param_bloom_intensity",0.);
 obs_source_update(effect,settings);
 obs_data_release(settings);
 size_t zero_diff=SIZE_MAX;
 for(int i=0;i<5;i++) {
  std::this_thread::sleep_for(45ms);
  auto current=make_frame(96);
  obs_enter_graphics();
  gs_texture_set_image(active->texture,current.data(),W*4,false);
  obs_leave_graphics();
  zero_diff=diff(render(source),current);
 }
 std::cout<<"HALATION_FILTER max_diff="<<total<<" late_halo_frames="<<late
          <<" warm_fringe_frames="<<dim<<" zero_intensity_diff="<<zero_diff<<"\n";
 obs_source_dec_showing(source);
 obs_source_filter_remove(source,effect);
 obs_source_release(effect);obs_source_release(source);
 obs_shutdown();
 bool pass=controls && zero_diff==0 &&
          (disabled ? (total==0&&late==0&&dim==0) :
                      (total>300&&late>18&&dim>18));
 if(pass)std::cout<<"PASS: real HyperShader halation filter loads, brightens actual frames, and bypasses cleanly\n";
 return pass?0:1;
}
