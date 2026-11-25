#include "ui/shader_performance_panel.hpp"
#include <QApplication>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <util/platform.h>
#include <array>
#include <cmath>
#include <thread>
#include <chrono>
#include <mutex>
#include <vector>
#include <cassert>
#include <iostream>
using namespace std::chrono_literals;
static std::mutex mutex;
static std::vector<std::pair<obs_task_t,void*>> tasks;
static void queue(obs_task_t task,void*arg,bool wait){if(wait)std::abort();std::lock_guard<std::mutex> lock(mutex);tasks.emplace_back(task,arg);}
static void drain(){std::vector<std::pair<obs_task_t,void*>> q;{std::lock_guard<std::mutex> lock(mutex);q.swap(tasks);}for(auto&t:q)t.first(t.second);QApplication::processEvents();}
static void render(obs_source_t*s){
 obs_enter_graphics();auto*t=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
 if(gs_texrender_begin(t,128,128)){gs_projection_push();gs_matrix_push();gs_matrix_identity();gs_ortho(0,128,0,128,-100,100);obs_source_video_render(s);gs_matrix_pop();gs_projection_pop();gs_texrender_end(t);}
 gs_texrender_destroy(t);obs_leave_graphics();
}
static int red(obs_source_t*s){
 int value=-1;obs_enter_graphics();auto*t=gs_texrender_create(GS_RGBA,GS_ZS_NONE);auto*stage=gs_stagesurface_create(128,128,GS_RGBA);
 if(gs_texrender_begin(t,128,128)){struct vec4 clear{};gs_clear(GS_CLEAR_COLOR,&clear,0,0);
 gs_projection_push();gs_matrix_push();gs_matrix_identity();gs_ortho(0,128,0,128,-100,100);obs_source_video_render(s);gs_matrix_pop();gs_projection_pop();gs_texrender_end(t);
 gs_stage_texture(stage,gs_texrender_get_texture(t));uint8_t*p=nullptr;uint32_t pitch=0;if(gs_stagesurface_map(stage,&p,&pitch)){value=p[64*pitch+64*4];gs_stagesurface_unmap(stage);}}
 gs_stagesurface_destroy(stage);gs_texrender_destroy(t);obs_leave_graphics();return value;
}
static const char* audioName(void*){return "Performance PCM fixture";}
static void* audioCreate(obs_data_t*,obs_source_t*){return new int(0);}
static void audioDestroy(void*p){delete static_cast<int*>(p);}
static double get(obs_source_t*s,const char*k){auto*d=obs_source_get_settings(s);double v=obs_data_get_double(d,k);obs_data_release(d);return v;}
static QString text(obs_source_t*s,const char*k){auto*d=obs_source_get_settings(s);QString v=QString::fromUtf8(obs_data_get_string(d,k));obs_data_release(d);return v;}
int main(int argc,char**argv){
 if(argc!=3)return 2;QApplication app(argc,argv);
 if(!obs_startup("en-US",nullptr,nullptr))return 3;
 obs_set_ui_task_handler(queue);
 obs_audio_info audioInfo{};audioInfo.samples_per_sec=48000;audioInfo.speakers=SPEAKERS_STEREO;
 if(!obs_reset_audio(&audioInfo))return 9;
 obs_source_info audioSource{};audioSource.id="hs_performance_pcm";audioSource.type=OBS_SOURCE_TYPE_INPUT;
 audioSource.output_flags=OBS_SOURCE_AUDIO;audioSource.get_name=audioName;audioSource.create=audioCreate;audioSource.destroy=audioDestroy;
 obs_register_source(&audioSource);
 obs_video_info v{};v.graphics_module="libobs-opengl";v.fps_num=30;v.fps_den=1;v.base_width=v.output_width=128;v.base_height=v.output_height=128;v.output_format=VIDEO_FORMAT_RGBA;v.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&v)!=OBS_VIDEO_SUCCESS)return 4;
 obs_module_t*m=nullptr;if(obs_open_module(&m,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(m))return 5;
 auto make=[&](const char*name,const char*file){auto*d=obs_data_create();obs_data_set_string(d,"shader_file",file);obs_data_set_int(d,"source_width",128);obs_data_set_int(d,"source_height",128);auto*s=obs_source_create("hypershader_source",name,d,nullptr);obs_data_release(d);obs_source_inc_showing(s);return s;};
 auto*a=make("Test A","shaders/root/source.audio_spectrum_orbit.effect");
 auto*b=make("Test B","shaders/root/source.spectral_garden.effect");
 if(!a||!b)return 6;
 QString target=QString::fromUtf8(obs_source_get_uuid(a));bool ok=true;
 {
 ShaderPerformancePanel panel([&]{return target;});panel.resize(300,650);panel.show();
 auto settle=[&]{for(int i=0;i<60;i++){drain();render(a);render(b);std::this_thread::sleep_for(15ms);}panel.refresh(true);drain();};
 settle();
 auto*status=panel.findChild<QLabel*>("hsPerformanceStatus");
 auto*gain=panel.findChild<QWidget*>("hsLive_param_gain");
 auto*audio=panel.findChild<QWidget*>("hsLive_audio_source");
 bool controls=status&&status->text().contains("Showing")&&gain&&audio;
 auto before=get(a,"param_gain"),other=get(b,"param_audio_gain");
 auto*setA=panel.findChild<QPushButton*>("hsPerformanceCaptureA");
 auto*setB=panel.findChild<QPushButton*>("hsPerformanceCaptureB");
 auto*random=panel.findChild<QPushButton*>("hsPerformanceVary");
 auto*undo=panel.findChild<QPushButton*>("hsPerformanceUndo");
 auto*morph=panel.findChild<QSlider*>("hsPerformanceMorph");
 bool buttons=setA&&setB&&random&&undo&&morph&&random->isEnabled();
 if(!buttons||!controls){std::cerr<<"PANEL controls="<<controls<<" buttons="<<buttons<<" status="<<(status?status->text().toStdString():"missing")<<std::endl;return 7;}
 auto*spin=gain->findChild<QDoubleSpinBox*>();auto*lock=gain->findChild<QCheckBox*>();
 bool lockExists=spin&&lock;if(!lockExists)return 8;
 setA->click();lock->setChecked(true);
 random->click();drain();bool locked=get(a,"param_gain")==before;
 gain=panel.findChild<QWidget*>("hsLive_param_gain");spin=gain->findChild<QDoubleSpinBox*>();lock=gain->findChild<QCheckBox*>();lock->setChecked(false);
 double end=std::min(spin->maximum(),before+1.0);if(end==before)end=spin->minimum();
 spin->setValue(end);QMetaObject::invokeMethod(spin,"editingFinished");drain();
 bool edit=std::abs(get(a,"param_gain")-end)<.001;
 setB->click();morph->setValue(50);drain();bool middle=std::abs(get(a,"param_gain")-(before+end)*.5)<.001;
 undo->click();drain();bool undoWorks=std::abs(get(a,"param_gain")-end)<.001;
 auto*modParam=panel.findChild<QComboBox*>("hsModParameter");auto*modDriver=panel.findChild<QComboBox*>("hsModDriver");
 auto*modBind=panel.findChild<QPushButton*>("hsModBind");
 bool modUi=modParam&&modDriver&&modBind;
 if(modUi){modParam->setCurrentIndex(modParam->findData("param_gain"));modDriver->setCurrentIndex(1);modBind->click();drain();
  modUi=text(a,"perf_mod_param")=="param_gain"&&get(a,"perf_mod_source")==1;
  auto*clear=panel.findChild<QPushButton*>("hsModClear");if(clear){clear->click();drain();modUi&=get(a,"perf_mod_source")==0;}else modUi=false;
 }
 auto*freeze=panel.findChild<QPushButton*>("hsPerformanceFreeze");freeze->click();bool paused=get(a,"speed")==0;freeze->click();bool resumed=get(a,"speed")>0;
 auto*bypass=panel.findChild<QPushButton*>("hsPerformanceBypass");bypass->click();bool disabled=!obs_source_enabled(a);bypass->click();bool enabled=obs_source_enabled(a);
 calldata_t cd;calldata_init(&cd);bool telemetry=proc_handler_call(obs_source_get_proc_handler(a),"hypershader_live_status",&cd)&&calldata_bool(&cd,"rendering");calldata_free(&cd);
 target=QString::fromUtf8(obs_source_get_uuid(b));panel.refresh(true);drain();
 bool switchWorks=!morph->isEnabled()&&get(b,"param_audio_gain")==other&&panel.findChild<QWidget*>("hsLive_param_audio_gain");
 // Actual plugin draw: modulated float must differ from saved base; stale
 // PCM and wrong shader identity must preserve the unmodulated value.
 auto*c=make("Modulation fixture","shaders/tests/performance_modulation_probe.effect");
 auto configure=[&](int mode,const char*path){auto*s=obs_source_get_settings(c);
 obs_data_set_double(s,"speed",0);obs_data_set_double(s,"param_amount",.25);
 obs_data_set_string(s,"perf_mod_shader",path);obs_data_set_string(s,"perf_mod_param","param_amount");
 obs_data_set_int(s,"perf_mod_source",mode);obs_data_set_double(s,"perf_mod_depth",.5);obs_data_set_double(s,"perf_mod_hz",1);
 obs_source_update(c,s);obs_data_release(s);};
 configure(0,"shaders/tests/performance_modulation_probe.effect");
 for(int i=0;i<65;i++){drain();render(c);std::this_thread::sleep_for(15ms);}
 int baseline=red(c);
 configure(1,"shaders/tests/performance_modulation_probe.effect");int lfo=red(c);
 configure(2,"shaders/tests/performance_modulation_probe.effect");int absentAudio=red(c);
 auto*props=obs_source_properties(c);auto*ap=obs_properties_get(props,"audio_source");bool modPicker=ap&&obs_property_visible(ap);obs_properties_destroy(props);
 auto*pcm=obs_source_create("hs_performance_pcm","Performance tone",nullptr,nullptr);
 if(!pcm)return 10;
 obs_source_inc_showing(pcm);obs_set_output_source(1,pcm);
 auto*cfg=obs_source_get_settings(c);
 obs_data_set_string(cfg,"audio_source","Performance tone");obs_data_set_bool(cfg,"enable_sound",true);
 obs_data_set_double(cfg,"perf_mod_depth",1);obs_source_update(c,cfg);obs_data_release(cfg);
 double phase=0;int pcmPeak=0;
 for(int i=0;i<100;i++){
  std::array<float,1024> samples{};
  for(float&x:samples){x=.85f*std::sin(phase);phase+=6.28318530718*440./48000.;}
  obs_source_audio pcmData{};pcmData.frames=samples.size();pcmData.samples_per_sec=48000;
  pcmData.speakers=SPEAKERS_STEREO;pcmData.format=AUDIO_FORMAT_FLOAT_PLANAR;pcmData.timestamp=os_gettime_ns();
  pcmData.data[0]=pcmData.data[1]=reinterpret_cast<const uint8_t*>(samples.data());
  obs_source_output_audio(pcm,&pcmData);drain();pcmPeak=std::max(pcmPeak,red(c));std::this_thread::sleep_for(22ms);
 }
 calldata_t live;calldata_init(&live);bool pcmLive=proc_handler_call(obs_source_get_proc_handler(c),"hypershader_live_status",&live)&&calldata_bool(&live,"audio");calldata_free(&live);
 for(int i=0;i<30;i++){drain();render(c);std::this_thread::sleep_for(18ms);}
 int stale=red(c);
 cfg=obs_source_get_settings(c);obs_data_set_bool(cfg,"enable_sound",false);obs_data_set_string(cfg,"audio_source","");obs_source_update(c,cfg);obs_data_release(cfg);
 obs_set_output_source(1,nullptr);obs_source_dec_showing(pcm);obs_source_remove(pcm);obs_source_release(pcm);
 configure(1,"shaders/root/other.effect");int wrong=red(c);
 bool pcmWorks=pcmPeak>baseline+8&&pcmLive&&std::abs(stale-baseline)<3;
 std::cout<<"PERFORMANCE_PCM peak="<<pcmPeak<<" baseline="<<baseline<<" stale="<<stale<<" fresh="<<pcmLive<<" PASS="<<pcmWorks<<std::endl;
 bool modulated=pcmWorks&&baseline>55&&baseline<75&&lfo>baseline+80&&std::abs(absentAudio-baseline)<3&&std::abs(wrong-baseline)<3&&get(c,"param_amount")==.25&&modPicker;
 std::cout<<"PERFORMANCE_MODULATION base="<<baseline<<" lfo="<<lfo<<" missing_audio="<<absentAudio<<" wrong_shader="<<wrong<<" audio_picker="<<modPicker<<" PASS="<<modulated<<std::endl;
 obs_source_dec_showing(c);obs_source_remove(c);obs_source_release(c);
 ok=modulated&&modUi&&controls&&buttons&&locked&&edit&&middle&&undoWorks&&paused&&resumed&&disabled&&enabled&&telemetry&&switchWorks;
 std::cout<<"PERFORMANCE_PANEL controls="<<controls<<" locked="<<locked<<" edit="<<edit<<" morph="<<middle<<" undo="<<undoWorks<<" pause="<<paused<<" resume="<<resumed<<" bypass="<<(disabled&&enabled)<<" telemetry="<<telemetry<<" mod_ui="<<modUi<<" target_switch="<<switchWorks<<" PASS="<<ok<<std::endl;
 }
 obs_source_dec_showing(a);obs_source_dec_showing(b);obs_source_remove(a);obs_source_remove(b);obs_source_release(a);obs_source_release(b);drain();obs_wait_for_destroy_queue();drain();obs_shutdown();return ok?0:1;
}
