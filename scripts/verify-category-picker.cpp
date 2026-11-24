// Native libobs category picker integration, including saved selection.
#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <iostream>
#include <string>
#include <set>
#include <filesystem>
#include "shader/shader_display_name.h"
using namespace std;
static bool check(bool ok,const string&name) {
 if(!ok)cerr<<"FAIL "<<name<<"\n";
 return ok;
}
static bool countPicker(obs_properties_t*p, obs_data_t*settings,
                        const char*category,size_t expected){
 auto* picker=obs_properties_get(p,"preset");
 const size_t n=picker?obs_property_list_item_count(picker):0;
 bool pass=n==expected+2 &&
      string(obs_data_get_string(settings,"shader_category"))==category;
 if(n>=2){
   pass &= obs_property_list_item_int(picker,0)==-1;
   pass &= string(obs_property_list_item_name(picker,n-1))=="Custom File...";
 }
 set<long long> ids;
 for(size_t i=1;i+1<n;i++)
   pass &= ids.insert(obs_property_list_item_int(picker,i)).second;
 cout<<"CATEGORY_PICKER "<<category<<" effects="<<(n>=2?n-2:0)
     <<" expected="<<expected<<" PASS="<<pass<<"\n";
 return check(pass,category);
}
struct CatalogCounts{
 size_t all=0,source=0,filter=0,transition=0,shabon=0,rikiya=0;
};
static CatalogCounts countActualShaders(const std::filesystem::path&data) {
 namespace fs=std::filesystem;
 CatalogCounts counts;
 const auto root=data/"shaders";
 auto visit=[&](const fs::path&dir,const string&relative) {
   if(!fs::exists(dir))return;
   for(const auto&ent:fs::directory_iterator(dir)) {
     if(!ent.is_regular_file() || ent.is_symlink() ||
        ent.path().extension()!=".effect")continue;
     string path=relative.empty()?ent.path().filename().string():
         relative+"/"+ent.path().filename().string();
     char label[512];
     if(!hs_shader_display_label_from_file(label,sizeof(label),path.c_str(),
                                            ent.path().c_str()))continue;
     string name(label);
     counts.all++;
     if(name.rfind("Source / ",0)==0)counts.source++;
     if(name.rfind("Filter / ",0)==0)counts.filter++;
     if(name.rfind("Transition / ",0)==0)counts.transition++;
     if(relative=="root/shabon")counts.shabon++;
     if(relative=="root/rikiya")counts.rikiya++;
   }
 };
 visit(root,"");
 visit(root/"root","root");
 for(const auto&dir:fs::directory_iterator(root/"root"))
   if(dir.is_directory()&&!dir.is_symlink())
     visit(dir.path(),"root/"+dir.path().filename().string());
 return counts;
}
int main(int argc,char**argv) {
 if(argc!=3)return 2;
 if(!obs_startup("en-US",nullptr,nullptr))return 3;
 obs_video_info v{};
 v.graphics_module="libobs-opengl";v.fps_num=30;v.fps_den=1;
 v.base_width=240;v.base_height=136;v.output_width=240;v.output_height=136;
 v.output_format=VIDEO_FORMAT_RGBA;v.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&v)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 4;}
 obs_module_t*module=nullptr;
 if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||
    !obs_init_module(module)){obs_shutdown();return 5;}
 bool good=true;
 const auto count=countActualShaders(argv[2]);
 good &=check(count.all==count.source+count.filter+count.transition,
             "All installed shader files classified");
 good &=check(count.shabon>=20 && count.rikiya>=50,
             "Real Shabon/Rikiya installations are scanned");
 for(const char*kind:{"hypershader_filter","hypershader_source","hypershader_transition"}){
   auto*source=obs_source_create(kind,kind,nullptr,nullptr);
   if(!source){cerr<<"FAILED_CREATE "<<kind<<"\n";good=false;continue;}
   cerr<<"CREATED_SOURCE "<<kind<<"\n";
   auto*settings=obs_source_get_settings(source);
   auto*props=obs_source_properties(source);
   auto*category=props?obs_properties_get(props,"shader_category"):nullptr;
   auto*preset=props?obs_properties_get(props,"preset"):nullptr;
   cerr<<"PROPERTIES "<<kind<<" "<<props<<" "<<category<<" "<<preset<<"\n";
   good &=check(category&&preset,"Native linked controls");
   if(!category||!preset){
     if(props)obs_properties_destroy(props);
     obs_data_release(settings);obs_source_release(source);continue;
   }
   bool hasShabon=false;
   for(size_t i=0;i<obs_property_list_item_count(category);i++)
     if(string(obs_property_list_item_name(category,i))=="Shabon FX")hasShabon=true;
   good &=check(hasShabon,"Shabon collection category");
   if(string(kind)=="hypershader_source")
      good &=countPicker(props,settings,"source",count.source);
   else if(string(kind)=="hypershader_filter")
      good &=countPicker(props,settings,"filter",count.filter);
   else good &=countPicker(props,settings,"transition",count.transition);
   if(string(kind)=="hypershader_filter"){
     obs_data_set_string(settings,"shader_category","collection:shabon");
     good &=check(obs_property_modified(category,settings),"Shabon category callback");
     good &=countPicker(props,settings,"collection:shabon",count.shabon);
     long long auroraId=0;
     for(size_t i=1;i+1<obs_property_list_item_count(preset);i++)
       if(string(obs_property_list_item_name(preset,i))=="Aurora Waves")
         auroraId=obs_property_list_item_int(preset,i);
     good &=check(auroraId < -103,"Stable Shabon preset ID");
     obs_data_set_int(settings,"preset",auroraId);
     good &=check(obs_property_modified(preset,settings),"Selecting Aurora Waves");
     const string path=obs_data_get_string(settings,"shader_file");
     good &=check(path.find("source.shabon_aurora_waves.effect")!=string::npos,
                 "Correct path after filtered choice");
     obs_data_set_string(settings,"shader_category","collection:rikiya");
     good &=check(obs_property_modified(category,settings),"Rikiya category callback");
     good &=check(path==obs_data_get_string(settings,"shader_file"),
                 "Browsing must not silently replace selected effect");
     bool pinned=false;
     for(size_t i=1;i+1<obs_property_list_item_count(preset);i++)
       if(obs_property_list_item_int(preset,i)==auroraId)pinned=true;
     good &=check(pinned,"Current effect remains in list");
     good &=check(obs_property_list_item_count(preset)==count.rikiya+3,
                 "Rikiya effects + current + choose + custom");
     obs_data_set_string(settings,"shader_category","collection:shabon");
     good &=check(obs_property_modified(category,settings),"Return to Shabon");
     good &=countPicker(props,settings,"collection:shabon",count.shabon);
     obs_source_update(source,settings);
     auto*reopened=obs_source_properties(source);
     auto*back=reopened?obs_properties_get(reopened,"preset"):nullptr;
     good &=check(back && obs_property_list_item_count(back)==26,
                 "Selected collection on scene reopen");
     if(reopened)obs_properties_destroy(reopened);
     obs_data_set_string(settings,"shader_category","all");
     good &=check(obs_property_modified(category,settings),"All category callback");
     good &=countPicker(props,settings,"all",count.all);
   }
   if(props)obs_properties_destroy(props);
   obs_data_release(settings);
   obs_source_release(source);
 }
 obs_shutdown();
 cout<<"CATEGORY_NATIVE_INTEGRATION PASS="<<good<<"\n";
 return good?0:1;
}
