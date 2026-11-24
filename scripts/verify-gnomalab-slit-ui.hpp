#pragma once
#include <obs.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
struct SlitUiSpec {
  const char* name; const char* label; const char* group;
  enum obs_property_type type;
  double min,max,step,defaultValue;
  unsigned listCount;
};
static bool verify_slit_ui(obs_source_t* filter) {
  const std::vector<SlitUiSpec> expected={
    {"mapMode","Map Source","Map",OBS_PROPERTY_LIST,0,3,1,1,4},
    {"mapInfluence","Map Influence","Map",OBS_PROPERTY_FLOAT,-1,2,.01,.2,0},
    {"mapSmoothing","Map Curve Smoothing","Map",OBS_PROPERTY_FLOAT,0,3,.05,.5,0},
    {"mapBlur","Map Luma Blur","Map",OBS_PROPERTY_FLOAT,0,.02,.001,.005,0},
    {"mapPhase","Map Phase","Map",OBS_PROPERTY_FLOAT,0,1,.01,0,0},
    {"scanDir","Scan Direction","Scan",OBS_PROPERTY_LIST,0,3,1,1,4},
    {"slitPos","Base Slit Position","Scan",OBS_PROPERTY_FLOAT,0,1,.01,.5,0},
    {"speed","Scan Speed","Scan",OBS_PROPERTY_FLOAT,0,.01,.0001,.002,0},
    {"rgbSplit","RGB Temporal Split","Scan",OBS_PROPERTY_FLOAT,0,.02,.0005,.002,0},
    {"feedbackAmount","Ghosting","Feedback",OBS_PROPERTY_FLOAT,0,.95,.01,0,0},
    {"hueShift","Feedback Hue Shift","Feedback",OBS_PROPERTY_FLOAT,-5,5,.05,0,0},
    {"cleanBuffer","Clear Buffer","Feedback",OBS_PROPERTY_BOOL,0,1,1,0,0},
    {"effect_mix","Effect Mix","Output",OBS_PROPERTY_FLOAT,0,1,.01,1,0}
  };
  obs_properties_t* properties=obs_source_properties(filter);
  obs_data_t* data=obs_source_get_settings(filter);
  obs_property_t* outer=properties?obs_properties_get(properties,"shader_params_group"):nullptr;
  obs_properties_t* groups=outer?obs_property_group_content(outer):nullptr;
  bool ok=bool(groups && data);
  size_t found=0;
  if(groups){
    for(obs_property_t* group=obs_properties_first(groups);group;obs_property_next(&group)){
      auto* children=obs_property_get_type(group)==OBS_PROPERTY_GROUP ? obs_property_group_content(group):nullptr;
      if(!children)continue;
      for(auto* child=obs_properties_first(children);child;obs_property_next(&child)){
        if(strncmp(obs_property_name(child),"param_",6)==0)found++;
      }
    }
  }
  ok &= found==expected.size();
  for(const auto& s:expected) {
    std::string groupId=std::string("group_")+s.group;
    auto* group=groups?obs_properties_get(groups,groupId.c_str()):nullptr;
    auto* children=group?obs_property_group_content(group):nullptr;
    std::string name=std::string("param_")+s.name;
    auto* p=children?obs_properties_get(children,name.c_str()):nullptr;
    if(!p){std::cerr<<"SLIT_UI_MISSING "<<name<<"\n";ok=false;continue;}
    bool good=obs_property_get_type(p)==s.type && obs_property_visible(p) &&
      strcmp(obs_property_description(p),s.label)==0;
    if(s.type==OBS_PROPERTY_FLOAT){
      good &= std::abs(obs_property_float_min(p)-s.min)<.0001 &&
       std::abs(obs_property_float_max(p)-s.max)<.0001 &&
       std::abs(obs_property_float_step(p)-s.step)<.0001;
    }
    if(s.type==OBS_PROPERTY_LIST){
      good &= obs_property_list_item_count(p)==s.listCount;
      for(unsigned i=0;i<s.listCount;i++)good &= obs_property_list_item_int(p,i)==i;
    }
    const double actual=s.type==OBS_PROPERTY_BOOL?obs_data_get_bool(data,name.c_str()):
      s.type==OBS_PROPERTY_LIST?obs_data_get_int(data,name.c_str()):
      obs_data_get_double(data,name.c_str());
    good &= std::abs(actual-s.defaultValue)<.0001;
    if(!good)std::cerr<<"SLIT_UI_WRONG "<<name<<" default="<<actual<<"\n";
    ok &= good;
  }
  if(data)obs_data_release(data);
  if(properties)obs_properties_destroy(properties);
  std::cout<<"SLIT_UI_SETTINGS count="<<found<<" expected="<<expected.size()<<" PASS="<<ok<<"\n";
  return ok;
}
