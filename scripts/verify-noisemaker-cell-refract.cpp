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
constexpr unsigned W=160,H=90;
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
Frame make_color_frame() {
 Frame pixels(W*H*4,0);
 for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++){
  const size_t j=4*(y*W+x);
  const int cell=int(x/16)+int(y/12);
  pixels[j]=uint8_t(cell%3==0?220:25+(x*150/W));
  pixels[j+1]=uint8_t(cell%3==1?235:15+(y*140/H));
  pixels[j+2]=uint8_t(cell%3==2?225:45+(x*80/W));
  pixels[j+3]=uint8_t(255);
 }
 return pixels;
}
const char* source_name(void*){return "Bright moving video fixture";}
uint32_t source_width(void*){return W;}
uint32_t source_height(void*){return H;}
void* source_create(obs_data_t*,obs_source_t*){
 auto* s=new TestSource;
 auto frame=make_color_frame();
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

int main(int argc,char **argv) {
 if(argc!=4)return 2;
 if(!obs_startup("en-US",nullptr,nullptr))return 2;
 obs_video_info info{};
 info.graphics_module="libobs-opengl";info.fps_num=30;info.fps_den=1;
 info.base_width=W;info.base_height=H;info.output_width=W;info.output_height=H;
 info.output_format=VIDEO_FORMAT_RGBA;info.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&info)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 2;}
 obs_register_source(&source_info);
 obs_module_t* module=nullptr;
 if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS || !obs_init_module(module)){
  obs_shutdown();return 3;
 }
 auto* source=obs_source_create("hyper_halation_test_source","Cell Refract RGB fixture",nullptr,nullptr);
 auto* settings=obs_data_create();
 obs_data_set_string(settings,"shader_file",argv[3]);
 obs_data_set_double(settings,"param_refractAmt",35.0);
 obs_data_set_double(settings,"param_scale",50.0);
 obs_data_set_double(settings,"param_cellScale",75.0);
 obs_data_set_double(settings,"param_cellSmooth",0.0);
 obs_data_set_double(settings,"param_variation",0.0);
 obs_data_set_double(settings,"param_speed",0.0);
 obs_data_set_double(settings,"param_direction",0.0);
 obs_data_set_int(settings,"param_seed",1);
 obs_data_set_int(settings,"param_edge_wrap",0);
 obs_data_set_int(settings,"param_cell_shape",1);
 obs_data_set_int(settings,"param_post_mode",0);
 obs_data_set_double(settings,"param_effectWidth",0.0);
 auto* filter=obs_source_create("hypershader_filter","NoiseMaker Cell Refract",settings,nullptr);
 obs_data_release(settings);
 if(!source||!filter){obs_shutdown();return 4;}
 obs_source_filter_add(source,filter);obs_source_inc_showing(source);
 const auto input=make_color_frame();
 obs_enter_graphics();gs_texture_set_image(active->texture,input.data(),W*4,false);obs_leave_graphics();
 auto setVal=[&](const char* name,double value,bool integer=false){
  auto* p=obs_source_get_settings(filter);
  if(integer)obs_data_set_int(p,name,(long long)value);
  else obs_data_set_double(p,name,value);
  obs_source_update(filter,p);obs_data_release(p);
 };
 auto settle=[&](int frames=5,int sleepMs=45){
  Frame output;
  for(int i=0;i<frames;i++){output=render(source);std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));}
  return output;
 };
 Frame initial=settle(45,65);
 auto* props=obs_source_properties(filter);
 const char* keys[]={"refractAmt","direction","edge_wrap","speed","cell_shape",
   "scale","cellScale","cellSmooth","variation","seed","post_mode","effectWidth"};
 bool controls=props!=nullptr;
 for(const char* k:keys){
  const std::string name=std::string("param_")+k;
  if(!props||!obs_properties_get(props,name.c_str()))controls=false;
 }
 if(props)obs_properties_destroy(props);
 const size_t activeDiff=diff(initial,input);
 bool alphaSafe=initial.size()==input.size();
 if(alphaSafe)for(size_t i=3;i<initial.size();i+=4){
  if(initial[i]!=input[i]){alphaSafe=false;break;}
 }

 auto measure=[&](const char* k,double value,double baseline,bool integer=false){
  setVal(k,value,integer);
  const auto changed=settle();
  const size_t result=diff(changed,initial);
  setVal(k,baseline,integer);
  settle(3);
  return result;
 };
 const size_t amount=measure("param_refractAmt",0.,35.);
 const size_t shape=measure("param_cell_shape",0.,1.,true);
 const size_t seed=measure("param_seed",59.,1.,true);
 const size_t scale=measure("param_scale",83.,50.);
 const size_t size=measure("param_cellScale",25.,75.);
 const size_t smooth=measure("param_cellSmooth",95.,0.);
 const size_t variation=measure("param_variation",95.,0.);
 const size_t direction=measure("param_direction",170.,0.);
 const size_t wrap=measure("param_edge_wrap",1.,0.,true);
 setVal("param_effectWidth",5.);
 size_t postMin=SIZE_MAX;
 for(int mode=1;mode<=6;++mode){
  setVal("param_post_mode",mode,true);
  const auto changed=settle();
  postMin=std::min(postMin,diff(changed,initial));
 }
 setVal("param_post_mode",0.,true);setVal("param_effectWidth",0.);
 const size_t speed=measure("param_speed",3.,0.);
 setVal("param_refractAmt",0.);
 const size_t passDiff=diff(settle(),input);

 const bool pass=controls&&alphaSafe&&activeDiff>250&&
  amount>250&&shape>100&&seed>100&&scale>100&&size>100&&
  smooth>40&&variation>40&&direction>100&&wrap>40&&postMin>100&&
  speed>40&&passDiff==0;
 std::cout<<"NOISEMAKER_CELL_REFRACT controls="<<controls
  <<" active_diff="<<activeDiff<<" amount="<<amount<<" shape="<<shape
  <<" seed="<<seed<<" scale="<<scale<<" cellSize="<<size
  <<" smooth="<<smooth<<" variation="<<variation<<" direction="<<direction
  <<" wrap="<<wrap<<" post_min="<<postMin<<" speed="<<speed
  <<" zero_amount_diff="<<passDiff<<" alpha_preserved="<<alphaSafe
  <<" PASS="<<pass<<"\n";
 obs_source_dec_showing(source);obs_source_filter_remove(source,filter);
 obs_source_release(filter);obs_source_release(source);obs_shutdown();
 return pass?0:1;
}
