// Controlled native GPU inputs for shipped single-pass Sight effects.
// These are shader fixtures, not camera/model-inference tests.
#include <obs.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <graphics/vec3.h>
#include <graphics/vec2.h>
#include <util/bmem.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <cmath>
constexpr unsigned W=64,H=48;
using Frame=std::vector<uint8_t>;
int main(int argc,char**argv){
 if(argc!=2||!obs_startup("en-US",nullptr,nullptr))return 2;
 obs_video_info v{};v.graphics_module="libobs-opengl";v.fps_num=30;v.fps_den=1;
 v.base_width=v.output_width=W;v.base_height=v.output_height=H;v.output_format=VIDEO_FORMAT_RGBA;v.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&v)!=OBS_VIDEO_SUCCESS)return 3;
 obs_enter_graphics();gs_set_cull_mode(GS_NEITHER);
 Frame src(W*H*4),mask(W*H*4),face(256*256*4);
 for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++){
  auto i=(y*W+x)*4;src[i]=x*3+20;src[i+1]=y*4+20;src[i+2]=80;src[i+3]=255;
  mask[i]=x>=W/2?255:0;mask[i+3]=255;
 }
 for(unsigned y=0;y<256;y++)for(unsigned x=0;x<256;x++){
  auto i=(y*256+x)*4;face[i]=1;face[i+1]=x<128?0:85;face[i+2]=85;face[i+3]=255;
 }
 const uint8_t*data=src.data();auto*input=gs_texture_create(W,H,GS_RGBA,1,&data,0);
 data=mask.data();auto*matte=gs_texture_create(W,H,GS_RGBA,1,&data,0);
 data=face.data();auto*surface=gs_texture_create(256,256,GS_RGBA,1,&data,0);
 float flow[4]={0.08f,0.0f,1.0f,0.0f};data=reinterpret_cast<const uint8_t*>(flow);
 auto*motion=gs_texture_create(1,1,GS_RGBA32F,1,&data,0);
 auto*target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);auto*stage=gs_stagesurface_create(W,H,GS_RGBA);
 bool all=input&&matte&&surface&&motion&&target&&stage;
 if(!all)return 4;
 auto equalPixel=[](const Frame&a,const Frame&b,unsigned x,unsigned y){
  if(a.size()!=W*H*4||b.size()!=a.size())return false;
  auto i=(y*W+x)*4;for(int k=0;k<4;k++)if(std::abs(int(a[i+k])-int(b[i+k]))>1)return false;return true;
 };
 for(auto name:{"filter.sight_flow_warp.effect","filter.sight_motion_pulse.effect","filter.sight_subject_frame.effect","filter.sight_text_scramble.effect","filter.face_mesh_wireframe.effect"}){
  char*err=nullptr;auto*effect=gs_effect_create_from_file((std::string(argv[1])+"/"+name).c_str(),&err);
  if(err){fprintf(stderr,"%s: %s\n",name,err);bfree(err);}
  if(!effect){all=false;continue;}
  auto p=[&](const char*n){return gs_effect_get_param_by_name(effect,n);};
  auto f=[&](const char*n,float x){if(auto*a=p(n))gs_effect_set_float(a,x);};
  auto i=[&](const char*n,int x){if(auto*a=p(n))gs_effect_set_int(a,x);};
  auto b=[&](const char*n,bool x){if(auto*a=p(n))gs_effect_set_bool(a,x);};
  auto t=[&](const char*n,gs_texture_t*x){if(auto*a=p(n))gs_effect_set_texture(a,x);};
  auto v4=[&](const char*n,float x,float y,float z,float w){struct vec4 q;vec4_set(&q,x,y,z,w);if(auto*a=p(n))gs_effect_set_vec4(a,&q);};
  auto draw=[&](bool present,float strength,bool debug,int treatment){
   Frame result;gs_texrender_reset(target);if(!gs_texrender_begin(target,W,H))return result;
   gs_projection_push();gs_matrix_push();gs_matrix_identity();gs_ortho(0,W,0,H,-100,100);
   gs_blend_state_push();gs_enable_blending(false);
   t("image",input);t("hs_segmentation",matte);t("hs_sight_text_mask",matte);
   t("hs_sight_flow",motion);t("hs_face_surface",surface);
   struct vec3 size;vec3_set(&size,W,H,float(W)/H);if(auto*a=p("hs_resolution"))gs_effect_set_vec3(a,&size);
   f("hs_sight_flow_present",present);f("hs_sight_flow_interval",.033f);f("hs_sight_flow_coverage",1);
   f("warp_strength",strength);b("debug_flow",debug);
   f("hs_sight_motion_present",present);f("hs_sight_matte_present",present);
   f("hs_sight_motion_energy",.1f);f("sensitivity",8);f("pulse_mix",strength);
   f("audio_mix",0);f("audio_beat",0);v4("pulse_color",.15f,.85f,1,1);b("show_motion",debug);
   f("hs_sight_subject_present",present);v4("hs_sight_subject_bounds",.25f,.25f,.75f,.75f);
   struct vec2 center;vec2_set(&center,.5f,.5f);if(auto*a=p("hs_sight_subject_center"))gs_effect_set_vec2(a,&center);
   f("hs_sight_subject_coverage",.5f);f("line_width",2);f("frame_opacity",strength);
   v4("frame_color",.14f,.92f,.8f,1);b("show_center",true);b("show_coverage",true);
   f("hs_sight_text_present",present);i("hs_sight_text_count",1);i("treatment",treatment);
   f("hs_time",1);f("rate",12);f("block_size",8);v4("noise_color",.15f,.85f,1,1);
   f("hs_face_present",present);f("thickness",.02f);f("strength",strength);v4("wireColor",0,.9f,.9f,1);
   while(gs_effect_loop(effect,"Draw"))gs_draw_sprite(input,0,W,H);
   gs_blend_state_pop();gs_matrix_pop();gs_projection_pop();gs_texrender_end(target);
   gs_stage_texture(stage,gs_texrender_get_texture(target));uint8_t*px=nullptr;uint32_t pitch=0;
   if(gs_stagesurface_map(stage,&px,&pitch)){result.resize(W*H*4);for(unsigned y=0;y<H;y++)memcpy(result.data()+y*W*4,px+y*pitch,W*4);gs_stagesurface_unmap(stage);}
   return result;
  };
  auto active=draw(true,1,false,1);auto absent=draw(false,1,false,1);
  bool ok=active.size()==src.size()&&active!=src&&absent==src;
  std::string n=name;
  if(n.find("text_scramble")!=std::string::npos){
   auto black=(H/2*W+3*W/4)*4;ok&=equalPixel(active,src,W/4,H/2)&&active.size()==src.size()&&active[black]==0&&active[black+1]==0&&active[black+2]==0&&active[black+3]==255;
   auto noise=draw(true,1,false,0);ok&=noise!=active&&equalPixel(noise,src,W/4,H/2);
  }else{
   ok&=draw(true,0,false,1)==src;
   if(n.find("motion_pulse")!=std::string::npos)ok&=equalPixel(active,src,8,12)&&!equalPixel(active,src,56,12);
   if(n.find("subject_frame")!=std::string::npos)ok&=equalPixel(active,src,1,1)&&!equalPixel(active,src,32,24);
   if(n.find("face_mesh")!=std::string::npos)ok&=!equalPixel(active,src,8,12)&&equalPixel(active,src,56,12);
  }
  printf("SIGHT_SIMPLE %s active=%d absent=%d PASS=%d\n",name,active!=src,absent==src,ok);
  all&=ok;gs_effect_destroy(effect);
 }
 gs_stagesurface_destroy(stage);gs_texrender_destroy(target);gs_texture_destroy(motion);gs_texture_destroy(surface);gs_texture_destroy(matte);gs_texture_destroy(input);
 obs_leave_graphics();obs_shutdown();return all?0:1;
}
