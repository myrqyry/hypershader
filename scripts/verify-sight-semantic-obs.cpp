#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdlib>
using namespace std::chrono_literals;
using Frame=std::vector<uint8_t>;
static bool draw(obs_source_t* source,Frame& frame,unsigned w,unsigned h){
    obs_enter_graphics();
    auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
    auto* stage=gs_stagesurface_create(w,h,GS_RGBA);
    bool ok=false;
    if(target&&stage&&gs_texrender_begin(target,w,h)){
        gs_projection_push();gs_matrix_push();gs_matrix_identity();gs_ortho(0,float(w),0,float(h),-100,100);
        obs_source_video_render(source);
        gs_matrix_pop();gs_projection_pop();gs_texrender_end(target);
        gs_stage_texture(stage,gs_texrender_get_texture(target));
        uint8_t* bytes=nullptr;uint32_t pitch=0;
        if(gs_stagesurface_map(stage,&bytes,&pitch)){
            if(pitch>=w*4){frame.resize(w*h*4);for(unsigned y=0;y<h;y++)memcpy(frame.data()+y*w*4,bytes+y*pitch,w*4);ok=true;}
            gs_stagesurface_unmap(stage);
        }
    }
    if(stage)gs_stagesurface_destroy(stage);
    if(target)gs_texrender_destroy(target);
    obs_leave_graphics();return ok;
}
static size_t difference(const Frame& a,const Frame& b){
    if(a.empty()||a.size()!=b.size())return 0;
    size_t n=0;for(size_t i=0;i<a.size();i++)n+=std::abs(int(a[i])-int(b[i]))>3;
    return n;
}
static bool load(const char* lib,const char* data){
    obs_module_t* module=nullptr;
    return obs_open_module(&module,lib,data)==MODULE_SUCCESS&&obs_init_module(module);
}

