// Native libobs GPU readback of positionable RGB, Focus Bulge, and Noise.
// Checks independent strength/center response and alpha preservation.
#include <algorithm>
#include <fstream>
#include <cstdlib>
// Real HyperShaderFX async filter test: native shader loading, property
// controls, bright moving frames, halo generation and shader bypass.
// The filter and frame readbacks run in real libobs/OpenGL.
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
 if(argc!=5)return 2;
 std::string mode=argv[4];
 if(mode!="rgb"&&mode!="bulge"&&mode!="noise"&&mode!="defaults")return 2;
 if(!obs_startup("en-US",nullptr,nullptr))return 3;
 obs_video_info info{};
 info.graphics_module="libobs-opengl";info.fps_num=30;info.fps_den=1;
 info.base_width=W;info.base_height=H;info.output_width=W;info.output_height=H;
 info.output_format=VIDEO_FORMAT_RGBA;info.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&info)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 4;}
 obs_register_source(&source_info);
 obs_module_t* module=nullptr;
 if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS || !obs_init_module(module)) {
    obs_shutdown();return 5;
 }
 auto* video=obs_source_create("hyper_halation_test_source","Color source",nullptr,nullptr);
 auto* opts=obs_data_create();
 obs_data_set_string(opts,"shader_file",argv[3]);
 const std::string prefix="param_";
 const char* keys[3]={ "shift_strength","bulge_strength","noise_strength" };
 const char* strength=keys[mode=="rgb"?0:(mode=="bulge"?1:2)];
 if(mode!="defaults") {
  obs_data_set_double(opts,(prefix+strength).c_str(),0.0);
  obs_data_set_double(opts,"param_center_x",0.5);
  obs_data_set_double(opts,"param_center_y",0.5);
  obs_data_set_double(opts,"param_radius",0.4);
  // Freeze moving noise for the center-movement comparison.
  obs_data_set_double(opts,"param_grain_speed",0.0);
 }
 auto* effect=obs_source_create("hypershader_filter","Real world source effect",opts,nullptr);
 obs_data_release(opts);
 if(!video || !effect){obs_shutdown();return 6;}
 obs_source_filter_add(video,effect);obs_source_inc_showing(video);
 Frame input=make_frame(54);
 for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x) {
   size_t i=4*(y*W+x);
   input[i]=uint8_t(25+205*x/(W-1));
   input[i+1]=uint8_t(15+205*y/(H-1));
   input[i+2]=uint8_t(25+160*(x+y)/(W+H-2));
 }
 obs_enter_graphics();gs_texture_set_image(active->texture,input.data(),W*4,false);obs_leave_graphics();
 auto capture=[&](int count=35){
   Frame frame;
   for(int i=0;i<count;i++){frame=render(video);std::this_thread::sleep_for(26ms);}
   return frame;
 };
 auto settings=[&](const char* key,double value){
    auto* values=obs_source_get_settings(effect);
    obs_data_set_double(values,(prefix+key).c_str(),value);
    obs_source_update(effect,values);obs_data_release(values);
 };
 if(mode=="defaults") {
  // Load the real effect, then perform a normal unrelated OBS source update.
  // Metadata defaults must persist in the source's live settings, not just
  // an async task snapshot, so this update cannot reset the GPU to zero.
  auto initial=capture(55);
  auto* saved=obs_source_get_settings(effect);
  bool hadDefaults=obs_data_has_user_value(saved,"param_noise_strength") &&
      obs_data_get_double(saved,"param_noise_strength")>0.15 &&
      obs_data_get_double(saved,"param_radius")>0.35;
  obs_data_set_double(saved,"speed",0.95);
  obs_source_update(effect,saved);
  obs_data_release(saved);
  auto updated=capture(20);
  auto* persisted=obs_source_get_settings(effect);
  bool stillDefault=obs_data_has_user_value(persisted,"param_noise_strength") &&
      obs_data_get_double(persisted,"param_noise_strength")>0.15;
  obs_data_release(persisted);
  size_t visible=diff(updated,input);
  bool ok=hadDefaults&&stillDefault&&visible>100&&updated.size()==input.size();
  std::cout<<"REAL_WORLD_DEFAULTS saved="<<hadDefaults<<" after_update="<<stillDefault
           <<" pixels="<<visible<<" PASS="<<ok<<"\n";
  obs_source_dec_showing(video);
  obs_source_filter_remove(video,effect);
  obs_source_release(effect);obs_source_release(video);
  obs_shutdown();return ok?0:1;
 }
 auto neutral=capture();
 auto* props=obs_source_properties(effect);
 bool controls=props && obs_properties_get(props,(prefix+strength).c_str()) &&
      obs_properties_get(props,"param_center_x") &&
      obs_properties_get(props,"param_center_y") &&
      obs_properties_get(props,"param_radius");
 if(props)obs_properties_destroy(props);
 settings(strength,mode=="bulge"?0.85:(mode=="rgb"?0.07:0.5));
 auto activeFrame=capture(23);
 settings("center_x",0.22);
 settings("center_y",0.22);
 auto moved=capture(23);
 settings(strength,0.0);
 auto bypass=capture(23);
 bool alpha=true;
 for(const auto* fr:{&neutral,&activeFrame,&moved,&bypass}) {
    if(fr->size()!=input.size()){alpha=false;break;}
    for(size_t i=3;i<input.size();i+=4)if((*fr)[i]!=input[i]){alpha=false;break;}
 }
 if(const char* dir=std::getenv("HYPER_LOCAL_CAPTURE_DIR")) {
  const std::vector<std::pair<const char*,const Frame*>> frames={
      {"neutral",&neutral},{"active",&activeFrame},{"moved",&moved},{"bypass",&bypass}};
  for(const auto& frame:frames) {
   if(frame.second->size()!=input.size())continue;
   std::ofstream file(std::string(dir)+"/"+mode+"-"+frame.first+".ppm",std::ios::binary);
   file<<"P6\n"<<W<<" "<<H<<"\n255\n";
   for(size_t i=0;i<frame.second->size();i+=4)
      file.write(reinterpret_cast<const char*>(&(*frame.second)[i]),3);
  }
 }
 size_t off=diff(neutral,input);
 size_t active=diff(activeFrame,neutral);
 size_t movedDiff=diff(moved,activeFrame);
 size_t recovered=diff(bypass,input);
 bool pass=controls&&alpha&&off<22&&active>200&&movedDiff>100&&recovered<22;
 std::cout<<"REAL_WORLD_RESULT effect="<<mode<<" controls="<<controls
          <<" neutral_diff="<<off<<" active_diff="<<active
          <<" moved_diff="<<movedDiff<<" bypass_diff="<<recovered
          <<" alpha="<<alpha<<" PASS="<<pass<<"\n";
 obs_source_dec_showing(video);
 obs_source_filter_remove(video,effect);
 obs_source_release(effect);obs_source_release(video);
 obs_shutdown();return pass?0:1;
}
