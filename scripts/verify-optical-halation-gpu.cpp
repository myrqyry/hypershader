// Real OBS OpenGL GPU test of highlight-only halation; no shader mocks.
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
  f[i]=highlight?250:12;
  f[i+1]=highlight?244:12;
  f[i+2]=highlight?222:12;
  f[i+3]=255;
 }
 return f;
}
void fparam(gs_effect_t* effect,const char* key,float n) {
 auto* p=gs_effect_get_param_by_name(effect,key);
 if(p)gs_effect_set_float(p,n);
}
Frame draw(gs_effect_t* effect,gs_texture_t* input,float intensity,float threshold) {
 gs_set_cull_mode(GS_NEITHER);
 auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
 auto* stage=gs_stagesurface_create(W,H,GS_RGBA);
 Frame output;
 if(target&&stage&&gs_texrender_begin(target,W,H)) {
  gs_projection_push();gs_matrix_push();gs_matrix_identity();
  gs_ortho(0,float(W),0,float(H),-100,100);
  gs_effect_set_texture(gs_effect_get_param_by_name(effect,"image"),input);
  vec3 resolution{};vec3_set(&resolution,float(W),float(H),float(W)/H);
  gs_effect_set_vec3(gs_effect_get_param_by_name(effect,"iResolution"),&resolution);
  fparam(effect,"bloom_intensity",intensity);
  fparam(effect,"bloom_threshold",threshold);
  fparam(effect,"bloom_knee",.18f);
  fparam(effect,"bloom_radius",5.f);
  fparam(effect,"halation_warmth",.8f);
  fparam(effect,"halo_spread",3.f);
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
   auto on=draw(effect,tex,1.5f,.72f);
   auto off=draw(effect,tex,0.f,.72f);
   auto no_highlights=draw(effect,tex,1.5f,1.40f);
   size_t halo_pixels=0, red_dom=0;
   bool alpha_ok=on.size()==original.size();
   if(alpha_ok)for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
    auto i=4*(y*W+x);
    if(on[i+3]!=255)alpha_ok=false;
    bool square=x>=90&&x<102&&y>=57&&y<69;
    if(!square && on[i]>original[i]+4){
     ++halo_pixels;
     if(on[i]>on[i+1]+1)++red_dom;
    }
   }
   size_t no_strength_delta=pixel_diff(off,original);
   size_t no_highlights_delta=pixel_diff(no_highlights,original);
   size_t strength_delta=pixel_diff(on,original);
   std::cout<<"HALATION_GPU_COMPILED=1 HALO_PIXELS="<<halo_pixels
            <<" WARM_PIXELS="<<red_dom
            <<" HIGH_STRENGTH_DELTA="<<strength_delta
            <<" ZERO_STRENGTH_DELTA="<<no_strength_delta
            <<" NO_HIGHLIGHTS_DELTA="<<no_highlights_delta
            <<" ALPHA_OK="<<alpha_ok<<"\n";
   ok=halo_pixels>45 && red_dom>20 && strength_delta>120 &&
      no_strength_delta==0 && no_highlights_delta==0 && alpha_ok;
   gs_texture_destroy(tex);
  }
  gs_effect_destroy(effect);
 }
 obs_leave_graphics();
 obs_shutdown();
 if(ok)std::cout<<"PASS: native bloom diffuses highlights, warms fringe, preserves dark/alpha, and bypasses cleanly\n";
 return ok?0:1;
}