int main(int argc,char**argv){
 if(argc!=10||!obs_startup("en-US",nullptr,nullptr))return 2;
 obs_video_info v{};v.graphics_module="libobs-opengl";v.fps_num=30;v.fps_den=1;
 v.base_width=v.output_width=512;v.base_height=v.output_height=512;
 v.output_format=VIDEO_FORMAT_RGBA;v.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&v)!=OBS_VIDEO_SUCCESS)return 3;
 if(!load(argv[3],argv[4])||!load(argv[5],argv[6]))return 4;
 auto* d=obs_data_create();obs_data_set_string(d,"file",argv[8]);
 auto* source=obs_source_create("image_source","Semantic fixture",d,nullptr);obs_data_release(d);
 if(!source)return 5;
 const auto w=obs_source_get_base_width(source),h=obs_source_get_base_height(source);
 Frame baseline,current;
 if(!w||!h||!draw(source,baseline,w,h))return 6;
 d=obs_data_create();obs_data_set_string(d,"shader_file",argv[9]);
 obs_data_set_bool(d,"sight_use_tracking",true);
 const bool creative=std::strstr(argv[9],"semantic_prism")!=nullptr;
 if(creative){
   obs_data_set_int(d,"param_region_mode",2);
   obs_data_set_int(d,"param_category_id",1);
   obs_data_set_double(d,"param_amount",1.0);
   obs_data_set_int(d,"param_ink_color",0xffff00ffu);
 }
 auto* filter=obs_source_create("hypershader_filter","Semantic probe",d,nullptr);obs_data_release(d);
 if(!filter)return 7;
 obs_source_filter_add(source,filter);obs_source_inc_showing(source);obs_set_output_source(0,source);
 auto wait=[&](bool active){
   for(int i=0;i<90;++i){std::this_thread::sleep_for(50ms);if(!draw(source,current,w,h))return false;
     size_t magenta=0,unchanged=0;
     for(size_t j=0;j+3<current.size();j+=4){
       magenta+=current[j]>250&&current[j+1]<5&&current[j+2]>250;
       unchanged+=memcmp(&baseline[j],&current[j],4)==0;
     }
     if(active?magenta>1000&&unchanged>10000:difference(baseline,current)==0){
       printf("semantic active=%d magenta=%zu unchanged=%zu\n",active,magenta,unchanged);return true;}
   }return false;
 };
 bool absent=wait(false);
 if(!load(argv[1],argv[2]))return 8;
 d=obs_data_create();obs_data_set_bool(d,"semantic_enabled",true);
 obs_data_set_bool(d,"semantic_analysis_only",true);obs_data_set_string(d,"semantic_model",argv[7]);
 obs_data_set_string(d,"semantic_classes","1");obs_data_set_double(d,"semantic_confidence",.2);
 obs_data_set_int(d,"privacy_edge",512);obs_data_set_int(d,"privacy_cadence",1);
 auto* producer=obs_source_create("sight_privacy_v2_filter","Semantic analysis",d,nullptr);obs_data_release(d);
 if(!producer)return 9;
 obs_source_filter_remove(source,filter);obs_source_filter_add(source,producer);obs_source_filter_add(source,filter);
 bool active=wait(true);
 bool category_ok=true,styles_ok=true;
 if(creative){
   auto set_category=[&](int category){
     auto* cfg=obs_source_get_settings(filter);
     obs_data_set_int(cfg,"param_category_id",category);
     obs_source_update(filter,cfg);obs_data_release(cfg);
   };
   set_category(2);category_ok=wait(false);
   set_category(-1);category_ok=wait(true)&&category_ok;
   set_category(1);category_ok=wait(true)&&category_ok;
   auto mode=[&](int value,double mix){
     auto* cfg=obs_source_get_settings(filter);
     obs_data_set_int(cfg,"param_region_mode",value);
     obs_data_set_double(cfg,"param_amount",mix);
     obs_source_update(filter,cfg);obs_data_release(cfg);
   };
   auto changed_style=[&](){
     for(int i=0;i<90;++i){
       std::this_thread::sleep_for(50ms);if(!draw(source,current,w,h))return false;
       size_t changed=0,unchanged=0,magenta=0;
       for(size_t j=0;j+3<current.size();j+=4){
         changed+=memcmp(&baseline[j],&current[j],4)!=0;
         unchanged+=memcmp(&baseline[j],&current[j],4)==0;
         magenta+=current[j]>250&&current[j+1]<5&&current[j+2]>250;
       }
       if(changed>1000&&unchanged>10000&&magenta<1000)return true;
     }return false;
   };
   mode(0,1);styles_ok=changed_style();
   Frame duotone=current;
   mode(1,1);styles_ok=changed_style()&&styles_ok;
   // The shader update is asynchronous; wait for mosaic to differ from duotone.
   bool distinct=false;
   for(int i=0;i<60;++i){
     std::this_thread::sleep_for(50ms);
     if(draw(source,current,w,h)&&difference(duotone,current)>1000&&difference(baseline,current)>1000){distinct=true;break;}
   }
   styles_ok=styles_ok&&distinct;
   mode(1,0);styles_ok=wait(false)&&styles_ok;
   mode(2,1);styles_ok=wait(true)&&styles_ok;

 }

 obs_source_filter_remove(source,filter);
 bool analysis_only=wait(false);
 obs_source_filter_add(source,filter);
 bool reattached=wait(true);
 obs_source_set_enabled(producer,false);
 bool disabled=wait(false);
 obs_source_set_enabled(producer,true);
 bool resumed=wait(true);
 obs_source_filter_remove(source,producer);obs_source_release(producer);
 bool removed=wait(false);
 bool pass=absent&&active&&analysis_only&&reattached&&disabled&&resumed&&removed&&category_ok&&styles_ok;
 printf("HYPERSHADER_SEMANTIC_REAL absent=%d active=%d analysis_only=%d reattached=%d disabled=%d resumed=%d removed=%d category=%d styles=%d pass=%d\n",absent,active,analysis_only,reattached,disabled,resumed,removed,category_ok,styles_ok,pass);
 obs_set_output_source(0,nullptr);obs_source_dec_showing(source);obs_source_filter_remove(source,filter);
 obs_source_release(filter);obs_source_release(source);obs_wait_for_destroy_queue();obs_shutdown();return pass?0:1;
}
