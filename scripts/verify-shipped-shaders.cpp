// Audit every shipped effect through the real loader and its advertised OBS lane.
#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <filesystem>
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <thread>
#include <mutex>
#include <cstring>
#include "shader/shader_display_name.h"
namespace fs=std::filesystem;
using namespace std::chrono_literals;
constexpr unsigned W=160,H=96;
using Frame=std::vector<uint8_t>;
static std::mutex uiMutex;
static std::vector<std::pair<obs_task_t,void*>> uiTasks;
static void queueUI(obs_task_t task,void*arg,bool wait){
 if(wait)std::abort();
 std::lock_guard<std::mutex> lock(uiMutex);uiTasks.emplace_back(task,arg);
}
static void drainUI(){
 std::vector<std::pair<obs_task_t,void*>> tasks;
 {std::lock_guard<std::mutex> lock(uiMutex);tasks.swap(uiTasks);}
 for(auto&t:tasks)t.first(t.second);
}
struct Fixture{gs_texture_t* texture=nullptr;};
static const char* fixtureName(void*){return "Audit video";}
static unsigned width(void*){return W;}static unsigned height(void*){return H;}
static void* create(obs_data_t*,obs_source_t*){
 auto*f=new Fixture;
 Frame frame(W*H*4);
 for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++){
 auto i=(y*W+x)*4;frame[i]=uint8_t(x*255/(W-1));frame[i+1]=uint8_t(y*255/(H-1));frame[i+2]=((x/13+y/11)%2)*200+20;frame[i+3]=255;}
 const uint8_t*p=frame.data();obs_enter_graphics();f->texture=gs_texture_create(W,H,GS_RGBA,1,&p,0);obs_leave_graphics();return f;
}
static void destroy(void*p){auto*f=static_cast<Fixture*>(p);obs_enter_graphics();gs_texture_destroy(f->texture);obs_leave_graphics();delete f;}
static void render(void*p,gs_effect_t*){obs_source_draw(static_cast<Fixture*>(p)->texture,0,0,W,H,false);}
static Frame capture(obs_source_t*source){
 Frame out;obs_enter_graphics();
 auto*target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
 auto*stage=gs_stagesurface_create(W,H,GS_RGBA);
 if(target&&stage&&gs_texrender_begin(target,W,H)){
 gs_projection_push();gs_matrix_push();gs_matrix_identity();gs_ortho(0,W,0,H,-100,100);
 struct vec4 clear{};gs_clear(GS_CLEAR_COLOR,&clear,0,0);obs_source_video_render(source);
 gs_matrix_pop();gs_projection_pop();gs_texrender_end(target);
 if(auto*tex=gs_texrender_get_texture(target)){
 gs_stage_texture(stage,tex);uint8_t*p=nullptr;uint32_t pitch=0;
 if(gs_stagesurface_map(stage,&p,&pitch)){out.resize(W*H*4);for(unsigned y=0;y<H;y++)std::memcpy(out.data()+y*W*4,p+y*pitch,W*4);gs_stagesurface_unmap(stage);}}}
 if(stage)gs_stagesurface_destroy(stage);if(target)gs_texrender_destroy(target);obs_leave_graphics();return out;
}
// Explicit shipped contract, independent of the metadata scanner. Updating the
// catalog requires an intentional role/input/fallback expectation here.
struct Contract {const char* file; bool source,audio,sight; const char* slots; bool absentPass, absentTransparent;};
static const Contract contracts[] = {
 {"audio_demo.effect",1,1,0,"0000",0,0},
 {"filter.afterimage_press.effect",0,1,0,"0000",0,0},
 {"filter.contact_filaments.effect",0,1,1,"0000",1,0},
 {"filter.face_mesh_wireframe.effect",0,0,1,"0000",1,0},
 {"filter.ink_currents.effect",0,0,0,"0000",0,0},
 {"filter.liquid_light.effect",0,1,0,"0000",0,0},
 {"filter.reactive_sprite.effect",0,1,0,"1110",1,0},
 {"filter.ripple_glass.effect",0,0,0,"0000",0,0},
 {"filter.sight_depth_glass.effect",0,1,1,"0000",1,0},
 {"filter.sight_flow_echo.effect",0,0,1,"0000",1,0},
 {"filter.sight_flow_sparks.effect",0,1,1,"0000",1,0},
 {"filter.sight_flow_warp.effect",0,0,1,"0000",1,0},
 {"filter.sight_hand_highlights.effect",0,0,1,"0000",1,0},
 {"filter.sight_hand_ripples.effect",0,0,1,"0000",1,0},
 {"filter.sight_motion_pulse.effect",0,1,1,"0000",1,0},
 {"filter.sight_relight.effect",0,0,1,"0000",1,0},
 {"filter.sight_semantic_materials.effect",0,0,1,"0000",1,0},
 {"filter.sight_semantic_prism.effect",0,0,1,"0000",1,0},
 {"filter.sight_subject_frame.effect",0,0,1,"0000",1,0},
 {"filter.sight_text_scramble.effect",0,0,1,"0000",1,0},
 {"filter.topographic_etch.effect",0,1,1,"0000",1,0},
 {"filter.vidicon_afterglow.effect",0,0,0,"0000",0,0},
 {"ink_reaction_buffers.effect",0,0,0,"0000",0,0},
 {"optical_halation.effect",0,0,0,"0000",0,0},
 {"pframe_drift.effect",0,0,0,"0000",0,0},
 {"playground.effect",0,0,0,"0000",0,0},
 {"shaderfilter_demo.effect",0,0,0,"0000",0,0},
 {"source.audio_spectrum_orbit.effect",1,1,0,"0000",0,0},
 {"source.brass_register.effect",1,1,0,"0000",0,0},
 {"source.paper_tide.effect",1,1,0,"0000",0,0},
 {"source.phosphor_scope.effect",1,1,0,"1000",0,0},
 {"source.reactive_sprite.effect",1,1,0,"1111",0,1},
 {"source.spectral_garden.effect",1,1,0,"0000",0,0},
 {"source.spectral_strata.effect",1,1,0,"1000",0,1},
 {"template.effect",0,0,0,"0000",0,0},
 {"warp_tape.effect",0,0,0,"0000",0,0},
};
int main(int argc,char**argv){
 if(argc!=3||!obs_startup("en-US",nullptr,nullptr))return 2;
 obs_set_ui_task_handler(queueUI);
 obs_video_info v{};v.graphics_module="libobs-opengl";v.fps_num=30;v.fps_den=1;
 v.base_width=v.output_width=W;v.base_height=v.output_height=H;v.output_format=VIDEO_FORMAT_RGBA;v.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&v)!=OBS_VIDEO_SUCCESS)return 3;
 obs_source_info fixture{};fixture.id="hs_audit_input";fixture.type=OBS_SOURCE_TYPE_INPUT;fixture.output_flags=OBS_SOURCE_VIDEO;
 fixture.get_name=fixtureName;fixture.create=create;fixture.destroy=destroy;fixture.get_width=width;fixture.get_height=height;fixture.video_render=render;
 obs_register_source(&fixture);
 obs_module_t*mod=nullptr;if(obs_open_module(&mod,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(mod))return 4;
 auto*base=obs_source_create("hs_audit_input","Audit base",nullptr,nullptr);obs_source_inc_showing(base);
 auto reference=capture(base);std::vector<fs::path> files;
 auto root=fs::path(argv[2])/"shaders/root";
 for(auto&e:fs::directory_iterator(root))if(e.is_regular_file()&&e.path().extension()==".effect")files.push_back(e.path());
 std::sort(files.begin(),files.end());bool all=files.size()==sizeof(contracts)/sizeof(contracts[0]);
 for(auto&file:files){
 char label[512];auto rel="root/"+file.filename().string();
 if(!hs_shader_display_label_from_file(label,sizeof(label),rel.c_str(),file.c_str()))return 5;
 bool source=std::string(label).rfind("Source / ",0)==0;
 auto*settings=obs_data_create();
 obs_data_set_int(settings,"preset",-1);obs_data_set_int(settings,"preset_schema",2);
 obs_data_set_int(settings,"source_width",W);obs_data_set_int(settings,"source_height",H);
 auto*effect=obs_source_create(source?"hypershader_source":"hypershader_filter",file.filename().c_str(),settings,nullptr);
 obs_data_release(settings);if(!effect)return 6;
 if(source){obs_source_inc_showing(effect);obs_set_output_source(0,effect);}else{obs_source_filter_add(base,effect);obs_set_output_source(0,base);}
 auto path="shaders/"+rel;
 auto*priv=obs_source_get_private_settings(effect);obs_data_set_string(priv,"hypershader_builder_compile_path",path.c_str());
 obs_data_set_string(priv,"hypershader_builder_compile_state","pending");obs_data_release(priv);
 settings=obs_source_get_settings(effect);obs_data_set_string(settings,"shader_file",path.c_str());obs_source_update(effect,settings);obs_data_release(settings);
 std::string state,detail;Frame frame;
 for(int i=0;i<100;i++){drainUI();frame=capture(source?effect:base);std::this_thread::sleep_for(25ms);
 priv=obs_source_get_private_settings(effect);state=obs_data_get_string(priv,"hypershader_builder_compile_state");
 detail=obs_data_get_string(priv,"hypershader_builder_compile_error");obs_data_release(priv);
 if(state=="failed"|| (state=="succeeded"&&i>=20))break;}
 auto*props=obs_source_properties(effect);
 auto visible=[&](const char*key){auto*p=obs_properties_get(props,key);return p&&obs_property_visible(p);};
 size_t pixels=0,changed=0,opaque=0;unsigned min=255,max=0;
 for(size_t i=0;i+3<frame.size();i+=4){
  pixels+=frame[i]||frame[i+1]||frame[i+2];opaque+=frame[i+3]>0;
  for(int j=0;j<3;j++){min=std::min(min,unsigned(frame[i+j]));max=std::max(max,unsigned(frame[i+j]));
  if(reference.size()==frame.size()&&abs(int(frame[i+j])-int(reference[i+j]))>3)changed++;}
 }
 auto it=std::find_if(std::begin(contracts),std::end(contracts),[&](const Contract& c){return file.filename()==c.file;});
 std::string slots;
 for(int i=0;i<4;i++)slots+=visible(("channel"+std::to_string(i)+"_group").c_str())?'1':'0';
 bool inputs=it!=std::end(contracts)&&source==it->source&&visible("audio_source")==it->audio&&
     visible("sight_tracking_channel")==it->sight&&slots==it->slots;
 bool fallback=it!=std::end(contracts)&&(!it->absentPass||frame==reference)&&(!it->absentTransparent||opaque==0);
 bool defaults=true;
 if(file.filename()=="template.effect"){
   auto*cfg=obs_source_get_settings(effect);
   defaults=obs_data_get_double(cfg,"param_speed")>0.0&&obs_data_get_double(cfg,"param_decay")>0.0;
   obs_data_release(cfg);
 }
 bool behavior=true;
 if(file.filename()=="playground.effect"&&state=="succeeded"){
   auto apply=[&](int bg,int distortion,int post,double strength){
     auto*cfg=obs_source_get_settings(effect);
     obs_data_set_int(cfg,"param_bg_mode",bg);
     obs_data_set_int(cfg,"param_bg_color",0xff996633);
     obs_data_set_int(cfg,"param_dist_mode",distortion);
     obs_data_set_double(cfg,"param_dist_strength",strength);
     obs_data_set_int(cfg,"param_filter_mode",post);
     obs_data_set_double(cfg,"param_filter_strength",strength);
     obs_source_update(effect,cfg);obs_data_release(cfg);
     Frame out;
     for(int i=0;i<12;i++){drainUI();out=capture(base);std::this_thread::sleep_for(25ms);}
     return out;
   };
   const auto solid=apply(3,0,0,1);
   const auto chromatic=apply(3,0,1,1);
   const auto clean=apply(0,0,0,0);
   const auto zeroPixelation=apply(0,2,0,0);
   const auto pixelated=apply(0,2,0,1);
   bool solidPreserved=!solid.empty()&&solid==chromatic;
   bool zeroBypass=!clean.empty()&&clean==zeroPixelation;
   bool active=!clean.empty()&&clean!=pixelated;
   behavior=solidPreserved&&zeroBypass&&active;
   std::cout<<"PLAYGROUND solid_chromatic="<<solidPreserved<<" zero_pixelate="<<zeroBypass<<" active_pixelate="<<active<<" PASS="<<behavior<<std::endl;
 }
 bool ok=state=="succeeded"&&frame.size()==W*H*4&&inputs&&fallback&&defaults&&behavior;
 std::replace(detail.begin(),detail.end(),'\n',' ');
 std::cout<<"SHIPPED "<<file.filename().string()<<" role="<<(source?"source":"filter")<<" compile="<<state
 <<" audio="<<visible("audio_source")<<" sight="<<visible("sight_tracking_channel")
 <<" textures="<<visible("channel0_group")<<visible("channel1_group")<<visible("channel2_group")<<visible("channel3_group")
 <<" inputs="<<inputs<<" fallback="<<fallback<<" defaults="<<defaults<<" pixels="<<pixels<<" alpha="<<opaque<<" range="<<min<<":"<<max<<" changed="<<changed<<" PASS="<<ok<<" "<<detail<<std::endl;
 all&=ok;obs_properties_destroy(props);obs_set_output_source(0,nullptr);
 if(source)obs_source_dec_showing(effect);else obs_source_filter_remove(base,effect);
 obs_source_remove(effect);obs_source_release(effect);drainUI();obs_wait_for_destroy_queue();
 }
 obs_source_dec_showing(base);obs_source_remove(base);obs_source_release(base);drainUI();obs_wait_for_destroy_queue();obs_shutdown();
 std::cout<<"SHIPPED_TOTAL "<<files.size()<<" PASS="<<all<<std::endl;return all?0:1;
}
