// Loads the actual HyperShader OBS module: native source playback, absent
// tracking passthrough, and camera-output feedback under isolated libobs.
#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <thread>
#include <chrono>
#include <vector>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
using namespace std::chrono_literals;
namespace fs=std::filesystem;
constexpr unsigned W=320,H=192;
using Frame=std::vector<uint8_t>;
struct Fixture{gs_texture_t* tex=nullptr;};
Fixture* active=nullptr;
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
 active=f;return f;
}
void destroy(void* data){
 auto* f=(Fixture*)data;
 obs_enter_graphics();if(f->tex)gs_texture_destroy(f->tex);obs_leave_graphics();
 active=nullptr;delete f;
}
void video_render(void* data,gs_effect_t*){auto* f=(Fixture*)data;if(f->tex)obs_source_draw(f->tex,0,0,W,H,false);}
obs_source_info fixture_info {
 .id="original_instrument_input",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_VIDEO,
 .get_name=name,.create=create,.destroy=destroy,.get_width=width,.get_height=height,
 .video_render=video_render
};
void update(int step){
 auto frame=pattern(step);obs_enter_graphics();
 if(active&&active->tex)gs_texture_set_image(active->tex,frame.data(),W*4,false);
 obs_leave_graphics();
}
Frame capture(obs_source_t* src){
 Frame pixels;obs_enter_graphics();
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
int main(int argc,char**argv){
 if(argc!=4)return 2;
 if(!obs_startup("en-US",nullptr,nullptr))return 3;
 obs_video_info v{};v.graphics_module="libobs-opengl";v.fps_num=30;v.fps_den=1;
 v.base_width=v.output_width=W;v.base_height=v.output_height=H;
 v.output_format=VIDEO_FORMAT_RGBA;v.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&v)!=OBS_VIDEO_SUCCESS)return 4;
 obs_register_source(&fixture_info);obs_module_t* module=nullptr;
 if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 5;
 bool pass=true;fs::path root(argv[3]);
 for(const char* file:{"source.brass_register.effect","source.paper_tide.effect","source.spectral_garden.effect"}){
  auto* settings=obs_data_create();auto path=(root/file).string();
  obs_data_set_int(settings,"preset",-1);
  obs_data_set_int(settings,"source_width",W);obs_data_set_int(settings,"source_height",H);
  obs_data_set_string(settings,"shader_file",path.c_str());
  auto* src=obs_source_create("hypershader_source",file,settings,nullptr);
  obs_data_release(settings);
  if(!src){pass=false;continue;}
  obs_source_inc_showing(src);obs_set_output_source(0,src);
  Frame previous;int shown=0,moved=0;
  for(int i=0;i<65;i++){
   std::this_thread::sleep_for(38ms);Frame next=capture(src);
   if(i>15){
    // The brass artwork has a deliberately dark center; test the
    // full framebuffer, not one arbitrary center pixel.
    shown+=!next.empty()&&std::any_of(next.begin(),next.end(),
      [](uint8_t value){return value>140;});
    if(!previous.empty()&&delta(next,previous)>80)moved++;
   }
   previous=next;
  }
  bool ok=shown>28&&moved>7;pass&=ok;
  printf("ORIGINAL_PLUGIN_SOURCE_%s %s visible=%d evolving=%d\n",ok?"PASS":"FAIL",file,shown,moved);
  obs_set_output_source(0,nullptr);obs_source_dec_showing(src);obs_source_release(src);
 }
 auto* src=obs_source_create("original_instrument_input","camera",nullptr,nullptr);
 if(!src)return 6;
 obs_source_inc_showing(src);obs_set_output_source(0,src);
 Frame reference=capture(src);
 for(const char* file:{"filter.topographic_etch.effect","filter.contact_filaments.effect"}){
  auto* settings=obs_data_create();auto path=(root/file).string();
  obs_data_set_string(settings,"shader_file",path.c_str());
  auto* filter=obs_source_create("hypershader_filter",file,settings,nullptr);
  obs_data_release(settings);if(!filter){pass=false;continue;}
  obs_source_filter_add(src,filter);
  int same=0;
  for(int i=0;i<35;i++){
   std::this_thread::sleep_for(35ms);
   if(i>15&&delta(reference,capture(src))<2)same++;
  }
  bool ok=same>=14;pass&=ok;
  printf("ORIGINAL_PLUGIN_NO_SIGNAL_%s %s unchanged=%d\n",ok?"PASS":"FAIL",file,same);
  obs_source_filter_remove(src,filter);obs_source_release(filter);
 }
 auto* settings=obs_data_create();
 auto path=(root/"filter.afterimage_press.effect").string();
 obs_data_set_string(settings,"shader_file",path.c_str());
 auto* filter=obs_source_create("hypershader_filter","afterimage",settings,nullptr);
 obs_data_release(settings);
 if(!filter)pass=false;
 else {
  obs_source_filter_add(src,filter);
  Frame prev;int treated=0,evolving=0;
  for(int i=0;i<70;i++){
   update(i/6);std::this_thread::sleep_for(36ms);
   auto next=capture(src);
   if(i>15){
    treated+=delta(next,reference)>1000;
    evolving+=!prev.empty()&&delta(next,prev)>100;
   }
   prev=next;
  }
  bool ok=treated>=30&&evolving>=7;pass&=ok;
  printf("ORIGINAL_PLUGIN_FEEDBACK_%s treated=%d evolving=%d\n",ok?"PASS":"FAIL",treated,evolving);
  obs_source_filter_remove(src,filter);obs_source_release(filter);
 }
 obs_set_output_source(0,nullptr);obs_source_dec_showing(src);obs_source_release(src);
 obs_wait_for_destroy_queue();obs_shutdown();
 printf("ORIGINAL_INSTRUMENT_PLUGIN_PASS=%d\n",pass);
 return pass?0:1;
}
