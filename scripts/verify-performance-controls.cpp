#include "ui/shader_performance_model.hpp"
#include <cassert>
#include <iostream>
int main(){
 auto*p=obs_properties_create();auto*s=obs_data_create();
 auto*g=obs_properties_create();
 obs_properties_add_float_slider(g,"param_amount","Amount",0,2,.1);
 obs_properties_add_float_slider(g,"param_locked","Locked",0,1,.01);
 obs_properties_add_float(g,"param_unbounded","Unbounded",-1000,1000,.05);
 auto*hidden=obs_properties_add_float_slider(g,"param_hidden","Hidden",0,1,.1);
 obs_property_set_visible(hidden,false);
 obs_properties_add_bool(g,"param_mode","Mode");
 obs_properties_add_group(p,"shader_params_group","Shader",OBS_GROUP_NORMAL,g);
 obs_properties_add_float_slider(p,"speed","Speed",0,10,.1);
 obs_properties_add_text(p,"audio_source","Audio",OBS_TEXT_DEFAULT);
 obs_data_set_double(s,"param_amount",.5);obs_data_set_double(s,"param_locked",.4);
 obs_data_set_double(s,"param_hidden",.2);obs_data_set_double(s,"speed",1);
 obs_data_set_string(s,"audio_source","my source");
 auto fields=hsPerformanceFields(p);auto a=hsCaptureLook(fields,s);
 assert(a.size()==2&&a.contains("param_amount")&&a.contains("param_locked"));
 auto b=hsVaryLook(fields,a,{"param_locked"},1.0,42);
 assert(b["param_locked"]==.4&&b["param_amount"]>=0&&b["param_amount"]<=2);
 assert(b==hsVaryLook(fields,a,{"param_locked"},1.0,42));
 assert(hsVaryLook(fields,a,{},0,42)==a);
 b["param_amount"]=1.5;
 auto mid=hsMorphLook(fields,a,b,.5);
 assert(mid["param_amount"]==1.0&&mid["param_locked"]==.4);
 auto invalid=b;invalid["speed"]=7;invalid["param_hidden"]=1;
 hsWriteLook(fields,s,invalid);
 assert(obs_data_get_double(s,"speed")==1&&obs_data_get_double(s,"param_hidden")==.2);
 assert(std::string(obs_data_get_string(s,"audio_source"))=="my source");
 assert(obs_data_get_double(s,"param_amount")==1.5);
 obs_properties_destroy(p);obs_data_release(s);
 std::cout<<"PERFORMANCE_MODEL_PASS=1\n";
}
