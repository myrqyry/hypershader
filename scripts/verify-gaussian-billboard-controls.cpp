// End-to-end HyperShader/libobs graphics regression: real module,
// async shader loader, moving pixel fixture, feedback and clean I-frame.
// No mock plugin or fake P-frame results.
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
Frame make_frame(int x){
 Frame pixels(W*H*4);
 for(unsigned y=0;y<H;y++)for(unsigned z=0;z<W;z++){
  size_t i=4*(y*W+z);
  bool moving=z>=unsigned(x)&&z<unsigned(x+48)&&y>=33&&y<111;
  pixels[i]=moving?230:uint8_t((z*3+y)%125);
  pixels[i+1]=moving?45:uint8_t((z+y)%93);
  pixels[i+2]=moving?12:uint8_t((y*3+z)%110);
  pixels[i+3]=255;
 }
 return pixels;
}
const char* source_name(void*){return "Temporary moving video fixture";}
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
 .id="hyper_pframe_test_source",
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
  for(int k=0;k<3;k++)total+=(abs(int(a[i+k])-int(b[i+k]))>7);
 return total;
}



int main(int argc,char** argv) {
 if(argc!=4)return 2;
 if(!obs_startup("en-US",nullptr,nullptr))return 1;
 obs_video_info info{};
 info.graphics_module="libobs-opengl";info.fps_num=30;info.fps_den=1;
 info.base_width=W;info.base_height=H;info.output_width=W;info.output_height=H;
 info.output_format=VIDEO_FORMAT_RGBA;info.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&info)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 1;}
 obs_register_source(&source_info);
 obs_module_t* module=nullptr;
 if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS ||
    !obs_init_module(module)){obs_shutdown();return 1;}
 obs_source_t* source=obs_source_create("hyper_pframe_test_source","3D scene fixture",nullptr,nullptr);
 if(!source){obs_shutdown();return 1;}
 obs_source_inc_showing(source);
 Frame baseline=make_frame(67);
 obs_enter_graphics();
 gs_texture_set_image(active->texture,baseline.data(),W*4,false);
 obs_leave_graphics();
 const int counts[]={12,48,48,48};
 const float distances[]={0.0f,0.0f,0.4f,0.0f};
 const float lights[]={0.8f,0.8f,0.8f,1.75f};
 Frame outputs[4],bypass;
 bool controls=true;
 for(int k=0;k<4;k++){
  obs_data_t* settings=obs_data_create();
  obs_data_set_string(settings,"shader_file",argv[3]);
  obs_data_set_int(settings,"param_splat_density",counts[k]);
  obs_data_set_double(settings,"param_camera_yaw",distances[k]);
  obs_data_set_double(settings,"param_splat_radius",lights[k]);
  obs_data_set_double(settings,"param_orbit_speed",0.0);
  obs_source_t* filter=obs_source_create("hypershader_filter","3D gallery",settings,nullptr);
  obs_data_release(settings);
  if(!filter){obs_source_dec_showing(source);obs_source_release(source);obs_shutdown();return 1;}
  obs_source_filter_add(source,filter);
  for(int step=0;step<45;step++){
   Frame output=render(source);
   if(step==27){
    obs_properties_t* properties=obs_source_properties(filter);
    controls &= properties &&
      obs_properties_get(properties,"param_splat_density") &&
      obs_properties_get(properties,"param_camera_yaw") &&
      obs_properties_get(properties,"param_splat_radius") &&
      obs_properties_get(properties,"param_effect_mix");
    if(properties)obs_properties_destroy(properties);
   }
   if(step==44)outputs[k]=std::move(output);
   std::this_thread::sleep_for(20ms);
  }
  if(k==3){
   obs_data_t* updated=obs_source_get_settings(filter);
   obs_data_set_double(updated,"param_effect_mix",0.0);
   obs_source_update(filter,updated);
   obs_data_release(updated);
   for(int step=0;step<35;step++){bypass=render(source);std::this_thread::sleep_for(40ms);}
  }
  obs_source_filter_remove(source,filter);
  obs_source_release(filter);
 }
 const size_t countChange=diff(outputs[0],outputs[1]);
 const size_t cameraChange=diff(outputs[1],outputs[2]);
 const size_t lightChange=diff(outputs[1],outputs[3]);
 const size_t bypassChange=diff(bypass,baseline);
 const size_t rendered=diff(outputs[1],baseline);
 bool valid=controls && countChange>500 && cameraChange>500 &&
            lightChange>1000 && rendered>2000 && bypassChange<50;
 std::cout<<"GAUSSIAN_BILLBOARD_CONTROLS count_delta="<<countChange
          <<" camera_delta="<<cameraChange<<" light_delta="<<lightChange
          <<" rendered_delta="<<rendered<<" bypass_delta="<<bypassChange
          <<" controls="<<controls<<" PASS="<<valid<<"\n";
 obs_source_dec_showing(source);obs_source_release(source);obs_shutdown();
 return valid?0:1;
}
