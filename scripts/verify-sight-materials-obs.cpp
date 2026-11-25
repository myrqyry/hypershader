#include <obs.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <graphics/vec3.h>
#include <util/bmem.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
constexpr unsigned W=96,H=64;
using Frame=std::vector<uint8_t>;
static std::string read(const char* path) {
 std::ifstream f(path);return {std::istreambuf_iterator<char>(f),{}};
}
int main(int argc,char**argv){
 if(argc!=3||!obs_startup("en-US",nullptr,nullptr))return 2;
 obs_video_info v{};v.graphics_module="libobs-opengl";v.fps_num=30;v.fps_den=1;
 v.base_width=v.output_width=W;v.base_height=v.output_height=H;v.output_format=VIDEO_FORMAT_RGBA;v.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&v)!=OBS_VIDEO_SUCCESS)return 3;
 auto code=read(argv[1]),helper=read(argv[2]);
 const std::string include="#include \"../../stdlib/semantic_regions.glsl\"";
 auto at=code.find(include);
 if(at==std::string::npos||helper.empty()){puts("Missing semantic material effect or helper");return 4;}
 code.replace(at,include.size(),helper);
 obs_enter_graphics();gs_set_cull_mode(GS_NEITHER);
 char* error=nullptr;auto* effect=gs_effect_create(code.c_str(),"semantic-materials",&error);
 if(error){puts(error);bfree(error);}if(!effect)return 5;
 Frame src(W*H*4),mask(W*H,255);
 for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++){
  const auto i=(y*W+x)*4;src[i]=32+x;src[i+1]=40+y;src[i+2]=120;src[i+3]=255;
 }
 float bounds[256*4]={.10f,.15f,.55f,.85f, .40f,.15f,.85f,.85f, .90f,.15f,.98f,.85f};
 float meta[256*4]={1,.7f,7,0, 0,.95f,8,0, 9,.9f,9,0};
 const uint8_t* data=src.data();auto* input=gs_texture_create(W,H,GS_RGBA,1,&data,0);
 data=mask.data();auto* coverage=gs_texture_create(W,H,GS_R8,1,&data,0);
 data=(const uint8_t*)bounds;auto* boxes=gs_texture_create(256,1,GS_RGBA32F,1,&data,GS_DYNAMIC);
 data=(const uint8_t*)meta;auto* metadata=gs_texture_create(256,1,GS_RGBA32F,1,&data,GS_DYNAMIC);
 Frame depth_forward(W*H),depth_reverse(W*H),foreground(W*H);
 for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++){
  depth_forward[y*W+x]=uint8_t(x*255/(W-1));
  depth_reverse[y*W+x]=255-depth_forward[y*W+x];
  foreground[y*W+x]=y<H/2?255:0;
 }
 data=depth_forward.data();auto* df=gs_texture_create(W,H,GS_R8,1,&data,0);
 data=depth_reverse.data();auto* dr=gs_texture_create(W,H,GS_R8,1,&data,0);
 data=foreground.data();auto* matte=gs_texture_create(W,H,GS_R8,1,&data,0);
 float flows[3][4]={{.06f,0,1,0},{-.06f,0,1,0},{.06f,0,0,0}};
 gs_texture_t* flow[3];
 for(int i=0;i<3;i++){data=(const uint8_t*)flows[i];flow[i]=gs_texture_create(1,1,GS_RGBA32F,1,&data,0);}
 float anchors[3][4]={{0,0,1,0},{-.08f,.04f,1,0},{-.08f,.04f,0,0}};
 gs_texture_t* anchor[3];
 for(int i=0;i<3;i++){data=(const uint8_t*)anchors[i];anchor[i]=gs_texture_create(1,1,GS_RGBA32F,1,&data,0);}
 auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);auto* stage=gs_stagesurface_create(W,H,GS_RGBA);
 if(!input||!coverage||!boxes||!metadata||!df||!dr||!matte||!flow[0]||!flow[1]||!flow[2]||!anchor[0]||!anchor[1]||!anchor[2]||!target||!stage)return 6;
 auto p=[&](const char*n){return gs_effect_get_param_by_name(effect,n);};
 auto integer=[&](const char*n,int x){if(auto*a=p(n))gs_effect_set_int(a,x);};
 auto real=[&](const char*n,float x){if(auto*a=p(n))gs_effect_set_float(a,x);};
 auto texture=[&](const char*n,gs_texture_t*x){if(auto*a=p(n))gs_effect_set_texture(a,x);};
 auto color=[&](const char*n,float r,float g,float b){vec4 q;vec4_set(&q,r,g,b,1);if(auto*a=p(n))gs_effect_set_vec4(a,&q);};
 auto render=[&](bool present,int count,int a,int b,int style,float time,float mix,bool reverse=false,int c=-1,int depth=0,int motion=0,bool subject=false,float fusion=1,int history=0,float lock=1){
  Frame result;gs_texrender_reset(target);if(!gs_texrender_begin(target,W,H))return result;
  gs_projection_push();gs_matrix_push();gs_matrix_identity();gs_ortho(0,W,0,H,-100,100);
  gs_blend_state_push();gs_enable_blending(false);
  float bb[256*4],mm[256*4];memcpy(bb,bounds,sizeof bb);memcpy(mm,meta,sizeof mm);
  if(reverse)for(int k=0;k<4;k++){std::swap(bb[k],bb[k+4]);std::swap(mm[k],mm[k+4]);}
  gs_texture_set_image(boxes,(const uint8_t*)bb,sizeof bb,false);
  gs_texture_set_image(metadata,(const uint8_t*)mm,sizeof mm,false);
  texture("image",input);texture("hs_sight_semantic_mask",coverage);texture("hs_sight_semantic_bounds",boxes);texture("hs_sight_semantic_meta",metadata);
  vec3 resolution;vec3_set(&resolution,W,H,float(W)/H);if(auto*x=p("hs_resolution"))gs_effect_set_vec3(x,&resolution);
  real("hs_sight_semantic_present",present);integer("hs_sight_semantic_count",count);
  integer("category_a",a);integer("category_b",b);integer("category_c",c);
  integer("material_a",style);integer("material_b",0);integer("material_c",0);
  color("color_a",1,0,0);color("color_b",0,1,0);color("color_c",0,0,1);
  texture("hs_sight_depth",depth==2?dr:df);
  texture("hs_segmentation",matte);
  texture("hs_sight_flow",flow[motion>0?motion-1:0]);
  real("hs_sight_depth_present",depth!=0);real("hs_sight_matte_present",subject);
  real("hs_sight_flow_present",motion!=0);real("hs_sight_flow_interval",.033f);
  real("hs_sight_flow_coverage",1);
  texture("hs_sight_surface_uv",anchor[history>0?history-1:0]);
  real("hs_sight_surface_present",history>0);real("surface_lock",lock);integer("surface_preview",0);
  real("depth_mix",fusion);real("surface_relief",.5f);real("flow_mix",fusion);
  real("matte_mix",fusion);real("normal_radius",4);real("edge_tolerance",.2f);
  real("mix_a",mix);real("mix_b",mix);real("mix_c",mix);real("scale",8);real("speed",1);real("hs_time",time);
  while(gs_effect_loop(effect,"Draw"))gs_draw_sprite(input,0,W,H);
  gs_blend_state_pop();gs_matrix_pop();gs_projection_pop();gs_texrender_end(target);
  gs_stage_texture(stage,gs_texrender_get_texture(target));uint8_t* px=nullptr;uint32_t pitch=0;
  if(gs_stagesurface_map(stage,&px,&pitch)){result.resize(W*H*4);for(unsigned y=0;y<H;y++)memcpy(result.data()+y*W*4,px+y*pitch,W*4);gs_stagesurface_unmap(stage);}
  return result;
 };
 auto rgb=[&](const Frame& f,unsigned x,unsigned y,int r,int g,int b){
  if(f.size()!=src.size())return false;
  auto i=(y*W+x)*4;return abs(int(f[i])-r)<=1&&abs(int(f[i+1])-g)<=1&&abs(int(f[i+2])-b)<=1&&f[i+3]==255;
 };
 auto same=[&](const Frame& f,unsigned x,unsigned y){auto i=(y*W+x)*4;return f.size()==src.size()&&!memcmp(f.data()+i,src.data()+i,4);};
 auto solid=render(true,3,1,0,0,0,1);
 bool routing=rgb(solid,20,32,255,0,0)&&rgb(solid,70,32,0,255,0)&&rgb(solid,45,32,255,0,0)&&same(solid,92,32)&&same(solid,2,2);
 bool order=solid==render(true,3,1,0,0,0,1,true);
 bool fallback=render(false,3,1,0,0,0,1)==src&&render(true,0,1,0,0,0,1)==src&&render(true,3,1,0,0,0,0)==src;
 bool reassigned=rgb(render(true,3,0,1,0,0,1),45,32,255,0,0);
 auto h0=render(true,3,1,0,2,0,1),h1=render(true,3,1,0,2,.37f,1);
 bool animated=h0!=h1&&h0!=solid&&rgb(h0,70,32,0,255,0)&&rgb(h1,70,32,0,255,0)&&same(h0,2,2);
 bool third=rgb(render(true,3,1,0,0,0,1,false,9),92,32,0,0,255);
 auto ink=render(true,3,1,0,1,0,1),metal=render(true,3,1,0,3,0,1),mosaic=render(true,3,1,0,4,0,1);
 bool styles=ink!=solid&&metal!=solid&&mosaic!=solid&&ink!=metal&&metal!=mosaic&&ink!=mosaic&&
    rgb(ink,70,32,0,255,0)&&rgb(metal,70,32,0,255,0)&&rgb(mosaic,70,32,0,255,0)&&
    same(ink,2,2)&&same(metal,2,2)&&same(mosaic,2,2);
 auto front=render(true,3,1,0,3,0,1,false,-1,1);
 auto back=render(true,3,1,0,3,0,1,false,-1,2);
 bool depth_ok=front!=back&&front!=metal&&rgb(front,70,32,0,255,0)&&same(front,2,2);
 auto forward=render(true,3,1,0,2,0,1,false,-1,0,1);
 auto backward=render(true,3,1,0,2,0,1,false,-1,0,2);
 bool flow_ok=forward!=backward&&forward!=h0&&rgb(forward,70,32,0,255,0)&&same(forward,2,2)&&
     render(true,3,1,0,2,0,1,false,-1,0,3)==h0;
 auto cutout=render(true,3,1,0,0,0,1,false,-1,0,0,true);
 bool matte_ok=rgb(cutout,20,20,255,0,0)&&same(cutout,20,42)&&same(cutout,70,42)&&same(cutout,2,2);
 bool bypass=render(true,3,1,0,3,0,1,false,-1,1,1,true,0)==metal;
 auto pinned=render(true,3,1,0,2,0,1,false,-1,0,0,false,1,1);
 auto advected=render(true,3,1,0,2,0,1,false,-1,0,0,false,1,2);
 bool anchored=pinned!=advected&&rgb(advected,70,32,0,255,0)&&same(advected,2,2)&&
    render(true,3,1,0,2,0,1,false,-1,0,0,false,1,3)==h0&&
    render(true,3,1,0,2,0,1,false,-1,0,0,false,1,2,0)==h0;
 // A fully locked coordinate should not also apply instantaneous motion.
 bool no_double=render(true,3,1,0,2,0,1,false,-1,0,1,false,1,2)==advected;
 bool pass=routing&&order&&fallback&&reassigned&&animated&&third&&styles&&depth_ok&&flow_ok&&matte_ok&&bypass&&anchored&&no_double;
 printf("SEMANTIC_MATERIALS_GPU routing=%d order=%d fallback=%d reassigned=%d animated=%d third=%d styles=%d depth=%d flow=%d matte=%d fusion_bypass=%d anchored=%d no_double=%d PASS=%d\n",routing,order,fallback,reassigned,animated,third,styles,depth_ok,flow_ok,matte_ok,bypass,anchored,no_double,pass);
 for(auto*t:anchor)gs_texture_destroy(t);
 for(auto*t:flow)gs_texture_destroy(t);gs_texture_destroy(matte);gs_texture_destroy(dr);gs_texture_destroy(df);
 gs_stagesurface_destroy(stage);gs_texrender_destroy(target);gs_texture_destroy(metadata);gs_texture_destroy(boxes);gs_texture_destroy(coverage);gs_texture_destroy(input);gs_effect_destroy(effect);
 obs_leave_graphics();obs_shutdown();return pass?0:1;
}
