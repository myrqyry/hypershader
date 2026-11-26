// Native OBS integration: an ordinary HyperShader source must load
// its preset, preserve alpha, and respond to PCM from a selected OBS source.
#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <array>
#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <algorithm>
using namespace std::chrono_literals;
constexpr unsigned W=256,H=256;
using Frame=std::vector<uint8_t>;
static const char* fixture_name(void*) { return "OBS PCM fixture"; }
static void* fixture_create(obs_data_t*,obs_source_t*) {return new int(1);}
static void fixture_destroy(void* p) {delete (int*)p;}
static obs_source_info fixture_info={
 .id="hyper_visualizer_pcm_fixture",.type=OBS_SOURCE_TYPE_INPUT,
 .output_flags=OBS_SOURCE_AUDIO,.get_name=fixture_name,
 .create=fixture_create,.destroy=fixture_destroy,
};
static Frame render(obs_source_t* source) {
 Frame frame(W*H*4,0);
 obs_enter_graphics();
 auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
 auto* staging=gs_stagesurface_create(W,H,GS_RGBA);
 if(target&&staging&&gs_texrender_begin(target,W,H)) {
   gs_projection_push();gs_matrix_push();gs_matrix_identity();
   gs_ortho(0,float(W),0,float(H),-100,100);
   struct vec4 clear={0,0,0,0};
   gs_clear(GS_CLEAR_COLOR,&clear,0,0);
   obs_source_video_render(source);
   gs_matrix_pop();gs_projection_pop();gs_texrender_end(target);
   if(auto* tex=gs_texrender_get_texture(target)) {
     gs_stage_texture(staging,tex);
     uint8_t* ptr=nullptr;uint32_t pitch=0;
     if(gs_stagesurface_map(staging,&ptr,&pitch)) {
       for(unsigned y=0;y<H;y++)memcpy(frame.data()+y*W*4,ptr+y*pitch,W*4);
       gs_stagesurface_unmap(staging);
     }
   }
 }
 if(staging)gs_stagesurface_destroy(staging);
 if(target)gs_texrender_destroy(target);
 obs_leave_graphics();
 return frame;
}
static size_t changed(const Frame&a,const Frame&b) {
 size_t n=0;
 if(a.size()!=b.size())return 0;
 for(size_t i=0;i<a.size();i+=4)
   if(abs(int(a[i])-int(b[i]))>8||abs(int(a[i+1])-int(b[i+1]))>8||
      abs(int(a[i+2])-int(b[i+2]))>8||abs(int(a[i+3])-int(b[i+3]))>8)n++;
 return n;
}
static size_t visible(const Frame&a) {
 size_t n=0;
 for(size_t i=3;i<a.size();i+=4)if(a[i]>18)n++;
 return n;
}
int main(int argc,char**argv) {
 if(argc!=3)return 2;
 if(!obs_startup("en-US",nullptr,nullptr))return 3;
 obs_audio_info audio={};audio.samples_per_sec=48000;audio.speakers=SPEAKERS_STEREO;
 if(!obs_reset_audio(&audio)){obs_shutdown();return 4;}
 obs_video_info video={};video.graphics_module="libobs-opengl";video.fps_num=30;video.fps_den=1;
 video.base_width=W;video.base_height=H;video.output_width=W;video.output_height=H;
 video.output_format=VIDEO_FORMAT_RGBA;video.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&video)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 5;}
 obs_register_source(&fixture_info);
 obs_module_t* module=nullptr;
 if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module)){
   obs_shutdown();return 6;
 }
 auto* input=obs_source_create("hyper_visualizer_pcm_fixture","Visualizer Test Tone",nullptr,nullptr);
 const char* typeId=nullptr;
 int hyperTypes=0;
 for(size_t i=0;obs_enum_source_types(i,&typeId);++i)
   if(typeId&&strncmp(typeId,"hypershader_",12)==0)++hyperTypes;
 auto* initial=obs_data_create();
 obs_data_set_string(initial,"shader_file","shaders/root/source.audio_spectrum_orbit.effect");
 auto* viz=obs_source_create("hypershader_source","Audio Preset",initial,nullptr);
 obs_data_release(initial);
 if(!input||!viz) {
   fprintf(stderr,"AUDIO_VIS_SOURCE_MISSING input=%d viz=%d\n",input!=nullptr,viz!=nullptr);
   if(input)obs_source_release(input);
   if(viz)obs_source_release(viz);
   obs_shutdown();return 7;
 }
 auto* settings=obs_source_get_settings(viz);
 const char* preset=obs_data_get_string(settings,"shader_file");
 bool defaults=hyperTypes==3 && std::string(preset)=="shaders/root/source.audio_spectrum_orbit.effect" &&
    obs_data_get_int(settings,"input_mode")==2 &&
    obs_data_get_int(settings,"channel0_type")==0 &&
    obs_data_get_bool(settings,"enable_sound");
 printf("AUDIO_PRESET defaults=%d registered_types=%d\n",defaults,hyperTypes);
 obs_data_set_int(settings,"source_width",W);
 obs_data_set_int(settings,"source_height",H);
 obs_data_set_string(settings,"audio_source","Visualizer Test Tone");
 obs_data_set_int(settings,"input_mode",0);
 obs_data_set_double(settings,"speed",0.0);
 obs_data_set_double(settings,"param_gain",2.2);
 obs_data_set_double(settings,"param_ring_radius",0.27);
 obs_data_set_double(settings,"param_bar_length",0.15);
 obs_data_set_double(settings,"param_bar_fill",0.68);
 obs_data_set_double(settings,"param_spoke_count",48.0);
 obs_data_set_double(settings,"param_glow",0.36);
 obs_data_set_double(settings,"param_opacity",1.0);
 obs_data_set_int(settings,"param_low_color",0xFF3078E3);
 obs_data_set_int(settings,"param_high_color",0xFFB6D0AD);
 obs_source_update(viz,settings);
 obs_data_release(settings);
 obs_source_inc_showing(viz);obs_source_inc_showing(input);
 obs_set_output_source(0,viz);obs_set_output_source(1,input);
 double phase=0;
 auto settle=[&](int frames,bool tone) {
   Frame output;
   for(int i=0;i<frames;i++) {
     std::array<float,1024> left{},right{};
     for(size_t j=0;j<left.size();j++) {
       float s=tone?0.75f*std::sin(phase):0.0f;
       left[j]=s;right[j]=s;
       phase+=6.283185307179586*440.0/48000.0;
       if(phase>6.283185307179586)phase-=6.283185307179586;
     }
     obs_source_audio pcm={};
     pcm.frames=left.size();pcm.samples_per_sec=48000;pcm.speakers=SPEAKERS_STEREO;
     pcm.format=AUDIO_FORMAT_FLOAT_PLANAR;pcm.timestamp=os_gettime_ns();
     pcm.data[0]=(const uint8_t*)left.data();pcm.data[1]=(const uint8_t*)right.data();
     obs_source_output_audio(input,&pcm);
     output=render(viz);
     std::this_thread::sleep_for(24ms);
   }
   return output;
 };
 settle(50,false);
 // Shader loading is asynchronous, and an ONNX-enabled instance can take
 // longer to initialize than the fixed PCM warmup. Require observable shader
 // controls before sampling silence; retain a bounded startup deadline.
 bool controls=false;
 int readinessChecks=0;
 for (; readinessChecks<160; ++readinessChecks) {
   auto* sourceProps=obs_source_properties(viz);
   auto* audioProp=sourceProps?obs_properties_get(sourceProps,"audio_source"):nullptr;
   auto* gainProp=sourceProps?obs_properties_get(sourceProps,"param_gain"):nullptr;
   controls=audioProp&&gainProp&&obs_property_visible(audioProp)&&obs_property_visible(gainProp);
   if(sourceProps)obs_properties_destroy(sourceProps);
   if(controls)break;
   render(viz);
   std::this_thread::sleep_for(25ms);
 }
 printf("AUDIO_UI controls_ready=%d readiness_checks=%d\n",controls,readinessChecks);
 auto quiet=settle(35,false);
 auto sounding=settle(70,true);
 auto soundingAgain=settle(8,true);
 // The OBS-facing parameter must affect the real rendered pixels.
 auto* lowerGain=obs_source_get_settings(viz);
 obs_data_set_double(lowerGain,"param_gain",0.1);
 obs_source_update(viz,lowerGain);
 obs_data_release(lowerGain);
 auto subdued=settle(18,true);
 size_t gainResponse=changed(sounding,subdued);
 size_t response=changed(quiet,sounding);
 size_t staticDrift=changed(sounding,soundingAgain);
 size_t active=visible(sounding);
 // Validate actual offscreen RGBA from native libobs GPU rendering.
 bool transparent=sounding[3]<8 && sounding[(W-1)*4+3]<8;
 bool ok=defaults&&controls&&active>80&&active<W*H/2&&response>130&&response>staticDrift*2&&transparent&&gainResponse>130;
 printf("AUDIO_VIS source_defaults=%d controls=%d active_pixels=%zu tone_response=%zu gain_response=%zu tone_drift=%zu transparent=%d PASS=%d\n",
  defaults,controls,active,response,gainResponse,staticDrift,transparent,ok);
 // Switch presets on the same live source, keeping explicit choices intact.
 auto* switching=obs_source_get_settings(viz);
 obs_data_set_string(switching,"audio_source","");
 obs_data_unset_user_value(switching,"input_mode");
 obs_data_unset_user_value(switching,"enable_sound");
 obs_data_unset_user_value(switching,"channel0_type");
 obs_data_set_string(switching,"shader_file","shaders/root/source.phosphor_scope.effect");
 auto checkDefaults=[&](bool sound,int mode,int texture) {
   obs_source_update(viz,switching);
   for(int i=0;i<40;i++) {
     render(viz);
     if(obs_data_get_bool(switching,"enable_sound")==sound &&
        obs_data_get_int(switching,"input_mode")==mode &&
        obs_data_get_int(switching,"channel0_type")==texture)
       return true;
     std::this_thread::sleep_for(25ms);
   }
   return false;
 };
 bool switchAudio=checkDefaults(true,2,3);
 obs_data_set_bool(switching,"enable_sound",false);
 obs_data_set_int(switching,"channel0_type",1);
 bool explicitOverride=checkDefaults(false,0,1);
 obs_data_unset_user_value(switching,"enable_sound");
 obs_data_unset_user_value(switching,"channel0_type");
 obs_data_set_string(switching,"shader_file","shaders/root/template.effect");
 bool switchAway=checkDefaults(false,0,0);
 obs_data_release(switching);
 printf("AUDIO_PRESET_SWITCH audio=%d explicit_override=%d away=%d\n",
        switchAudio,explicitOverride,switchAway);
 ok=ok&&switchAudio&&explicitOverride&&switchAway;
 // Disconnect the selected PCM callback while both sources are still alive.
 auto* cleanup=obs_source_get_settings(viz);
 obs_data_set_string(cleanup,"audio_source","");
 obs_data_set_bool(cleanup,"enable_sound",false);
 obs_data_set_int(cleanup,"input_mode",0);
 obs_source_update(viz,cleanup);
 obs_data_release(cleanup);
 for(int i=0;i<8;i++){render(viz);std::this_thread::sleep_for(25ms);}
 obs_set_output_source(1,nullptr);obs_set_output_source(0,nullptr);
 obs_source_dec_showing(viz);obs_source_dec_showing(input);
 obs_source_remove(viz);obs_source_remove(input);
 obs_source_release(viz);obs_source_release(input);
 std::this_thread::sleep_for(80ms);
 obs_shutdown();
 return ok?0:1;
}
