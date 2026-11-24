// Real libobs RGBA readback for two independent Gnomalab signal ports.
// Verifies per-effect typed UI, animated pixels, meaningful visible output,
// control response, alpha retention and exact zero-mix bypass.
#include <obs.h>
#include <graphics/graphics.h>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <cstring>
#include <iostream>
#include <cmath>
using namespace std::chrono_literals;
constexpr int W=240,H=136;
using Frame=std::vector<uint8_t>;
struct Source { gs_texture_t* tex=nullptr; };
Source* active=nullptr;
Frame make_input(){
  Frame out(W*H*4);
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){
    const size_t n=4*(y*W+x);
    // Broad low-contrast quantized bands, with a sharp high-contrast object.
    int value=x>=160?225:((x/12)*9+30);
    out[n]=uint8_t(value);
    out[n+1]=uint8_t(value);
    out[n+2]=uint8_t(value);
    out[n+3]=255;
  }
  return out;
}
const char* source_name(void*){return "Gnomalab GPU fixture";}
uint32_t source_width(void*){return W;}
uint32_t source_height(void*){return H;}
void* source_create(obs_data_t*,obs_source_t*){
  auto* s=new Source;auto f=make_input();const uint8_t* p=f.data();
  obs_enter_graphics();s->tex=gs_texture_create(W,H,GS_RGBA,1,&p,GS_DYNAMIC);obs_leave_graphics();
  active=s;return s;
}
void source_destroy(void* data){
  auto* s=(Source*)data;obs_enter_graphics();gs_texture_destroy(s->tex);
  obs_leave_graphics();active=nullptr;delete s;
}
void source_render(void* data,gs_effect_t*){
  auto* s=(Source*)data;if(s->tex)obs_source_draw(s->tex,0,0,W,H,false);
}
obs_source_info info{
 .id="hyper_gnomalab_signal_fixture",.type=OBS_SOURCE_TYPE_INPUT,
 .output_flags=OBS_SOURCE_VIDEO,.get_name=source_name,.create=source_create,
 .destroy=source_destroy,.get_width=source_width,.get_height=source_height,
 .video_render=source_render
};
Frame render(obs_source_t* src){
 Frame frame;obs_enter_graphics();
 auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
 auto* stage=gs_stagesurface_create(W,H,GS_RGBA);
 if(target && stage && gs_texrender_begin(target,W,H)){
  gs_projection_push();gs_matrix_push();gs_matrix_identity();
  gs_ortho(0,float(W),0,float(H),-100,100);
  obs_source_video_render(src);
  gs_matrix_pop();gs_projection_pop();gs_texrender_end(target);
  if(auto* texture=gs_texrender_get_texture(target)){
   gs_stage_texture(stage,texture);uint8_t* data=nullptr;uint32_t pitch=0;
   if(gs_stagesurface_map(stage,&data,&pitch)){
    frame.resize(W*H*4);
    for(int y=0;y<H;y++)memcpy(frame.data()+y*W*4,data+y*pitch,W*4);
    gs_stagesurface_unmap(stage);
   }
  }
 }
 if(stage)gs_stagesurface_destroy(stage);
 if(target)gs_texrender_destroy(target);
 obs_leave_graphics();return frame;
}
int at(const Frame&f,int x,int y,int c=0){return f[4*(y*W+x)+c];}

