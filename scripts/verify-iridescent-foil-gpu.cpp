// Real OBS/OpenGL GPU test of moving iridescent foil, including light/time changes and exact bypass.
#include <obs.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
constexpr unsigned W=192,H=128;
using Frame=std::vector<uint8_t>;
Frame fixture() {
 Frame f(W*H*4,0);
 for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
  size_t i=4*(y*W+x);
  bool highlight=x>=90&&x<102&&y>=57&&y<69;
  bool transparent=x<12;
  f[i]=transparent?0:(highlight?250:12);
  f[i+1]=transparent?0:(highlight?244:12);
  f[i+2]=transparent?0:(highlight?222:12);
  f[i+3]=transparent?0:255;
 }
 return f;
}
void fparam(gs_effect_t* effect,const char* key,float n) {
 auto* p=gs_effect_get_param_by_name(effect,key);
 if(p)gs_effect_set_float(p,n);
}
Frame draw(gs_effect_t* effect,gs_texture_t* input,float intensity,float threshold,float timeSeconds=0.7f) {
 gs_set_cull_mode(GS_NEITHER);
 auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
 auto* stage=gs_stagesurface_create(W,H,GS_RGBA);
 Frame output;
 if(target&&stage&&gs_texrender_begin(target,W,H)) {
  gs_projection_push();gs_matrix_push();gs_matrix_identity();
  gs_ortho(0,float(W),0,float(H),-100,100);
  gs_effect_set_texture(gs_effect_get_param_by_name(effect,"image"),input);
  vec3 resolution{};vec3_set(&resolution,float(W),float(H),float(W)/H);
  gs_effect_set_vec3(gs_effect_get_param_by_name(effect,"hs_resolution"),&resolution);
  fparam(effect,"foil_strength",intensity);
  fparam(effect,"foil_light_x",threshold);
  fparam(effect,"foil_light_y",0.75f);
  fparam(effect,"foil_scale",1.25f);
  fparam(effect,"foil_relief",1.3f);
  fparam(effect,"foil_thickness",2.0f);
  fparam(effect,"foil_saturation",0.7f);
  fparam(effect,"foil_specular",0.65f);
  fparam(effect,"foil_speed",0.4f);
  fparam(effect,"hs_time",timeSeconds);
  while(gs_effect_loop(effect,"Draw"))gs_draw_sprite(input,0,W,H);
  gs_matrix_pop();gs_projection_pop();gs_texrender_end(target);
  auto* tex=gs_texrender_get_texture(target);
  if(tex) {
   gs_stage_texture(stage,tex);
   uint8_t* ptr=nullptr;uint32_t pitch=0;
   if(gs_stagesurface_map(stage,&ptr,&pitch)) {
    output.resize(W*H*4);
    for(unsigned y=0;y<H;++y)
     std::memcpy(output.data()+y*W*4,ptr+y*pitch,W*4);
    gs_stagesurface_unmap(stage);
   }
  }
 }
 if(stage)gs_stagesurface_destroy(stage);
 if(target)gs_texrender_destroy(target);
 return output;
}
size_t pixel_diff(const Frame& a,const Frame& b) {
 if(a.size()!=b.size())return SIZE_MAX;
 size_t count=0;
 for(size_t i=0;i<a.size();i+=4)
  for(int j=0;j<4;++j)count+=std::abs(int(a[i+j])-int(b[i+j]))>2;
 return count;
}
int main(int argc,char** argv) {
 if(argc!=2)return 2;
 if(!obs_startup("en-US",nullptr,nullptr))return 1;
 obs_video_info video{};
 video.graphics_module="libobs-opengl";
 video.fps_num=30;video.fps_den=1;
 video.base_width=W;video.base_height=H;
 video.output_width=W;video.output_height=H;
 video.output_format=VIDEO_FORMAT_RGBA;
 video.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&video)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 1;}
 bool ok=false;
 obs_enter_graphics();
 char* err=nullptr;
 auto* effect=gs_effect_create_from_file(argv[1],&err);
 if(err){std::cerr<<"Shader compile: "<<err<<"\n";bfree(err);}
 if(effect) {
  auto original=fixture();
  const uint8_t* data=original.data();
  auto* tex=gs_texture_create(W,H,GS_RGBA,1,&data,0);
  if(tex) {
   auto on=draw(effect,tex,1.0f,0.30f);
   auto off=draw(effect,tex,0.0f,0.30f);
   auto moved_light=draw(effect,tex,1.0f,0.95f);
   auto moved_time=draw(effect,tex,1.0f,0.30f,4.7f);
   size_t time_diff = pixel_diff(on, moved_time);
   size_t visible = pixel_diff(on, original);
   size_t zero = pixel_diff(off, original);
   size_t lighting = pixel_diff(on, moved_light);
   bool alpha_ok=on.size()==original.size() && off.size()==original.size();
   if(alpha_ok) for(size_t i=3;i<on.size();i+=4) {
     if(on[i]!=original[i] || off[i]!=original[i]) alpha_ok=false;
   }
   std::cout<<"FOIL_GPU_COMPILED=1 VISIBLE_PIXELS="<<visible
            <<" ZERO_MIX_DELTA="<<zero
            <<" LIGHT_MOVEMENT_PIXELS="<<lighting
            <<" TIME_MOVEMENT_PIXELS="<<time_diff
            <<" ALPHA_PRESERVED="<<alpha_ok<<"\n";
   ok = visible > 500 && zero == 0 && lighting > 100 && time_diff > 100 && alpha_ok;
   gs_texture_destroy(tex);
  }
  gs_effect_destroy(effect);
 }
 obs_leave_graphics();
 obs_shutdown();
 if(ok)std::cout<<"PASS: native foil renders changing folded interference, responds to light, preserves alpha and bypasses cleanly\n";
 return ok?0:1;
}
