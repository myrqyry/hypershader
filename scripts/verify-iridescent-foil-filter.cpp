// Real hypershader async filter test: native shader loading, property
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
 .id="hyper_foil_source_fixture",
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
 if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS || !obs_init_module(module)){
  obs_shutdown();return 1;
 }
 auto* source=obs_source_create("hyper_foil_source_fixture","Iridescent foil fixture",nullptr,nullptr);
 auto* settings=obs_data_create();
 obs_data_set_string(settings,"shader_file",argv[3]);
 obs_data_set_double(settings,"param_foil_strength",0.0);
 obs_data_set_double(settings,"param_foil_light_x",0.3);
 obs_data_set_double(settings,"param_foil_light_y",0.75);
 obs_data_set_double(settings,"param_foil_speed",0.0);
 obs_data_set_double(settings,"param_foil_scale",1.25);
 obs_data_set_double(settings,"param_foil_relief",1.3);
 obs_data_set_double(settings,"param_foil_thickness",2.0);
 obs_data_set_double(settings,"param_foil_saturation",0.7);
 obs_data_set_double(settings,"param_foil_specular",0.65);
 auto* filter=obs_source_create("hypershader_filter","Iridescent Foil",settings,nullptr);
 obs_data_release(settings);
 if(!source||!filter){obs_shutdown();return 1;}
 obs_source_filter_add(source,filter);
 obs_source_inc_showing(source);

 auto input=make_frame(25);
 obs_enter_graphics();
 gs_texture_set_image(active->texture,input.data(),W*4,false);
 obs_leave_graphics();

 auto setVal=[&](const char* name,double value,bool integer=false){
  auto* p=obs_source_get_settings(filter);
  if(integer)obs_data_set_int(p,name,(long long)value);
  else obs_data_set_double(p,name,value);
  obs_source_update(filter,p);
  obs_data_release(p);
 };
 auto settle=[&](int iterations=65){
  Frame frame;
  for(int i=0;i<iterations;i++){frame=render(source);std::this_thread::sleep_for(65ms);}
  return frame;
 };
 auto off=settle(18);
 size_t offDiff=diff(off,input);
 auto* props=obs_source_properties(filter);
 bool controls=props &&
   obs_properties_get(props,"param_foil_strength") &&
   obs_properties_get(props,"param_foil_scale") &&
   obs_properties_get(props,"param_foil_light_x") &&
   obs_properties_get(props,"param_foil_light_y") &&
   obs_properties_get(props,"param_foil_thickness");
 if(props)obs_properties_destroy(props);

 setVal("param_foil_strength",1.0);
 auto on=settle(18);
 size_t onDiff=diff(on,input);
 bool alphaSafe=on.size()==input.size();
 if(alphaSafe)for(size_t i=3;i<on.size();i+=4)
   if(on[i]!=input[i]){alphaSafe=false;break;}

 setVal("param_foil_light_x",0.98);
 auto shifted=settle(12);
 size_t lightDiff=diff(on,shifted);
 auto metrics=[](const Frame& frame) {
   unsigned long long sum=0, sumsq=0;
   unsigned char lo=255, hi=0;
   size_t nonblack=0;
   for(size_t i=0;i+3<frame.size();i+=4) {
      unsigned v=unsigned(frame[i])+unsigned(frame[i+1])+unsigned(frame[i+2]);
      sum+=v; sumsq+=(unsigned long long)v*v;
      if(v>12) ++nonblack;
      lo=std::min(lo,static_cast<unsigned char>(v/3));
      hi=std::max(hi,static_cast<unsigned char>(v/3));
   }
   std::cout<<"COLOR_METRICS rgb_sum="<<sum<<" rgb_square_sum="<<sumsq
            <<" min_avg="<<unsigned(lo)<<" max_avg="<<unsigned(hi)
            <<" nonblack_pixels="<<nonblack<<"\n";
   return size_t(hi)-size_t(lo);
 };
 size_t luminanceRange=metrics(on);


 setVal("param_foil_strength",0.0);
 auto again=settle(18);
 size_t restoredDiff=diff(again,input);
 bool pass=controls && offDiff==0 && onDiff>1500 && lightDiff>200 &&
           alphaSafe && restoredDiff==0 && luminanceRange>12;
 std::cout<<"IRIDESCENT_FOIL_FILTER controls="<<controls
          <<" off_diff="<<offDiff<<" active_diff="<<onDiff
          <<" light_diff="<<lightDiff<<" luminance_range="<<luminanceRange<<" restored_diff="<<restoredDiff
          <<" alpha_preserved="<<alphaSafe<<" PASS="<<pass<<"\n";
 obs_source_dec_showing(source);
 obs_source_filter_remove(source,filter);
 obs_source_release(filter);obs_source_release(source);
 obs_shutdown();
 return pass?0:1;
}
