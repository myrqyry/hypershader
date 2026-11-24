// Real libobs/HyperShaderFX loader check for existing stdlib modules,
 // wrapper expansion and missing-include safety. No fake shader compiler.
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
#include <iomanip>
#include <cstring>
#include <filesystem>
#include <mutex>
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

struct UiTask {obs_task_t task=nullptr;void* param=nullptr;};
static std::mutex ui_tasks_mutex;
static std::vector<UiTask> ui_tasks;
static void test_ui_task_handler(obs_task_t task,void* param,bool wait) {
 if(wait){task(param);return;}
 std::lock_guard<std::mutex> lock(ui_tasks_mutex);
 ui_tasks.push_back({task,param});
}
static void drain_ui_tasks() {
 for(;;){
  std::vector<UiTask> pending;
  {
   std::lock_guard<std::mutex> lock(ui_tasks_mutex);
   if(ui_tasks.empty())break;
   pending.swap(ui_tasks);
  }
  for(auto &item:pending)item.task(item.param);
 }
}

int main(int argc, char **argv) {
 if(argc!=5) return 2;
 const std::string mode=argv[4];
 const bool missing=(mode=="missing");
 const bool apply=(mode=="apply");
 const bool vector=(mode=="vector");
 const bool scalar=(mode=="scalar");
 const bool audit=(mode=="audit");
 if(!missing && !apply && mode!="math" && mode!="wrapper" && mode!="managed" && mode!="vector" && mode!="scalar" && mode!="macro" && mode!="bloom" && mode!="bloom_off" && mode!="audit" && mode!="sampling" && mode!="compat") return 2;
 if(!obs_startup("en-US",nullptr,nullptr))return 1;
 obs_set_ui_task_handler(test_ui_task_handler);
 obs_video_info video{};
 video.graphics_module="libobs-opengl";
 video.fps_num=30;video.fps_den=1;
 video.base_width=W;video.base_height=H;
 video.output_width=W;video.output_height=H;
 video.output_format=VIDEO_FORMAT_RGBA;video.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&video)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 1;}
 obs_register_source(&source_info);
 obs_module_t *module=nullptr;
 if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS ||
    !obs_init_module(module)){obs_shutdown();return 1;}
 std::string shaderFile=argv[3];
 if(mode=="managed") {
   const std::string uuid="11111111-1111-1111-1111-111111111111.effect";
   char* config=obs_module_get_config_path(module,"builder-applied");
   if(!config){std::cerr<<"No module config path\n";obs_shutdown();return 1;}
   const std::filesystem::path folder(config);
   bfree(config);
   std::error_code ec;
   std::filesystem::create_directories(folder,ec);
   if(ec){std::cerr<<"Cannot create isolated managed folder "<<ec.message()<<"\n";obs_shutdown();return 1;}
   std::filesystem::copy_file(argv[3],folder/uuid,
       std::filesystem::copy_options::overwrite_existing,ec);
   if(ec){std::cerr<<"Cannot stage managed shader "<<ec.message()<<"\n";obs_shutdown();return 1;}
   shaderFile="managed/builder-applied/"+uuid;
   std::cout<<"MANAGED_FILE_DIR="<<folder<<"\n";
 }
 auto *source=obs_source_create("hyper_halation_test_source","Stdlib test video",nullptr,nullptr);
 TestSource *primary=active;
 auto *settings=obs_data_create();
 obs_data_set_string(settings,"shader_file",shaderFile.c_str());
 if(audit)obs_data_set_double(settings,"speed",0);
 auto *filter=obs_source_create("hypershader_filter","Stdlib test",settings,nullptr);
 obs_data_release(settings);
 if(!source||!filter){obs_shutdown();return 1;}
 obs_source_filter_add(source,filter);
 obs_source_inc_showing(source);
 // A second independently configured filter must keep the original shader.
 obs_source_t *other=nullptr,*other_filter=nullptr;
 if(apply){
   other=obs_source_create("hyper_halation_test_source","Unaffected video",nullptr,nullptr);
   auto *other_settings=obs_data_create();
   obs_data_set_string(other_settings,"shader_file",argv[3]);
   other_filter=obs_source_create("hypershader_filter","Unaffected shader",other_settings,nullptr);
   obs_data_release(other_settings);
   if(!other||!other_filter){obs_shutdown();return 1;}
   obs_source_filter_add(other,other_filter);
   obs_source_inc_showing(other);
   active=primary;
 }
 obs_source_t *audit_source=nullptr,*audit_filter=nullptr;
 TestSource *audit_data=nullptr;
 if(audit){
   audit_source=obs_source_create("hyper_halation_test_source","Audit twin video",nullptr,nullptr);
   audit_data=active;
   active=primary;
   auto *audit_settings=obs_data_create();
   obs_data_set_string(audit_settings,"shader_file",shaderFile.c_str());
   obs_data_set_double(audit_settings,"speed",0);
   audit_filter=obs_source_create("hypershader_filter","Audit twin",audit_settings,nullptr);
   obs_data_release(audit_settings);
   if(!audit_source||!audit_filter){obs_shutdown();return 1;}
   obs_source_filter_add(audit_source,audit_filter);
   obs_source_inc_showing(audit_source);
 }
 size_t verified=0, unchanged=0, before=0, after=0, untouched=0;
 bool submitted=false;
 bool vectorDefaultNotOverridden=true;
 bool scalarDefaultPersisted=false;
 uint64_t audit_sum=0,audit_diff=0,audit_repeat_diff=0;
 uint8_t audit_min=255,audit_max=0;
 size_t audit_samples=0,audit_modified=0,audit_repeat_samples=0;
 for(int i=0;i<(apply?72:48);i++){
    int x=70+(i%12)*5;
    auto input=make_frame(x);
    if(mode=="sampling") {
       for(unsigned y=0;y<H;y++) {
          // Distinct edges ensure seam interpolation cannot pass by clamping.
          auto edge=4*(y*W+W-1);
          input[edge]=60;input[edge+1]=100;input[edge+2]=180;
       }
    }
    obs_enter_graphics();
    gs_texture_set_image(primary->texture,input.data(),W*4,false);
    if(audit_data)gs_texture_set_image(audit_data->texture,input.data(),W*4,false);
    obs_leave_graphics();
    if(scalar && i==25) {
       auto* stored=obs_source_get_settings(filter);
       scalarDefaultPersisted=obs_data_has_user_value(stored,"param_gain") &&
           std::abs(obs_data_get_double(stored,"param_gain")-0.75)<0.001;
       obs_data_set_double(stored,"speed",1.4);
       obs_source_update(filter,stored);
       obs_data_release(stored);
       std::cout<<"SCALAR_DEFAULT_PERSISTED="<<scalarDefaultPersisted<<"\n";
    }
    if(vector && i==25) {
       auto* stored=obs_source_get_settings(filter);
       vectorDefaultNotOverridden=!obs_data_has_user_value(stored,"param_tint");
       obs_data_set_double(stored,"speed",1.4);
       obs_source_update(filter,stored);
       obs_data_release(stored);
       std::cout<<"VECTOR_DEFAULT_UNSET_BEFORE_UPDATE="<<vectorDefaultNotOverridden<<"\n";
    }
    if(apply && i==25) {
       auto *properties=obs_source_properties(filter);
       auto *preset=properties?obs_properties_get(properties,"preset"):nullptr;
       const size_t count=preset?obs_property_list_item_count(preset):0;
       if(count<2) {std::cerr<<"FAIL: no Custom File option\n";return 1;}
       const long long custom=obs_property_list_item_int(preset,count-1);
       if(properties)obs_properties_destroy(properties);
       auto *settings=obs_source_get_settings(filter);
       const std::string replacement=std::string(argv[2])+
          "/shaders/tests/stdlib_wrapper_probe.effect";
       obs_data_set_int(settings,"preset",custom);
       obs_data_set_string(settings,"shader_file",replacement.c_str());
       obs_source_update(filter,settings);
       obs_data_release(settings);
       auto *stored=obs_source_get_settings(filter);
       submitted=std::string(obs_data_get_string(stored,"shader_file"))==replacement &&
                 obs_data_get_int(stored,"preset")==custom;
       obs_data_release(stored);
       if(!submitted) {std::cerr<<"FAIL: target shader settings did not change\n";return 1;}
    }
    auto output=render(source);
    drain_ui_tasks();
    Frame audit_output;
    if(audit_source){
       audit_output=render(audit_source);
       drain_ui_tasks();
    }
    if(apply && i>=40){
       const auto second=render(other);
       const size_t bright=4*(65*W+17);
       if(second.size()==input.size() &&
          std::abs(int(second[bright])-int(250*.81f))<=4 &&
          std::abs(int(second[0])-int(12*.81f))<=3)++untouched;
    }
    if(audit && output.size()==input.size() && i>=38) {
       audit_samples++;
       uint64_t sum=0,delta=0;
       uint8_t lo=255,hi=0;
       for(size_t j=0;j<output.size();j+=4)for(size_t k=0;k<3;k++){
          uint8_t v=output[j+k];
          sum+=v;
          delta+=std::abs(int(v)-int(input[j+k]));
          lo=std::min(lo,v);hi=std::max(hi,v);
       }
       audit_sum+=sum;audit_diff+=delta;
       audit_min=std::min(audit_min,lo);audit_max=std::max(audit_max,hi);
       if(delta>3*W*H*3)++audit_modified;
       if(audit_output.size()==output.size()){
          uint64_t repeat=0;
          for(size_t j=0;j<output.size();j+=4)for(size_t k=0;k<3;k++)
             repeat+=std::abs(int(output[j+k])-int(audit_output[j+k]));
          audit_repeat_diff+=repeat;
          audit_repeat_samples++;
       }
    }
    bool exact=output.size()==input.size();
    if(exact)for(size_t j=0;j<input.size();++j){
      if(j%4!=3 && std::abs(int(output[j])-int(input[j]))>2){exact=false;break;}
    }
    if(i>20) {
       if(exact)++unchanged;
       if(!missing && output.size()==input.size()) {
          if(mode=="sampling") {
             // Clamp samples the distinct right edge; repeat/fetch sample this pixel.
             bool ok=true;
             for(size_t j=0;j<input.size();j+=4) {
                if(std::abs(int(output[j])-60)>3 ||
                   std::abs(int(output[j+1])-int(input[j+1]))>3 ||
                   std::abs(int(output[j+2])-int(input[j+2]))>3 ||
                   output[j+3]!=255) {
                   if(i==30)std::cout<<"SAMPLING_MISMATCH x="<<(j/4%W)<<" y="<<(j/4/W)<<" out="<<int(output[j])<<","<<int(output[j+1])<<","<<int(output[j+2])<<","<<int(output[j+3])<<" expected=60,"<<int(input[j+1])<<","<<int(input[j+2])<<",255\n";
                   ok=false;break;}
             }
             if(ok)++verified;
          } else if(scalar) {
             const bool gain=std::abs(int(output[0])-191)<=4 &&
                             std::abs(int(output[1])-191)<=4 &&
                             std::abs(int(output[2])-191)<=4 && output[3]==255;
             if(gain && i>=30)++verified;
          } else if(vector) {
             const bool palette = std::abs(int(output[0])-204)<=7 &&
                                  std::abs(int(output[1])-51)<=7 &&
                                  std::abs(int(output[2])-102)<=7 &&
                                  output[3]==255;
             if(palette && i>=30) ++verified;
          } else if(mode=="bloom") {
             const size_t bright=4*(65*W+x+7);
             const size_t halo=4*(65*W+x-3);
             const size_t dark=0;
             if(i==22 || i==40)
                std::cout<<"BLOOM_FRAME frame="<<i<<" bright="<<int(output[bright])
                         <<" halo="<<int(output[halo])<<" baseline_halo="<<int(input[halo])
                         <<" dark="<<int(output[dark])<<" alpha="<<int(output[halo+3])<<"\n";
             bool ok=int(output[bright])>=int(input[bright])-3 &&
                     int(output[halo])>int(input[halo])+3 &&
                     std::abs(int(output[dark])-int(input[dark]))<=2 &&
                     output[bright+3]==255 && output[halo+3]==255;
             if(ok)++verified;
          } else {
          const float f=mode=="math"?.81f:(apply && i<25?.81f:.75f);
          const size_t bright=4*(65*W+x+7);
          const size_t dark=0;
          if(apply && (i==22||i==40||i==50||i==65))
             std::cout<<"APPLY_FRAME index="<<i<<" bright="<<int(output[bright])
                      <<" dark="<<int(output[dark])<<" expected_factor="<<f<<"\n";
          const bool ok=std::abs(int(output[bright])-int(250*f))<=4 &&
                     std::abs(int(output[bright+1])-int(244*f))<=4 &&
                     std::abs(int(output[dark])-int(12*f))<=3 &&
                     output[bright+3]==255 && output[dark+3]==255;
          if(ok) {
             ++verified;
             if(apply && i<25)++before;
             if(apply && i>=40)++after;
          }
          }
       }
    }
    std::this_thread::sleep_for(60ms);
 }
 if(audit) {
   std::cout<<"SHADER_AUDIT samples="<<audit_samples
            <<" mean_rgb="<<(audit_samples?double(audit_sum)/audit_samples/(W*H*3):0)
            <<" delta_rgb="<<(audit_samples?double(audit_diff)/audit_samples/(W*H*3):0)
            <<" min_rgb="<<int(audit_min)<<" max_rgb="<<int(audit_max)
            <<" modified_frames="<<audit_modified
            <<" repeat_delta_rgb="<<std::fixed<<std::setprecision(6)
            <<(audit_repeat_samples?double(audit_repeat_diff)/audit_repeat_samples/(W*H*3):0)
            <<std::defaultfloat<<"\n";
 }
 bool pass=audit ? audit_samples>=8 && audit_repeat_samples==audit_samples :
     scalar ? scalarDefaultPersisted && verified>=15 :
     vector ? vectorDefaultNotOverridden && verified>=15 :
     apply ? submitted&&before>=3&&after>=20&&untouched>=20 :
     ((missing || mode=="bloom_off") ? unchanged>=22 : verified>=22);
 std::cout<<"STDLIB_LOADER mode="<<mode
          <<" verified_frames="<<verified
          <<" unchanged_frames="<<unchanged
          <<" apply_submitted="<<submitted<<" initial_valid="<<before
          <<" changed_valid="<<after<<" other_unchanged="<<untouched
          <<" PASS="<<pass<<"\n";
 if(other){
   obs_source_dec_showing(other);
   obs_source_filter_remove(other,other_filter);
   obs_source_release(other_filter);obs_source_release(other);
 }
 if(audit_source){
   obs_source_dec_showing(audit_source);
   obs_source_filter_remove(audit_source,audit_filter);
   obs_source_release(audit_filter);obs_source_release(audit_source);
   drain_ui_tasks();
 }
 obs_source_dec_showing(source);
 obs_source_filter_remove(source,filter);
 obs_source_release(filter);obs_source_release(source);
 drain_ui_tasks();
 obs_shutdown();
 return pass?0:1;
}
