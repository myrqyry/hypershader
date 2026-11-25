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
static double get(obs_source_t*s,const char*k){auto*d=obs_source_get_settings(s);double v=obs_data_get_double(d,k);obs_data_release(d);return v;}
static QString text(obs_source_t*s,const char*k){auto*d=obs_source_get_settings(s);QString v=QString::fromUtf8(obs_data_get_string(d,k));obs_data_release(d);return v;}
int main(int argc,char**argv){
 if(argc!=6)return 2;QApplication app(argc,argv);
 if(!obs_startup("en-US",nullptr,nullptr))return 3;
 obs_set_ui_task_handler(queue);
 obs_audio_info audio{};audio.samples_per_sec=48000;audio.speakers=SPEAKERS_STEREO;
 if(!obs_reset_audio(&audio))return 4;
 obs_video_info v{};v.graphics_module="libobs-opengl";v.fps_num=30;v.fps_den=1;v.base_width=v.output_width=128;v.base_height=v.output_height=128;v.output_format=VIDEO_FORMAT_RGBA;v.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&v)!=OBS_VIDEO_SUCCESS)return 5;
 obs_module_t *providerModule=nullptr,*shaderModule=nullptr;
 if(obs_open_module(&providerModule,argv[3],argv[4])!=MODULE_SUCCESS||!obs_init_module(providerModule))return 6;
 if(obs_open_module(&shaderModule,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(shaderModule))return 7;
 const std::string base=argv[5];
 auto providerSettings=[&](const char*path,bool share){
  auto*s=obs_data_create();
  std::string json=std::string(R"({"url":")")+base+path+R"(","url_or_file":"url","method":"GET","body":"","inputs":[],"headers":{},"output_type":"JSON","output_json_pointer":"/value","output_xpath":"","output_xquery":"","output_regex":"","output_regex_flags":"","output_regex_group":"0","fail_on_http_error":true})";
  obs_data_set_string(s,"request_data",json.c_str());obs_data_set_string(s,"output_mapping_data","[]");
  obs_data_set_bool(s,"share_parsed_data",share);obs_data_set_bool(s,"run_while_not_visible",true);
  obs_data_set_int(s,"update_timer",10000);return s;
 };
 auto*d=providerSettings("/high",true);auto*provider=obs_source_create("url_source","Test provider",d,nullptr);obs_data_release(d);
 if(!provider)return 8;
 auto update=[&](const char*path,bool share){auto*s=providerSettings(path,share);obs_source_update(provider,s);obs_data_release(s);};
 const char*path="shaders/tests/data_binding_probe.effect";
 d=obs_data_create();obs_data_set_string(d,"shader_file",path);obs_data_set_int(d,"source_width",128);obs_data_set_int(d,"source_height",128);obs_data_set_double(d,"param_amount",.25);
 auto*shader=obs_source_create("hypershader_source","Data bound shader",d,nullptr);obs_data_release(d);
 if(!shader)return 9;obs_source_inc_showing(shader);
 auto settle=[&](int count){for(int i=0;i<count;i++){drain();render(shader);std::this_thread::sleep_for(20ms);}};
 bool ok=true;
 auto check=[&](const char*name,bool passed){std::cout<<"DATA_BINDING "<<name<<"="<<passed<<std::endl;ok&=passed;};
 {
 ShaderPerformancePanel panel([&]{return QString::fromUtf8(obs_source_get_uuid(shader));});panel.resize(420,750);panel.show();
 settle(65);panel.refresh(true);drain();
 int baseline=red(shader);check("baseline",baseline>=62&&baseline<=66);
 auto*param=panel.findChild<QComboBox*>("hsModParameter");auto*driver=panel.findChild<QComboBox*>("hsModDriver");
 auto*pick=panel.findChild<QComboBox*>("hsModDataSource");auto*age=panel.findChild<QSpinBox*>("hsModDataAge");
 auto*bind=panel.findChild<QPushButton*>("hsModBind");
 if(!param||!driver||!pick||!age||!bind){check("ui",false);return 10;}
 param->setCurrentIndex(param->findData("param_amount"));driver->setCurrentIndex(8);
 pick->setCurrentIndex(pick->findData(QString::fromUtf8(obs_source_get_uuid(provider))));age->setValue(5000);bind->click();drain();
 check("ui_binding",get(shader,"perf_mod_source")==8&&text(shader,"perf_mod_data_uuid")==QString::fromUtf8(obs_source_get_uuid(provider)));
 settle(35);int high=red(shader);check("live_pixel",high>=189&&high<=193);
 std::cout<<"DATA_PIXELS baseline="<<baseline<<" live="<<high<<std::endl;
 obs_source_set_name(provider,"Renamed test provider");settle(15);check("rename",red(shader)>=189&&red(shader)<=193);
 update("/over",true);settle(40);int clamped=red(shader);check("clamp",clamped>=253);
 std::cout<<"DATA_PIXELS clamped="<<clamped<<std::endl;
 update("/string",true);settle(40);check("wrong_type",std::abs(red(shader)-baseline)<=2);
 update("/high",true);settle(35);check("recover",red(shader)>=189&&red(shader)<=193);
 update("/high",false);settle(35);check("opt_out",std::abs(red(shader)-baseline)<=2);
 update("/bool",true);settle(35);check("bool",red(shader)>=253);
 d=obs_source_get_settings(shader);obs_data_set_int(d,"perf_mod_data_age_ms",100);obs_source_update(shader,d);obs_data_release(d);
 settle(25);int stale=red(shader);check("stale",std::abs(stale-baseline)<=2);
 std::cout<<"DATA_PIXELS stale="<<stale<<std::endl;
 d=obs_source_get_settings(shader);obs_data_set_int(d,"perf_mod_data_age_ms",5000);obs_source_update(shader,d);obs_data_release(d);
 update("/high",true);settle(35);check("fresh_again",red(shader)>=189&&red(shader)<=193);
 obs_source_remove(provider);settle(30);check("removed",std::abs(red(shader)-baseline)<=2);
 check("base_unchanged",get(shader,"param_amount")==.25);
 panel.refresh(true);drain();auto*clear=panel.findChild<QPushButton*>("hsModClear");
 if(clear){clear->click();drain();}check("clear",clear&&get(shader,"perf_mod_source")==0);
 }
 obs_source_release(provider);obs_source_dec_showing(shader);obs_source_remove(shader);obs_source_release(shader);drain();obs_wait_for_destroy_queue();drain();obs_shutdown();
 return ok?0:1;
}
