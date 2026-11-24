// Focused native libobs/OpenGL pixel probe. Signal textures are explicit
// fixtures; this test makes no claim to exercise model inference.
#include <obs.h>
#include <graphics/graphics.h>
#include <graphics/vec3.h>
#include <png.h>
#include <util/bmem.h>
#include <array>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <cmath>
namespace fs=std::filesystem;
constexpr int W=320,H=192;
using Frame=std::vector<uint8_t>;
struct Textures{gs_texture_t* video;gs_texture_t* depth;gs_texture_t* hands;gs_texture_t* old;};
static void sf(gs_effect_t* e,const char* name,float val){
 if(auto* p=gs_effect_get_param_by_name(e,name))gs_effect_set_float(p,val);
}
static void sv(gs_effect_t* e,const char* name,float a,float b,float c){
 if(auto* p=gs_effect_get_param_by_name(e,name)){
  vec3 v;vec3_set(&v,a,b,c);gs_effect_set_vec3(p,&v);
 }
}
static void si(gs_effect_t* e,const char* name,int val){
 if(auto* p=gs_effect_get_param_by_name(e,name))gs_effect_set_int(p,val);
}
static void st(gs_effect_t* e,const char* name,gs_texture_t* val){
 if(auto* p=gs_effect_get_param_by_name(e,name))gs_effect_set_texture(p,val);
}
static Textures fixture(){
 Frame src(W*H*4),depth(W*H*4),old(W*H*4);
 for(int y=0;y<H;y++)for(int x=0;x<W;x++){
  const int i=(y*W+x)*4;
  int grid=((x/17+y/17)%2)*18;
  src[i]=uint8_t(42+x*150/W+grid);
  src[i+1]=uint8_t(25+y*170/H+grid);
  src[i+2]=uint8_t(40+(x+y)*100/(W+H)+grid);
  src[i+3]=x<8?0:255;
  if(x>W/3&&x<2*W/3&&y>H/3&&y<2*H/3){
   src[i]=226;src[i+1]=177;src[i+2]=50;
  }
  uint8_t z=uint8_t(7+x*243/W);
  depth[i]=z;depth[i+1]=z;depth[i+2]=z;depth[i+3]=255;
  old[i]=194;old[i+1]=24;old[i+2]=100;old[i+3]=255;
 }
 const uint8_t* ptr=src.data();auto* a=gs_texture_create(W,H,GS_RGBA,1,&ptr,0);
 ptr=depth.data();auto* b=gs_texture_create(W,H,GS_RGBA,1,&ptr,0);
 ptr=old.data();auto* c=gs_texture_create(W,H,GS_RGBA,1,&ptr,0);
 std::array<float,21*2*4> joints{};
 auto put=[&](int k,float x,float y){joints[k*4]=x;joints[k*4+1]=y;joints[k*4+3]=1;};
 put(0,.45,.76);put(4,.37,.46);put(8,.48,.28);
 put(12,.54,.24);put(16,.61,.32);put(20,.68,.47);
 ptr=reinterpret_cast<const uint8_t*>(joints.data());
 auto* h=gs_texture_create(21,2,GS_RGBA32F,1,&ptr,0);
 return {a,b,h,c};
}
static Frame render(gs_effect_t* fx,const Textures& t,int kind,int state,int soloBand=-1){
 st(fx,"image",t.video);st(fx,"iSightDepth",t.depth);
 st(fx,"iSightHands",t.hands);st(fx,"iPreviousFrame",state==2?t.depth:t.old);
 vec3 r;vec3_set(&r,W,H,1);
 if(auto* p=gs_effect_get_param_by_name(fx,"iResolution"))gs_effect_set_vec3(p,&r);
 sf(fx,"iTime",soloBand>=0?1.25f:(state==0?1.25f:state==1?2.65f:4.15f));
 for(int i=0;i<8;i++){char name[24];snprintf(name,sizeof(name),"audio_band%d",i);
  float value=soloBand>=0?(state>0&&i==soloBand?.95f:0.f)
                          :(state==0?0.f:state==1?.28f+i*.07f:.95f);
  sf(fx,name,value);}
 sf(fx,"audio_kick",soloBand>=0?0.f:(state==0?0.f:.73f));
 sf(fx,"audio_envelope",soloBand>=0?0.f:(state==0?0.f:.72f));
 sf(fx,"audio_beat",state==0?0.f:.81f);
 sf(fx,"audio_flux",state==0?0.f:.94f);
 sf(fx,"audio_beat_phase",state==0?0.f:.61f);
 sf(fx,"iSightPresent",kind==2&&state>0?1.f:0.f);
 si(fx,"iSightHandCount",kind==2&&state>0?1:0);
 sf(fx,"iSightPinch0",state==2?1.f:0.2f);
 sf(fx,"iSightPinch1",0.0f);
 sf(fx,"iSightDepthPresent",kind==1&&state>0?1.f:0.f);
 if(kind==3){sf(fx,"effect_mix",state==0?0.f:.88f);
  sf(fx,"history",state==2?.99f:.74f);}
 auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
 auto* stage=gs_stagesurface_create(W,H,GS_RGBA);
 Frame out;
 gs_set_cull_mode(GS_NEITHER);
 if(target&&stage&&gs_texrender_begin(target,W,H)){
  gs_projection_push();gs_matrix_push();gs_matrix_identity();
  gs_ortho(0.f,float(W),0.f,float(H),-100.f,100.f);
  while(gs_effect_loop(fx,"Draw"))gs_draw_sprite(t.video,0,W,H);
  gs_matrix_pop();gs_projection_pop();gs_texrender_end(target);
  if(auto* tex=gs_texrender_get_texture(target)){
   gs_stage_texture(stage,tex);
   uint8_t* pixels=nullptr;uint32_t pitch=0;
   if(gs_stagesurface_map(stage,&pixels,&pitch)){
    out.resize(W*H*4);
    for(int y=0;y<H;y++)memcpy(out.data()+y*W*4,pixels+y*pitch,W*4);
    gs_stagesurface_unmap(stage);
   }
  }
 }
 if(stage)gs_stagesurface_destroy(stage);
 if(target)gs_texrender_destroy(target);
 return out;
}
static size_t rgbDiff(const Frame& a,const Frame& b){
 if(a.empty()||a.size()!=b.size())return 0;
 size_t n=0;for(size_t i=0;i<a.size();i+=4)for(int c=0;c<3;c++)
  n+=abs(int(a[i+c])-int(b[i+c]))>3;
 return n;
}
static size_t aDiff(const Frame& a,const Frame& b){
 if(a.empty()||a.size()!=b.size())return 999999;
 size_t n=0;for(size_t i=3;i<a.size();i+=4)n+=a[i]!=b[i];return n;
}
static bool png(const fs::path& name,const Frame& rgba){
 if(rgba.size()!=W*H*4)return false;
 png_image img{};img.version=PNG_IMAGE_VERSION;img.width=W;img.height=H;
 img.format=PNG_FORMAT_RGBA;
 return png_image_write_to_file(&img,name.c_str(),0,rgba.data(),W*4,nullptr)!=0;
}
int main(int argc,char**argv){
 if(argc!=3)return 2;
 if(!obs_startup("en-US",nullptr,nullptr))return 3;
 obs_video_info v{};v.graphics_module="libobs-opengl";v.fps_num=30;v.fps_den=1;
 v.base_width=W;v.base_height=H;v.output_width=W;v.output_height=H;
 v.output_format=VIDEO_FORMAT_RGBA;v.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&v)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 4;}
 struct Case{const char* file;int kind;size_t minChange;} cases[]={
  {"source.brass_register.effect",0,100},
  {"source.paper_tide.effect",0,100},
  {"source.spectral_garden.effect",0,100},
  {"filter.topographic_etch.effect",1,100},
  {"filter.contact_filaments.effect",2,80},
  {"filter.afterimage_press.effect",3,100}
 };
 bool pass=true;obs_enter_graphics();Textures t=fixture();
 for(const auto& c:cases){
  fs::path path=fs::path(argv[1])/c.file;
  char* err=nullptr;auto* fx=gs_effect_create_from_file(path.c_str(),&err);
  if(err){fprintf(stderr,"COMPILER %s: %s\n",c.file,err);bfree(err);}
  if(!fx){pass=false;continue;}
  // Explicit test controls, not a promise about native UI default values.
  sf(fx,"spacing",.17);sf(fx,"motion_gain",3.);sf(fx,"drift_rate",.55);
  sf(fx,"bell_radius",.042);sf(fx,"engraving",.85);
  sf(fx,"swell",.1);sf(fx,"detail",2.2);sf(fx,"flow",.28);
  sf(fx,"audio_gain",2.5);
  sf(fx,"blossom_size",.026);sf(fx,"bloom_amount",.039);sf(fx,"wind_speed",.95);
  sf(fx,"contour_count",30);sf(fx,"stroke_width",.025);
  sf(fx,"edge_weight",1);sf(fx,"audio_response",.8);
  sf(fx,"effect_mix",.78);sf(fx,"thread_width",2);
  sf(fx,"node_radius",7);sf(fx,"audio_sway",.7);sf(fx,"opacity",.9);
  sf(fx,"offset_px",4);sf(fx,"grain",.25);sf(fx,"history",.87);
  sv(fx,"paper_color",.087,.102,.13);
  sv(fx,"metal_color",.86,.64,.37);
  sv(fx,"bright_color",.92,.89,.76);
  if(c.kind==0 && std::strstr(c.file,"paper_tide")){
    sv(fx,"paper_color",.89,.83,.70);
  }
  sv(fx,"background_color",.075,.106,.112);
  sv(fx,"stem_color",.32,.48,.36);
  sv(fx,"cool_petals",.56,.69,.65);
  sv(fx,"warm_petals",.93,.58,.44);
  sv(fx,"low_ink",.19,.34,.34);
  sv(fx,"high_ink",.63,.40,.37);
  sv(fx,"paper_tint",.89,.85,.76);
  sv(fx,"ink_color",.12,.19,.17);
  sv(fx,"thread_color",.76,.44,.30);
  sv(fx,"node_color",.96,.91,.81);
  sv(fx,"cool_ink",.18,.34,.41);
  sv(fx,"warm_ink",.72,.37,.27);
  Frame off=render(fx,t,c.kind,0),on=render(fx,t,c.kind,1),more=render(fx,t,c.kind,2);
  size_t delta=rgbDiff(off,on),ctl=rgbDiff(on,more),alpha=aDiff(off,on);
  bool ok=delta>=c.minChange && alpha==0 &&
          png(fs::path(argv[2])/fs::path(c.file).replace_extension(".png"),on);
  if(c.kind==0||c.kind==2||c.kind==3)ok=ok&&ctl>25;
  if(std::strstr(c.file,"source.spectral_garden.effect")){
   Frame quiet=render(fx,t,c.kind,0,0);
   for(int band=0;band<8;band++){
    Frame one=render(fx,t,c.kind,1,band);
    size_t separate=rgbDiff(quiet,one);
    printf("SPECTRAL_GARDEN_BAND_%d delta=%zu\n",band,separate);
    ok=ok&&separate>60;
   }
  }
  printf("ORIGINAL_GPU_%s %s active=%zu control=%zu alpha=%zu\n",
         ok?"PASS":"FAIL",c.file,delta,ctl,alpha);
  pass&=ok;gs_effect_destroy(fx);
 }
 if(t.video)gs_texture_destroy(t.video);
 if(t.depth)gs_texture_destroy(t.depth);
 if(t.hands)gs_texture_destroy(t.hands);
 if(t.old)gs_texture_destroy(t.old);
 obs_leave_graphics();obs_shutdown();
 printf("ORIGINAL_INSTRUMENT_REAL_GPU_PASS=%d\n",pass?1:0);
 return pass?0:1;
}
