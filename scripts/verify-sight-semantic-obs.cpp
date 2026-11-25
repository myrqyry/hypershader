#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <utility>
#include "interop/sight_client.h"
using namespace std::chrono_literals;
using Frame=std::vector<uint8_t>;
static std::mutex ui_mutex;
static std::vector<std::pair<obs_task_t,void*>> ui_tasks;
static void queue_ui(obs_task_t task,void* arg,bool wait){
    if(wait)std::abort();
    std::lock_guard<std::mutex> lock(ui_mutex);ui_tasks.emplace_back(task,arg);
}
static void drain_ui(){
    std::vector<std::pair<obs_task_t,void*>> tasks;
    {std::lock_guard<std::mutex> lock(ui_mutex);tasks.swap(ui_tasks);}
    for(auto& t:tasks)t.first(t.second);
}
static bool draw(obs_source_t* source,Frame& frame,unsigned w,unsigned h){
    drain_ui();
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
 obs_set_ui_task_handler(queue_ui);
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
 const std::string full=argv[9],root=std::string(argv[4])+"/";
 if(full.rfind(root,0)!=0)return 10;
 const std::string relative=full.substr(root.size());
 d=obs_data_create();obs_data_set_string(d,"shader_file",relative.c_str());
 obs_data_set_bool(d,"sight_use_tracking",true);
 const bool creative=std::strstr(argv[9],"semantic_prism")!=nullptr;
 const bool materials=std::strstr(argv[9],"semantic_materials")!=nullptr;
 if(materials){
   obs_data_set_int(d,"param_category_a",1);
   obs_data_set_int(d,"param_category_b",0);
   obs_data_set_int(d,"param_category_c",-1);
   obs_data_set_int(d,"param_material_a",0);
   obs_data_set_int(d,"param_material_b",0);
   obs_data_set_int(d,"param_color_a",0xffff00ffu);
   obs_data_set_int(d,"param_color_b",0xff00ff00u);
   obs_data_set_double(d,"param_mix_a",1.0);
   obs_data_set_double(d,"param_mix_b",1.0);
 }

 if(creative){
   obs_data_set_int(d,"param_region_mode",2);
   obs_data_set_int(d,"param_category_id",1);
   obs_data_set_double(d,"param_amount",1.0);
   obs_data_set_int(d,"param_ink_color",0xffff00ffu);
 }
 auto* filter=obs_source_create("hypershader_filter","Semantic probe",d,nullptr);obs_data_release(d);
 if(!filter)return 7;
 obs_source_filter_add(source,filter);obs_source_inc_showing(source);obs_set_output_source(0,source);
 bool compiled=false;
 for(int i=0;i<100;++i){
   std::this_thread::sleep_for(30ms);if(!draw(source,current,w,h))return 11;
   auto* priv=obs_source_get_private_settings(filter);
   const std::string state=obs_data_get_string(priv,"hypershader_live_state");
   const std::string path=obs_data_get_string(priv,"hypershader_live_path");
   obs_data_release(priv);
   if(state=="ready"&&path==relative){compiled=true;break;}
   if(state=="failed")break;
 }
 if(!compiled){puts("FAIL: shader did not reach ready state");return 12;}

 auto wait=[&](bool active){
   for(int i=0;i<90;++i){std::this_thread::sleep_for(50ms);if(!draw(source,current,w,h))return false;
     size_t magenta=0,green=0,unchanged=0;
     for(size_t j=0;j+3<current.size();j+=4){
       magenta+=current[j]>250&&current[j+1]<5&&current[j+2]>250;
       green+=current[j]<5&&current[j+1]>250&&current[j+2]<5;
       unchanged+=memcmp(&baseline[j],&current[j],4)==0;
     }
     if(i==89)printf("semantic wait exhausted active=%d magenta=%zu green=%zu unchanged=%zu\n",active,magenta,green,unchanged);
     if(active?magenta>1000&&(materials?green>1000:unchanged>10000):difference(baseline,current)==0){
       printf("semantic active=%d magenta=%zu green=%zu unchanged=%zu\n",active,magenta,green,unchanged);return true;}
   }return false;
 };
 bool absent=wait(false);
 if(!load(argv[1],argv[2]))return 8;
 d=obs_data_create();obs_data_set_bool(d,"semantic_enabled",true);
 obs_data_set_bool(d,"semantic_analysis_only",true);obs_data_set_string(d,"semantic_model",argv[7]);
 obs_data_set_string(d,"semantic_classes",materials?"0,1":"1");obs_data_set_double(d,"semantic_confidence",.2);
 obs_data_set_int(d,"privacy_edge",512);obs_data_set_int(d,"privacy_cadence",1);
 auto* producer=obs_source_create("sight_privacy_v2_filter","Semantic analysis",d,nullptr);obs_data_release(d);
 if(!producer)return 9;
 obs_source_filter_remove(source,filter);obs_source_filter_add(source,producer);obs_source_filter_add(source,filter);
 bool active=wait(true);
 bool category_ok=true,styles_ok=true;
 if(materials){
   auto set_mix=[&](double mix){
     auto* cfg=obs_source_get_settings(filter);
     obs_data_set_double(cfg,"param_mix_b",mix);
     obs_source_update(filter,cfg);obs_data_release(cfg);
   };
   set_mix(0.0);
   bool only_a=false;
   for(int i=0;i<90;++i){
     std::this_thread::sleep_for(50ms);
     if(!draw(source,current,w,h))break;
     size_t magenta=0,green=0,unchanged=0;
     for(size_t j=0;j+3<current.size();j+=4){
       magenta+=current[j]>250&&current[j+1]<5&&current[j+2]>250;
       green+=current[j]<5&&current[j+1]>250&&current[j+2]<5;
       unchanged+=memcmp(&baseline[j],&current[j],4)==0;
     }
     if(magenta>1000&&green<1000&&unchanged>10000){only_a=true;break;}
   }
   set_mix(1.0);
   category_ok=only_a&&wait(true);
   printf("semantic materials isolated=%d restored=%d\n",only_a,category_ok);
 }

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
 // Optional model-backed fusion on the same source and in analysis-only order.
 bool fusion_ok=true;
 const char* depth_model=std::getenv("SIGHT_MATERIAL_DEPTH_MODEL");
 const char* matte_model=std::getenv("SIGHT_MATERIAL_MATTE_MODEL");
 if(materials&&bool(depth_model)!=bool(matte_model)){puts("Supply both fusion model paths");return 14;}
 if(materials&&depth_model&&matte_model){
   obs_source_filter_remove(source,filter);
   d=obs_data_create();obs_data_set_string(d,"model_path",depth_model);
   obs_data_set_int(d,"view_mode",2);obs_data_set_int(d,"inference_edge",252);
   obs_data_set_int(d,"inference_cadence",4);
   auto* depth=obs_source_create("sight_depth_filter","Material depth",d,nullptr);obs_data_release(d);
   d=obs_data_create();obs_data_set_string(d,"model_path",matte_model);
   obs_data_set_int(d,"matte_view",4);
   auto* matte=obs_source_create("sight_u2net_filter","Material matte",d,nullptr);obs_data_release(d);
   if(!depth||!matte)return 13;
   obs_source_filter_add(source,depth);obs_source_filter_add(source,matte);obs_source_filter_add(source,filter);
   auto set=[&](const char* key,double value){
     auto* cfg=obs_source_get_settings(filter);obs_data_set_double(cfg,key,value);
     obs_source_update(filter,cfg);obs_data_release(cfg);
   };
   auto check=[&](auto predicate,int count=180){
     for(int i=0;i<count;++i){std::this_thread::sleep_for(50ms);if(!draw(source,current,w,h))return false;
       if(predicate())return true;}
     return false;
   };
   // Observe a stable solid-material reference with the real semantic producer.
   bool solid=wait(true);Frame unmasked=current;
   set("param_matte_mix",1);
   bool clipped=check([&](){return difference(current,unmasked)>10000&&difference(current,baseline)>10000;},400);
   obs_source_set_enabled(matte,false);
   bool matte_lost=check([&](){return difference(current,unmasked)<100;});
   obs_source_set_enabled(matte,true);
   bool matte_restored=check([&](){return difference(current,unmasked)>10000&&difference(current,baseline)>10000;},400);
   set("param_matte_mix",0);set("param_material_a",3);set("param_material_b",3);
   set("param_speed",0);set("param_flow_mix",0);set("param_depth_mix",0);
   // Use no time animation; track IDs may change with inference cadence,
   // so compare the full image and repeat until the depth response is clear.
   bool metal=check([&](){return difference(current,unmasked)>10000&&difference(current,baseline)>10000;});
   Frame flat=current;
   set("param_depth_mix",1);
   bool shaped=check([&](){return difference(current,flat)>10000&&difference(current,baseline)>10000;},400);
   // Validate that a fresh actual depth snapshot was consumed in the shader,
   // plus fallback when disabled (the controlled test covers exact pixels).
   obs_source_set_enabled(depth,false);
   bool depth_lost=check([&](){return difference(current,flat)<1000;});
   obs_source_set_enabled(depth,true);
   bool depth_restored=check([&](){return difference(current,flat)>10000&&difference(current,baseline)>10000;},400);
   fusion_ok=solid&&clipped&&matte_lost&&matte_restored&&metal&&shaped&&depth_lost&&depth_restored;
   const char* shifted=std::getenv("SIGHT_MATERIAL_FLOW_SHIFT_IMAGE");
   if(shifted){
     obs_source_filter_remove(source,filter);
     d=obs_data_create();obs_data_set_int(d,"flow_view",0);obs_data_set_int(d,"flow_interval",150);
     auto* flow=obs_source_create("sight_spatial_motion_filter","Material motion",d,nullptr);obs_data_release(d);
     if(!flow)return 15;
     obs_source_filter_add(source,flow);obs_source_filter_add(source,filter);
     set("param_flow_mix",1);set("param_matte_mix",1);set("param_material_a",2);
     set("param_surface_lock",1);set("param_surface_preview",1);
     sight_flow_v1 snap{};
     auto stationary=[&](){
       if(!hs_sight_flow_read(filter,&snap))return false;
       unsigned moving=0;
       for(auto& c:snap.cells)moving+=c.confidence>.5f&&(std::abs(c.dx)>.002f||std::abs(c.dy)>.002f);
       return moving<5;
     };
     auto offset_pixels=[&](bool translated){
       size_t n=0;
       for(size_t j=0;j+3<current.size();j+=4)
         n+=current[j+2]>170&&(translated?current[j]<119:
             (current[j]>=124&&current[j]<=132&&current[j+1]>=124&&current[j+1]<=132));
       return n;
     };
     bool ready=check([&](){return stationary()&&offset_pixels(false)>1000;},200);
     uint64_t predecessor=0;bool linked=false;
     bool link_ready=hs_sight_flow_read_linked(filter,&snap,&predecessor,&linked)&&linked;
     auto image=[&](const char* path){
       auto* cfg=obs_data_create();obs_data_set_string(cfg,"file",path);
       obs_source_update(source,cfg);obs_data_release(cfg);
     };
     image(shifted);
     unsigned moving=0;
     bool moved=check([&](){
       if(!hs_sight_flow_read(filter,&snap))return false;
       moving=0;
       for(auto& c:snap.cells)moving+=c.confidence>.5f&&(std::abs(c.dx)>.003f||std::abs(c.dy)>.003f);
       return moving>30;
     },120);
     // Once a translated frame is followed by a stationary pair, the
     // uploaded offset must remain nonzero: not just instantaneous flow.
     bool anchored=check([&](){return offset_pixels(true)>1000;},100);
     bool retained=check([&](){return stationary()&&offset_pixels(true)>1000;},100);
     printf("SEMANTIC_SURFACE_REAL linked=%d anchored=%d retained=%d pixels=%zu\n",
          link_ready,anchored,retained,offset_pixels(true));
     obs_source_set_enabled(flow,false);
     bool lost=check([&](){return !hs_sight_flow_read(filter,&snap);});
     image(argv[8]);obs_source_set_enabled(flow,true);
     bool restored=check(stationary,200);
     printf("SEMANTIC_FLOW_REAL ready=%d moved=%d cells=%u lost=%d restored=%d PASS=%d\n",
         ready,moved,moving,lost,restored,ready&&moved&&lost&&restored);
     fusion_ok=fusion_ok&&ready&&moved&&lost&&restored&&link_ready&&anchored&&retained;
     obs_source_filter_remove(source,flow);obs_source_release(flow);
     set("param_matte_mix",0);set("param_surface_preview",0);
     // Restore original image before the final no-producer fallback check.
   }

   printf("SEMANTIC_FUSION_REAL matte=%d matte_lost=%d matte_restored=%d depth=%d depth_lost=%d depth_restored=%d PASS=%d\n",clipped,matte_lost,matte_restored,shaped,depth_lost,depth_restored,fusion_ok);
   obs_source_filter_remove(source,matte);obs_source_release(matte);
   obs_source_filter_remove(source,depth);obs_source_release(depth);
 }
 obs_source_filter_remove(source,producer);obs_source_release(producer);
 bool removed=wait(false);
 bool pass=absent&&active&&analysis_only&&reattached&&disabled&&resumed&&removed&&category_ok&&styles_ok&&fusion_ok;
 printf("HYPERSHADER_SEMANTIC_REAL absent=%d active=%d analysis_only=%d reattached=%d disabled=%d resumed=%d removed=%d category=%d styles=%d pass=%d\n",absent,active,analysis_only,reattached,disabled,resumed,removed,category_ok,styles_ok,pass);
 obs_set_output_source(0,nullptr);obs_source_dec_showing(source);obs_source_filter_remove(source,filter);
 obs_source_release(filter);obs_source_release(source);drain_ui();obs_wait_for_destroy_queue();drain_ui();obs_shutdown();return pass?0:1;
}
