// Native libobs UI contract with the real asynchronous shader loader.
#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <string>
#include <cstdio>
#include <thread>
#include <chrono>
#include <iostream>
using namespace std::chrono_literals;
static const unsigned W=240,H=136;
static bool group_item(obs_properties_t* props,const char* group,const char* key) {
 auto* gp=props?obs_properties_get(props,group):nullptr;
 auto* children=gp&&obs_property_get_type(gp)==OBS_PROPERTY_GROUP?
    obs_property_group_content(gp):nullptr;
 return children&&obs_properties_get(children,key);
}
static bool no_generated_builtin(obs_properties_t* props) {
 if(!props)return false;
 for(auto* it=obs_properties_first(props);it;obs_property_next(&it)){
   const std::string name=obs_property_name(it);
   if(name=="param_iProgress" || name=="param_iScale")return false;
   if(obs_property_get_type(it)==OBS_PROPERTY_GROUP &&
       !no_generated_builtin(obs_property_group_content(it)))return false;
 }
 return true;
}
static void render_frames(obs_source_t* src,int frames){
 for(int i=0;i<frames;i++){
   obs_enter_graphics();
   auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
   if(target && gs_texrender_begin(target,W,H)){
     gs_projection_push();gs_matrix_push();gs_matrix_identity();
     gs_ortho(0,W,0,H,-100,100);
     obs_source_video_render(src);
     gs_matrix_pop();gs_projection_pop();gs_texrender_end(target);
   }
   if(target)gs_texrender_destroy(target);
   obs_leave_graphics();
   std::this_thread::sleep_for(25ms);
 }
}
int main(int argc,char** argv) {
 if(argc!=5)return 2;
 if(!obs_startup("en-US",nullptr,nullptr))return 1;
 obs_video_info video{};
 video.graphics_module="libobs-opengl";video.fps_num=30;video.fps_den=1;
 video.base_width=W;video.base_height=H;
 video.output_width=W;video.output_height=H;
 video.output_format=VIDEO_FORMAT_RGBA;video.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&video)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 1;}
 obs_module_t* module=nullptr;
 if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS ||
    !obs_init_module(module)){obs_shutdown();return 1;}
 bool good=true;
 const char* files[]={argv[3],argv[4]};
 for(int test=0;test<2;test++){
   auto* settings=obs_data_create();
   obs_data_set_string(settings,"shader_file",files[test]);
   obs_data_set_int(settings,"source_width",W);
   obs_data_set_int(settings,"source_height",H);
   auto* src=obs_source_create("hypershader_source",
                test==0?"Shabon Aurora UI":"Blurred Gradient channel UI",settings,nullptr);
   obs_data_release(settings);
   if(!src){std::cerr<<"SOURCE_CREATE_FAILED="<<test<<"\n";good=false;break;}
   obs_source_inc_showing(src);
   render_frames(src,55);
   obs_properties_t* properties=obs_source_properties(src);
   const bool localControls = no_generated_builtin(properties);
   const bool globalOne=properties &&
        obs_properties_get(properties,"global_overrides_group") &&
        !obs_properties_get(properties,"controls_group") &&
        !obs_properties_get(properties,"composition_group") &&
        group_item(properties,"global_overrides_group","speed") &&
        group_item(properties,"global_overrides_group","reset_time") &&
        group_item(properties,"global_overrides_group","compose_base_opacity") &&
        group_item(properties,"global_overrides_group","compose_effect_opacity") &&
        group_item(properties,"global_overrides_group","compose_blend_mode") &&
        std::string(obs_property_description(
           obs_properties_get(properties,"global_overrides_group")))=="Global Overrides";
   bool channels=true;
   for(int i=0;i<4;i++) {
     char key[32];snprintf(key,sizeof(key),"channel%d_group",i);
     auto* group=properties?obs_properties_get(properties,key):nullptr;
     if(!group){channels=false;continue;}
     const bool needed=(test==1 && i==0);
     channels &= obs_property_visible(group)==needed;
     if(needed) {
       channels &= std::string(obs_property_description(group))=="Texture Input 0";
       auto* inside=obs_property_group_content(group);
       auto* picker=inside?obs_properties_get(inside,"channel0_type"):nullptr;
       channels &= picker && obs_property_list_item_count(picker)==4 &&
          obs_property_list_item_int(picker,0)==0;
     }
   }
   bool optionalInputsWork=true;
   if(test==1 && properties) {
     auto* channel=obs_properties_get(properties,"channel0_group");
     auto* inside=channel?obs_property_group_content(channel):nullptr;
     auto* type=inside?obs_properties_get(inside,"channel0_type"):nullptr;
     auto* image=inside?obs_properties_get(inside,"channel0_file"):nullptr;
     auto* source=inside?obs_properties_get(inside,"channel0_source"):nullptr;
     auto* changed=obs_source_get_settings(src);
     if(type && image && source && changed){
       obs_data_set_int(changed,"channel0_type",1);
       obs_property_modified(type,changed);
       optionalInputsWork &= obs_property_visible(image) && !obs_property_visible(source);
       obs_data_set_int(changed,"channel0_type",2);
       obs_property_modified(type,changed);
       optionalInputsWork &= !obs_property_visible(image) && obs_property_visible(source);
       obs_data_set_int(changed,"channel0_type",0);
       obs_property_modified(type,changed);
       optionalInputsWork &= !obs_property_visible(image) && !obs_property_visible(source);
     } else optionalInputsWork=false;
     if(changed)obs_data_release(changed);
   }
   bool shaderSpecific=true;
   if(test==0) {
     auto* group=properties?obs_properties_get(properties,"shader_params_group"):nullptr;
     auto* nested=group?obs_property_group_content(group):nullptr;
     shaderSpecific=nested && group_item(nested,"group_Shabon FX","param_uColorA")
                       && group_item(nested,"group_Shabon FX","param_uIntensity");
   }
   // Read-only explanatory shader information, not an editable notes field.
   auto* informationGroup=properties?obs_properties_get(properties,"shader_information_group"):nullptr;
   auto* infoProps=informationGroup?obs_property_group_content(informationGroup):nullptr;
   auto* notesGroup=infoProps?obs_properties_get(infoProps,"shader_notes_group"):nullptr;
   auto* creditGroup=infoProps?obs_properties_get(infoProps,"shader_credits_group"):nullptr;
   auto* notesProps=notesGroup?obs_property_group_content(notesGroup):nullptr;
   auto* creditProps=creditGroup?obs_property_group_content(creditGroup):nullptr;
   bool information=informationGroup && notesGroup && creditGroup && notesProps && creditProps &&
       !obs_properties_get(properties,"notes_credits_group") &&
      std::string(obs_property_description(informationGroup))=="Shader Information" &&
      std::string(obs_property_description(notesGroup))=="Notes" &&
      std::string(obs_property_description(creditGroup))=="Credits";
   // All informational fields must be display-only text, not OBS settings.
   auto readonly=[&](obs_properties_t* props) {
     if(!props)return false;
     bool ok=true;
     for(auto* it=obs_properties_first(props);it;obs_property_next(&it)){
       std::string key=obs_property_name(it);
       ok &= obs_property_get_type(it)==OBS_PROPERTY_TEXT &&
             obs_property_text_type(it)==OBS_TEXT_INFO &&
             key.rfind("shader_notes_",0)!=0;
     }
     return ok;
   };
   information &= readonly(notesProps) && readonly(creditProps);
   auto* about=notesProps?obs_properties_get(notesProps,"shader_about"):nullptr;
   auto* credit=creditProps?obs_properties_get(creditProps,"shader_credit"):nullptr;
   auto* upstream=creditProps?obs_properties_get(creditProps,"shader_source"):nullptr;
   auto* license=creditProps?obs_properties_get(creditProps,"shader_license"):nullptr;
   if(test==0){
     information &= about && credit && upstream && license &&
        std::string(obs_property_long_description(about)).find("MIT-licensed GLSL port")!=std::string::npos &&
        std::string(obs_property_long_description(credit)).find("Wrennly")!=std::string::npos &&
        std::string(obs_property_long_description(upstream)).find("github.com/wrennly/shabon-fx")!=std::string::npos &&
        std::string(obs_property_long_description(license)).find("MIT")!=std::string::npos;
   } else {
     information &= credit && !upstream && !license &&
        std::string(obs_property_long_description(credit))==
             "Not specified in this shader's header.";
   }
   auto* saved=obs_source_get_settings(src);
   // Old notes, if present in a preexisting scene, must not be erased.
   if(saved)obs_data_set_string(saved,"shader_notes_legacy_value","Keep this");
   bool keysUntouched=saved &&
        obs_data_get_int(saved,"channel0_type")==0 &&
        obs_data_get_double(saved,"speed")==1.0 &&
        obs_data_get_double(saved,"compose_effect_opacity")==1.0;
   if(saved)obs_data_release(saved);
   const bool ok=localControls && globalOne && channels && optionalInputsWork &&
                 shaderSpecific && information && keysUntouched;
   std::cout<<"SHADER_UI_BOUNDARIES case="<<(test==0?"aurora":"channel0")
            <<" builtins_hidden="<<localControls
            <<" global_group="<<globalOne
            <<" channels_correct="<<channels
            <<" channel_picker_works="<<optionalInputsWork
            <<" actual_shader_controls="<<shaderSpecific
            <<" read_only_information="<<information
            <<" persisted_keys="<<keysUntouched
            <<" PASS="<<ok<<"\n";
   good &= ok;
   if(properties)obs_properties_destroy(properties);
   if(test==0){
     // Switching the actual shader updates the displayed credits, while
     // legacy settings data remains untouched and no editor appears.
     bool switching=true;
     auto* settings0=obs_source_get_settings(src);
     obs_data_set_string(settings0,"shader_notes_legacy_value","Keep this");
     obs_data_set_string(settings0,"shader_file",argv[4]);
     obs_source_update(src,settings0);
     obs_data_release(settings0);
     render_frames(src,45);
     auto* other=obs_source_properties(src);
     auto* infoB=other?obs_properties_get(other,"shader_information_group"):nullptr;
     auto* nestedB=infoB?obs_property_group_content(infoB):nullptr;
     auto* creditsB=nestedB?obs_properties_get(nestedB,"shader_credits_group"):nullptr;
     auto* creditFieldsB=creditsB?obs_property_group_content(creditsB):nullptr;
     auto* creditB=creditFieldsB?obs_properties_get(creditFieldsB,"shader_credit"):nullptr;
     switching &= creditB && std::string(obs_property_long_description(creditB))==
              "Not specified in this shader's header.";
     if(other)obs_properties_destroy(other);
     auto* settings1=obs_source_get_settings(src);
     switching &= settings1 &&
        std::string(obs_data_get_string(settings1,"shader_notes_legacy_value"))=="Keep this";
     if(settings1){
       obs_data_set_string(settings1,"shader_file",argv[3]);
       obs_source_update(src,settings1);
       obs_data_release(settings1);
     }
     render_frames(src,45);
     auto* again=obs_source_properties(src);
     auto* infoA=again?obs_properties_get(again,"shader_information_group"):nullptr;
     auto* nestedA=infoA?obs_property_group_content(infoA):nullptr;
     auto* creditsA=nestedA?obs_properties_get(nestedA,"shader_credits_group"):nullptr;
     auto* creditFieldsA=creditsA?obs_property_group_content(creditsA):nullptr;
     auto* creditA=creditFieldsA?obs_properties_get(creditFieldsA,"shader_credit"):nullptr;
     switching &= creditA &&
        std::string(obs_property_long_description(creditA)).find("Wrennly")!=std::string::npos;
     if(again)obs_properties_destroy(again);
     auto* settings2=obs_source_get_settings(src);
     switching &= settings2 &&
        std::string(obs_data_get_string(settings2,"shader_notes_legacy_value"))=="Keep this";
     if(settings2)obs_data_release(settings2);
     std::cout<<"SHADER_INFO_SWITCH different_headers="<<switching
              <<" legacy_values_preserved="<<switching<<" PASS="<<switching<<"\n";
     good &= switching;
   }
   obs_source_dec_showing(src);
   obs_source_release(src);
 }
 obs_shutdown();
 return good?0:1;
}
