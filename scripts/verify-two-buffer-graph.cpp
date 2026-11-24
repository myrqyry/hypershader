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
 if(open!=MODULE_SUCCESS||!obs_init_module(module)){obs_shutdown();return 1;}
 auto* source=obs_source_create("hyper_pframe_test_source","Buffer fixture",nullptr,nullptr);
 auto* data=obs_data_create();
 obs_data_set_string(data,"shader_file",argv[3]);
 auto* filter=obs_source_create("hypershader_filter","Two buffers",data,nullptr);
 obs_data_release(data);
 if(!source||!filter){obs_shutdown();return 1;}
 obs_source_filter_add(source,filter);
 obs_source_inc_showing(source);
 auto fixed=make_frame(30);
 int firstA=-1, firstB=-1, lastA=-1,lastB=-1, samples=0;
 int risingA=0, risingB=0;
 for(int i=0;i<78;i++){
  obs_enter_graphics();
  if(active&&active->texture)gs_texture_set_image(active->texture,fixed.data(),W*4,false);
  obs_leave_graphics();
  auto pixels=render(source);
  if(pixels.size()==W*H*4&&i>=28){
   size_t pos=4*(H/2*W+W/2);
   int a=pixels[pos],b=pixels[pos+1];
   if(samples==0){firstA=a;firstB=b;}
   if(samples>0){if(a>lastA+1)risingA++;if(b>lastB+1)risingB++;}
   lastA=a;lastB=b;samples++;
  }
  std::this_thread::sleep_for(55ms);
 }
 std::cout<<"TWO_BUFFER_OBS samples="<<samples<<" first_A="<<firstA<<" last_A="<<lastA
          <<" first_B="<<firstB<<" last_B="<<lastB
          <<" rising_A="<<risingA<<" rising_B="<<risingB<<"\n";
 bool pass=samples>20&&firstA>5&&firstB>5&&lastA>firstA+15&&lastB>firstB+10
            &&risingA>2&&risingB>2&&lastA>lastB;
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
   auto out=render(source2);
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
  auto out=render(source);
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
 obs_source_dec_showing(source);
 obs_source_filter_remove(source,filter);
 obs_source_release(filter);
 obs_source_release(source);
 obs_shutdown();
 std::cout<<"TWO_BUFFER_RUNTIME_PASS="<<(pass?1:0)<<"\n";
 return pass?0:1;
}