struct Spec {
  const char* name; const char* label; const char* group;
  obs_property_type type; double low,high,step,def;
  const char* items;
};
static bool ui_check(obs_source_t* filter,bool topo) {
  const Spec topo_specs[]={
   {"levels","Number of Levels","Contours",OBS_PROPERTY_FLOAT,2,100,1,20,nullptr},
   {"lineThickness","Line Thickness","Contours",OBS_PROPERTY_FLOAT,.001,.5,.001,.08,nullptr},
   {"softness","Edge Softness","Contours",OBS_PROPERTY_FLOAT,0,1,.01,.2,nullptr},
   {"speed","Erosion Speed","Animation",OBS_PROPERTY_FLOAT,-2,2,.05,.5,nullptr},
   {"offset","Height Offset","Animation",OBS_PROPERTY_FLOAT,0,1,.01,0,nullptr},
   {"preProcess","Blur / Sharpen","Preprocess",OBS_PROPERTY_FLOAT,-1,1,.05,0,nullptr},
   {"lineColor","Line Color","Output",OBS_PROPERTY_COLOR,0,0,0,0,nullptr},
   {"showSource","Mix With Original (HUD)","Output",OBS_PROPERTY_FLOAT,0,1,.01,0,nullptr},
   {"invert","Invert Map","Contours",OBS_PROPERTY_BOOL,0,1,1,0,nullptr},
   {"effect_mix","Effect Mix","Output",OBS_PROPERTY_FLOAT,0,1,.01,1,nullptr}
  };
  const Spec broke_specs[]={
   {"threshold","Luma Threshold","Mask",OBS_PROPERTY_FLOAT,0,1,.01,.5,nullptr},
   {"invertLuma","Invert Threshold","Mask",OBS_PROPERTY_BOOL,0,1,1,0,nullptr},
   {"displacement","Distortion H","Distortion",OBS_PROPERTY_FLOAT,0,.3,.005,.05,nullptr},
   {"blendMode","Blend Mode","Pattern",OBS_PROPERTY_LIST,0,1,1,0,"Replace,Multiply"},
   {"patternStyle","Pattern Style","Pattern",OBS_PROPERTY_LIST,0,2,1,0,"Fine Grain,Ultra Thin V,Ultra Thin H"},
   {"patternDensity","Density / Size","Pattern",OBS_PROPERTY_FLOAT,10,200,1,100,nullptr},
   {"effect_mix","Effect Mix","Output",OBS_PROPERTY_FLOAT,0,1,.01,1,nullptr}
  };
  const Spec* expected=topo?topo_specs:broke_specs;
  const size_t count=topo?std::size(topo_specs):std::size(broke_specs);
  auto* props=obs_source_properties(filter);
  auto* outer=props?obs_properties_get(props,"shader_params_group"):nullptr;
  auto* groups=outer?obs_property_group_content(outer):nullptr;
  auto* settings=obs_source_get_settings(filter);
  bool ok=bool(groups&&settings);
  size_t actualCount=0;
  if(groups)for(auto* group=obs_properties_first(groups);group;obs_property_next(&group)){
   auto* nested=obs_property_get_type(group)==OBS_PROPERTY_GROUP?
       obs_property_group_content(group):nullptr;
   if(nested)for(auto* p=obs_properties_first(nested);p;obs_property_next(&p))
     actualCount+=strncmp(obs_property_name(p),"param_",6)==0;
  }
  ok &= actualCount==count;
  for(size_t i=0;i<count;i++) {
   const auto& s=expected[i];
   std::string groupName=std::string("group_")+s.group;
   auto* g=groups?obs_properties_get(groups,groupName.c_str()):nullptr;
   auto* nested=g?obs_property_group_content(g):nullptr;
   std::string propertyName=std::string("param_")+s.name;
   auto* p=nested?obs_properties_get(nested,propertyName.c_str()):nullptr;
   if(!p){std::cerr<<"GNOMALAB_UI_MISSING "<<propertyName<<"\n";ok=false;continue;}
   bool correct=obs_property_get_type(p)==s.type && obs_property_visible(p) &&
                 strcmp(obs_property_description(p),s.label)==0;
   if(s.type==OBS_PROPERTY_FLOAT){
     correct &= std::abs(obs_property_float_min(p)-s.low)<1e-4 &&
       std::abs(obs_property_float_max(p)-s.high)<1e-4 &&
       std::abs(obs_property_float_step(p)-s.step)<1e-4;
   }
   if(s.type==OBS_PROPERTY_LIST){
      std::string options=s.items;
      size_t countItems=1;
      for(char ch:options)countItems+=(ch==',');
      correct &= obs_property_list_item_count(p)==countItems;
      for(size_t j=0;j<countItems;j++) correct &= obs_property_list_item_int(p,j)==j;
   }
   if(s.type!=OBS_PROPERTY_COLOR){
     const double actual = s.type==OBS_PROPERTY_BOOL?obs_data_get_bool(settings,propertyName.c_str()):
       s.type==OBS_PROPERTY_LIST?obs_data_get_int(settings,propertyName.c_str()):
       obs_data_get_double(settings,propertyName.c_str());
     correct &= std::abs(actual-s.def)<1e-4;
   }
   if(!correct)std::cerr<<"GNOMALAB_UI_WRONG "<<propertyName<<"\n";
   ok &= correct;
  }
  if(settings)obs_data_release(settings);
  if(props)obs_properties_destroy(props);
  std::cout<<"GNOMALAB_PAIR_UI_PASS="<<ok<<" count="<<actualCount
           <<" expected="<<count<<"\n";
  return ok;
}
static size_t changed(const Frame& a,const Frame& b){
 if(a.size()!=b.size())return 0;
 size_t count=0;
 for(size_t i=0;i+3<a.size();i+=4)
  for(int c=0;c<3;c++)count+=std::abs(int(a[i+c])-int(b[i+c]))>7;
 return count;
}
static Frame frames(obs_source_t* source,int count){
 Frame out;
 for(int i=0;i<count;i++){out=render(source);std::this_thread::sleep_for(36ms);}
 return out;
}
int main(int argc,char**argv){
 if(argc!=5)return 2;
 const std::string mode=argv[4];
 const bool topo=mode=="topo_scan";
 if(!topo && mode!="broke_in_bad")return 2;
 if(!obs_startup("en-US",nullptr,nullptr))return 2;
 obs_video_info vi{};vi.graphics_module="libobs-opengl";vi.fps_num=30;vi.fps_den=1;
 vi.base_width=W;vi.base_height=H;vi.output_width=W;vi.output_height=H;
 vi.output_format=VIDEO_FORMAT_RGBA;vi.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&vi)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 2;}
 obs_register_source(&info);
 obs_module_t* module=nullptr;
 if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module)){
  obs_shutdown();return 3;
 }
 auto* source=obs_source_create(info.id,"Gnomalab source",nullptr,nullptr);
 auto* data=obs_data_create();
 obs_data_set_string(data,"shader_file",argv[3]);
 auto* filter=obs_source_create("hypershader_filter","Gnomalab FX",data,nullptr);
 obs_data_release(data);
 if(!source||!filter){obs_shutdown();return 4;}
 obs_source_filter_add(source,filter);obs_source_inc_showing(source);
 const Frame input=make_input(),original=frames(source,42);
 Frame animated=frames(source,10);
 const bool ui=ui_check(filter,topo);
 auto* settings=obs_source_get_settings(filter);
 if(topo) {
   obs_data_set_double(settings,"param_levels",5);
   obs_data_set_bool(settings,"param_invert",true);
   obs_data_set_double(settings,"param_showSource",0.75);
 } else {
   obs_data_set_int(settings,"param_patternStyle",2);
   obs_data_set_int(settings,"param_blendMode",1);
   obs_data_set_double(settings,"param_displacement",.24);
 }
 obs_source_update(filter,settings);obs_data_release(settings);
 Frame altered=frames(source,12);
 settings=obs_source_get_settings(filter);
 obs_data_set_double(settings,"param_effect_mix",0);
 obs_source_update(filter,settings);obs_data_release(settings);
 Frame bypass=frames(source,12);
 const size_t animationDelta=changed(original,animated);
 const size_t effectDelta=changed(original,input),
   controlsDelta=changed(original,altered),bypassDelta=changed(bypass,input);
 bool alpha=true;
 for(size_t i=3;i<input.size();i+=4)
  alpha &= original[i]==input[i] && altered[i]==input[i] && bypass[i]==input[i];
 size_t bright=0, dark=0, changedBright=0, changedDark=0;
 for(size_t i=0;i+3<original.size();i+=4){
   int luma=(original[i]+original[i+1]+original[i+2])/3;
   int second=(altered[i]+altered[i+1]+altered[i+2])/3;
   bright+=luma>15;dark+=luma<12;
   changedBright+=second>15;changedDark+=second<12;
 }
 std::cout<<"GNOMALAB_FRAME_COVERAGE original_bright="<<bright
    <<" original_dark="<<dark<<" altered_bright="<<changedBright
    <<" altered_dark="<<changedDark<<"\n";
 const bool success=ui&&effectDelta>200&&controlsDelta>200&&
    (topo?(bright>300&&dark>300&&changedBright>300):(bright>1000&&changedBright>1000))&&
    bypassDelta==0&&alpha;
 std::cout<<"GNOMALAB_PAIR_GPU_PASS="<<success<<" effect_delta="<<effectDelta
    <<" controls_delta="<<controlsDelta<<" animation_delta="<<animationDelta
    <<" bypass_delta="<<bypassDelta
    <<" alpha="<<alpha<<"\n";
 obs_source_dec_showing(source);
 obs_source_filter_remove(source,filter);
 obs_source_release(filter);obs_source_release(source);obs_shutdown();
 return success?0:1;
}
