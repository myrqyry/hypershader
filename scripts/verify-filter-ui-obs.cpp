#include <obs.h>
#include <obs-module.h>
#include "../src/source-selection.h"
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string>
#include <filesystem>
#include <set>
#include <map>
#include <vector>
#include <utility>
#include <functional>
// Black-box libobs validation of the real HyperShader filter properties
// and the preset change callback. No frontend UI or live camera required.
int main(int argc,char** argv){
 if(argc!=3){std::cerr<<"usage: plugin data\n";return 2;}
 if(!obs_startup("en-US",nullptr,nullptr))return 1;
 obs_module_t* module=nullptr;
 int code=obs_open_module(&module,argv[1],argv[2]);
 if(code!=MODULE_SUCCESS||!obs_init_module(module)){
   std::cerr<<"module failed "<<code<<"\n";obs_shutdown();return 1;
 }
 // Real libobs scene hierarchy: source bindings identify sources (not scene items).
 obs_source_info fixtureInfo{};
 fixtureInfo.id="hs_picker_fixture";
 fixtureInfo.type=OBS_SOURCE_TYPE_INPUT;
 fixtureInfo.output_flags=OBS_SOURCE_VIDEO|OBS_SOURCE_AUDIO;
 fixtureInfo.get_name=[](void*) { return "Picker fixture"; };
 fixtureInfo.create=[](obs_data_t*,obs_source_t*) -> void* { return new int(1); };
 fixtureInfo.destroy=[](void* p) { delete static_cast<int*>(p); };
 fixtureInfo.get_width=[](void*) -> uint32_t { return 64; };
 fixtureInfo.get_height=[](void*) -> uint32_t { return 64; };
 obs_register_source(&fixtureInfo);
 auto* root=obs_scene_create("Picker Root");
 auto* nested=obs_scene_create("Picker Nested");
 auto* leaf=obs_source_create("hs_picker_fixture","Picker Leaf",nullptr,nullptr);
 auto* shared=obs_source_create("hs_picker_fixture","Picker Shared",nullptr,nullptr);
 auto* loose=obs_source_create("hs_picker_fixture","Picker Unplaced",nullptr,nullptr);
 auto* group=root?obs_scene_add_group(root,"Picker Group"):nullptr;
 bool pickerFixture=root && nested && leaf && shared && loose && group;
 if(pickerFixture) {
   pickerFixture=obs_scene_add(obs_sceneitem_group_get_scene(group),leaf)
     && obs_scene_add(root,obs_scene_get_source(nested))
     && obs_scene_add(nested,shared) && obs_scene_add(root,shared);
 }
 auto* pickerProps=obs_get_source_properties("hypershader_filter");
 auto* pickerGroup=pickerProps?obs_properties_get(pickerProps,"channel0_group"):nullptr;
 auto* videoPicker=pickerGroup?obs_properties_get(obs_property_group_content(pickerGroup),"channel0_source"):nullptr;
 auto* audioPicker=pickerProps?obs_properties_get(pickerProps,"audio_source"):nullptr;
 auto checkPicker=[&](obs_property_t* prop,obs_source_t* target,const std::string& labelPart) {
   if(!prop || !target)return false;
   const std::string value=std::string("hs-source:v1:")+obs_source_get_uuid(target);
   size_t count=0;
   bool path=false;
   for(size_t i=0;i<obs_property_list_item_count(prop);++i) {
     if(value==obs_property_list_item_string(prop,i)){
       ++count;
       path=std::string(obs_property_list_item_name(prop,i)).find(labelPart)!=std::string::npos;
     }
   }
   return count==1 && path;
 };
 bool sourcePickerPassed=pickerFixture &&
   checkPicker(videoPicker,leaf,"Picker Group / Picker Leaf") &&
   checkPicker(videoPicker,shared,"Picker Shared") &&
   checkPicker(videoPicker,obs_scene_get_source(nested),"Picker Nested") &&
   checkPicker(videoPicker,loose,"Picker Unplaced") &&
   checkPicker(audioPicker,leaf,"Picker Group / Picker Leaf") &&
   checkPicker(audioPicker,shared,"Picker Shared") &&
   checkPicker(audioPicker,loose,"Picker Unplaced");
 if(pickerFixture) {
   const std::string selected=std::string(HS_SOURCE_SELECTION_PREFIX)+obs_source_get_uuid(leaf);
   const std::string staleUuid=obs_source_get_uuid(shared);
   obs_source_set_name(leaf,"Picker Renamed");
   auto* resolved=hs_source_selection_resolve(selected.c_str(),staleUuid.c_str());
   sourcePickerPassed &= resolved==leaf;
   if(resolved)obs_source_release(resolved);
   auto* legacy=hs_source_selection_resolve("Picker Shared",nullptr);
   sourcePickerPassed &= legacy==shared;
   if(legacy)obs_source_release(legacy);
   auto* legacyRenamed=hs_source_selection_resolve("Picker Leaf",obs_source_get_uuid(leaf));
   sourcePickerPassed &= legacyRenamed==leaf;
   if(legacyRenamed)obs_source_release(legacyRenamed);
   auto* empty=hs_source_selection_resolve("",staleUuid.c_str());
   sourcePickerPassed &= !empty;
   if(empty)obs_source_release(empty);
   obs_source_remove(leaf);
   auto* missing=hs_source_selection_resolve(selected.c_str(),staleUuid.c_str());
   sourcePickerPassed &= !missing;
   if(missing)obs_source_release(missing);
   auto* saved=obs_data_create();
   obs_data_set_string(saved,"channel0_source",selected.c_str());
   obs_data_set_string(saved,"audio_source",selected.c_str());
   auto* filter=obs_source_create("hypershader_filter","Missing picker probe",saved,nullptr);
   obs_data_release(saved);
   auto* props=filter?obs_source_properties(filter):nullptr;
   auto* gp=props?obs_properties_get(props,"channel0_group"):nullptr;
   auto* missingVideo=gp?obs_properties_get(obs_property_group_content(gp),"channel0_source"):nullptr;
   auto* missingAudio=props?obs_properties_get(props,"audio_source"):nullptr;
   auto preserved=[&](obs_property_t* prop) {
     if(!prop)return false;
     for(size_t i=0;i<obs_property_list_item_count(prop);++i)
       if(selected==obs_property_list_item_string(prop,i))
         return std::string(obs_property_list_item_name(prop,i)).find("Unavailable")!=std::string::npos;
     return false;
   };
   sourcePickerPassed &= preserved(missingVideo) && preserved(missingAudio);
   if(props)obs_properties_destroy(props);
   if(filter)obs_source_release(filter);
 }
 std::cout<<"NESTED_SOURCE_PICKERS="<<sourcePickerPassed<<"\n";
 if(pickerProps)obs_properties_destroy(pickerProps);
 if(root){obs_source_remove(obs_scene_get_source(root));obs_scene_release(root);}
 if(nested){obs_source_remove(obs_scene_get_source(nested));obs_scene_release(nested);}
 if(leaf)obs_source_release(leaf);
 if(shared)obs_source_release(shared);
 if(loose)obs_source_release(loose);
 obs_properties_t* type=obs_get_source_properties("hypershader_filter");
 // OBS's native property labels are drawn with its Qt theme text font.
 // They must never depend on a color emoji font fallback.
 const auto isAscii=[](const char* value) {
   if(!value)return false;
   for(const unsigned char* p=(const unsigned char*)value;*p;p++)
     if(*p>=128)return false;
   return true;
 };
 std::function<bool(obs_properties_t*)> textLabels = [&](obs_properties_t* properties) {
   if(!properties)return false;
   bool ok=true;
   for(obs_property_t* p=obs_properties_first(properties);p;obs_property_next(&p)){
     const char* description=obs_property_description(p);
     if(!isAscii(description)){
       std::cerr<<"FONT_DEPENDENT_PROPERTY_LABEL "<<obs_property_name(p)
                <<" "<<(description?description:"<null>")<<"\n";
       ok=false;
     }
     if(obs_property_get_type(p)==OBS_PROPERTY_GROUP)
       ok &= textLabels(obs_property_group_content(p));
   }
   return ok;
 };
 const bool themeSafeLabels=textLabels(type);
 std::cout<<"THEME_SAFE_PROPERTY_LABELS="<<themeSafeLabels<<"\n";
 auto* chooser=type?obs_properties_get(type,"preset"):nullptr;
 auto* path=type?obs_properties_get(type,"shader_file"):nullptr;
 auto* audio=type?obs_properties_get(type,"audio_source"):nullptr;
 auto* graph=type?obs_properties_get(type,"mp_controlnet_model"):nullptr;
 int num=chooser?(int)obs_property_list_item_count(chooser):-1;
 std::cout<<"TYPE_CHOOSER_ITEMS="<<num<<"\n";
 // Actual source kind chooses the initial category before shader selection.
 auto checkKind=[&](const char* sourceId,const std::string& preferred){
   obs_properties_t* roleProps=obs_get_source_properties(sourceId);
   auto* roles=roleProps?obs_properties_get(roleProps,"shader_category"):nullptr;
   auto* rolePicker=roleProps?obs_properties_get(roleProps,"preset"):nullptr;
   size_t total=rolePicker?obs_property_list_item_count(rolePicker):0;
   bool roleOk=roles && rolePicker && total>=3 &&
      std::string(obs_property_list_item_string(roles,0))==preferred &&
      obs_property_list_item_int(rolePicker,0)==-1 &&
      obs_property_list_item_int(rolePicker,total-1)==1;
   bool found=false;
   for(size_t i=1;i+1<total;i++){
     const std::string label=obs_property_list_item_name(rolePicker,i);
     if(preferred=="source" && label=="Blurred Gradient")found=true;
     if(preferred=="filter" && label=="P-Frame Drift")found=true;
     if(preferred=="transition" && (label=="Warp Dither" || label=="CRT Power"))found=true;
   }
   roleOk &= found;
   if(preferred=="source")roleOk &= total>=30 && total<100;
   if(preferred=="filter")roleOk &= total>=80 && total<145;
   std::cout<<"ROLE_PICKER_KIND="<<sourceId<<" preferred="<<preferred
            <<" total="<<total<<" matched="<<found<<" PASS="<<roleOk<<"\n";
   if(roleProps)obs_properties_destroy(roleProps);
   return roleOk;
 };
 bool orderingPassed=checkKind("hypershader_filter","filter")
     && checkKind("hypershader_source","source")
     && checkKind("hypershader_transition","transition");
 // The All Shaders category must still expose every installed ID and file.
 auto* category=type?obs_properties_get(type,"shader_category"):nullptr;
 auto* fullSettings=obs_data_create();
 obs_data_set_string(fullSettings,"shader_category","all");
 bool allFilterWorks=category && obs_property_modified(category,fullSettings);
 obs_data_release(fullSettings);
 num=chooser?(int)obs_property_list_item_count(chooser):-1;
 orderingPassed &= allFilterWorks;
 std::cout<<"ALL_CATEGORY_ITEMS="<<num<<" callback="<<allFilterWorks<<"\n";
 for(int i=0;i<num;i++)
   std::cout<<"PRESET "<<i<<" VALUE="<<obs_property_list_item_int(chooser,i)
             <<" NAME="<<obs_property_list_item_name(chooser,i)<<"\n";
 if(path)std::cout<<"TYPE_CUSTOM_PATH_VISIBLE="<<obs_property_visible(path)<<"\n";
 if(audio)std::cout<<"TYPE_AUDIO_VISIBLE="<<obs_property_visible(audio)<<"\n";
 if(graph)std::cout<<"TYPE_MEDIAPIPE_VISIBLE="<<obs_property_visible(graph)<<"\n";
 // Every installed .effect under shaders/, root/, and one named root
 // collection (e.g. rikiya) belongs in the picker. Ignore tests/ and
 // generated platform copies. No hard-coded expected catalog length.
 namespace fs=std::filesystem;
 const fs::path shaderDir=fs::path(argv[2])/"shaders";
 auto countEffects=[](const fs::path& dir) {
   size_t result=0;
   if(!fs::is_directory(dir))return result;
   for(const auto& entry:fs::directory_iterator(dir))
     if(entry.is_regular_file() && !entry.is_symlink() && entry.path().extension()==".effect")
       ++result;
   return result;
 };
 size_t installed=countEffects(shaderDir)+countEffects(shaderDir/"root");
 for(const auto& entry:fs::directory_iterator(shaderDir/"root"))
   if(entry.is_directory() && !entry.is_symlink())
      installed+=countEffects(entry.path());
 bool passes=chooser && num==int(installed+2) &&
             orderingPassed && themeSafeLabels && sourcePickerPassed;
 if(chooser){
   bool headerSafe=std::string(obs_property_description(chooser))=="Shader Preset";
   bool selectSafe=std::string(obs_property_list_item_name(chooser,0))=="Choose a shader...";
   bool fileSafe=std::string(obs_property_list_item_name(chooser,num-1))=="Custom File...";
   std::cout<<"THEME_SAFE_PICKER_HEADER="<<headerSafe
            <<" CHOOSE="<<selectSafe<<" CUSTOM="<<fileSafe<<"\n";
   passes &= headerSafe && selectSafe && fileSafe;
 }
 std::set<long long> ids;
 std::set<std::string> visibleNames;
 long long plasmaId=0,rikiyaId=0,moshId=0,isolateId=0,reshadeId=0,slitId=0,topoId=0,brokeId=0,liquidId=0;
 bool pframe=false,bulge=false,noise=false,rgb=false,ink=false;
 for(int i=1;i<num-1;i++){
   const auto id=obs_property_list_item_int(chooser,i);
   const std::string label=obs_property_list_item_name(chooser,i);
   passes &= ids.insert(id).second && id!=-1;
   const bool uniqueLabel=visibleNames.insert(label).second;
   if(!uniqueLabel)std::cerr<<"DUPLICATE_PRESET_LABEL "<<label<<"\n";
   passes &= uniqueLabel;
   // The user sees a readable role and effect title, never a raw .effect
   // filename or the old dotted machine-facing label.
   const bool readable=(label.rfind("Filter / ",0)==0 ||
                        label.rfind("Source / ",0)==0 ||
                        label.rfind("Transition / ",0)==0) &&
                        label.find(".effect")==std::string::npos &&
                        label.find('_')==std::string::npos;
   if(!readable)std::cerr<<"UNREADABLE_PRESET "<<i<<" "<<label<<"\n";
   passes &= readable;
   if(label=="Filter / P-Frame Drift")pframe=(id==0);
   if(label=="Filter / Focus Bulge")bulge=(id==-101);
   if(label=="Filter / Focus Noise Field")noise=(id==-102);
   if(label=="Filter / Interactive RGB Shift")rgb=(id==-103);
   if(label=="Source / Plasma")plasmaId=id;
   if(label=="Filter / Ink Reaction / Two-Buffer")ink=true;
   if(label=="Filter / Liquid Light")liquidId=id;
   if(label=="Filter / Motion-Matched Datamosh")moshId=id;
   if(label=="Filter / Motion Isolation")isolateId=id;
   if(label=="Filter / ReShade-Inspired Adaptive Deband")reshadeId=id;
   if(label=="Filter / Gnomalab / Slit-Scan")slitId=id;
   if(label=="Filter / Gnomalab / Topo Scan")topoId=id;
   if(label=="Filter / Gnomalab / Broke in Bad")brokeId=id;
   if(label=="Filter / Rikiya / 202401")rikiyaId=id;
 }
 passes &= pframe&&bulge&&noise&&rgb&&ink && plasmaId < -103 &&
           rikiyaId < -103 && plasmaId != rikiyaId;
 passes &= moshId < -103 && isolateId < -103 && moshId != isolateId;
 const bool expectReshadeDeband=fs::exists(shaderDir/"root"/"filter.reshade_deband.effect");
 if(expectReshadeDeband)
   passes &= reshadeId < -103 && reshadeId != moshId && reshadeId != isolateId;
 else
   passes &= reshadeId==0;
 const bool expectSlitScan=fs::exists(shaderDir/"root"/"filter.gnomalab_slitscan.effect");
 if(expectSlitScan) passes &= slitId < -103 && slitId != moshId && slitId != isolateId;
 else passes &= slitId==0;
 const bool expectTopo=fs::exists(shaderDir/"root"/"filter.gnomalab_topo_scan.effect");
 const bool expectBroke=fs::exists(shaderDir/"root"/"filter.gnomalab_broke_in_bad.effect");
 if(expectTopo) passes &= topoId < -103 && topoId != slitId && topoId != brokeId;
 else passes &= topoId==0;
 if(expectBroke) passes &= brokeId < -103 && brokeId != slitId && brokeId != topoId;
 else passes &= brokeId==0;
 const bool expectLiquid=fs::exists(shaderDir/"root"/"filter.liquid_light.effect");
 if(expectLiquid) passes &= liquidId < -103;
 else passes &= liquidId==0;
 passes &= obs_property_list_item_int(chooser,0)==-1 &&
           obs_property_list_item_int(chooser,num-1)==1;
 std::cout<<"INSTALLED_EFFECTS="<<installed<<" PICKER_ITEMS="<<num
          <<" PLASMA_ID="<<plasmaId<<" RIKIYA_ID="<<rikiyaId<<"\n";
 if(path)passes &= !obs_property_visible(path);
 if(audio)passes &= !obs_property_visible(audio);
 if(graph)passes &= !obs_property_visible(graph);
 if(type)obs_properties_destroy(type);
 auto* filter=obs_source_create("hypershader_filter","UI probe",nullptr,nullptr);
 if(!filter){
  std::cerr<<"FAIL: could not create real HyperShader filter instance\n";
  passes=false;
 }else{
  obs_properties_t* instance=obs_source_properties(filter);
  auto* fchooser=instance?obs_properties_get(instance,"preset"):nullptr;
  auto* fpath=instance?obs_properties_get(instance,"shader_file"):nullptr;
  auto* faudio=instance?obs_properties_get(instance,"audio_source"):nullptr;
  auto* fgraph=instance?obs_properties_get(instance,"mp_controlnet_model"):nullptr;
  auto* ch0=instance?obs_properties_get(instance,"channel0_group"):nullptr;
  auto* global=instance?obs_properties_get(instance,"global_overrides_group"):nullptr;
  bool controlsPresent=global && !obs_properties_get(instance,"controls_group") &&
      !obs_properties_get(instance,"composition_group");
  if(global){
   auto* sub=obs_property_group_content(global);
   controlsPresent &= std::string(obs_property_description(global))=="Global Overrides"
        && sub && obs_properties_get(sub,"speed")
        && obs_properties_get(sub,"reset_time")
        && obs_properties_get(sub,"compose_base_opacity")
        && obs_properties_get(sub,"compose_effect_opacity")
        && obs_properties_get(sub,"compose_blend_mode");
  }
  bool channelLabels=true;
  if(ch0){
    auto* channel=obs_property_group_content(ch0);
    auto* imagePath=channel?obs_properties_get(channel,"channel0_file"):nullptr;
    auto* inputType=channel?obs_properties_get(channel,"channel0_type"):nullptr;
    channelLabels = std::string(obs_property_description(ch0))=="Texture Input 0" &&
        imagePath && std::string(obs_property_description(imagePath))=="Image File Path" &&
        inputType && std::string(obs_property_description(inputType))=="Input Type";
  }
  std::cout<<"THEME_SAFE_CHANNEL_LABELS="<<channelLabels<<"\n";
  passes &= channelLabels && textLabels(instance);
  std::cout<<"GLOBAL_CONTROLS_PRESENT="<<controlsPresent<<"\n";
  passes &= controlsPresent;
  auto* data=obs_source_get_settings(filter);
  auto* filterCategory=instance?obs_properties_get(instance,"shader_category"):nullptr;
  const bool initial=instance && filterCategory &&
       std::string(obs_property_list_item_string(filterCategory,0))=="filter" &&
       !obs_properties_get(instance,"shader_information_group") &&
       !obs_properties_get(instance,"notes_credits_group")
       && fchooser && fpath && faudio && fgraph &&
       ch0 && !obs_property_visible(fpath) &&
       !obs_property_visible(faudio) &&
       !obs_property_visible(fgraph) &&
       !obs_property_visible(ch0) &&
       obs_data_get_int(data,"preset")==-1 &&
       obs_data_get_double(data,"speed")==1.0;
  std::cout<<"INSTANCE_INITIAL_VALID="<<initial<<"\n";
  if(!initial)passes=false;
  if(fchooser && num>=6){
    obs_data_set_int(data,"preset",0);
    bool refreshed=obs_property_modified(fchooser,data);
    std::string selected=obs_data_get_string(data,"shader_file");
    std::cout<<"SELECTED_FILE="<<selected<<" refreshed="<<refreshed<<"\n";
    if(!refreshed || selected.find("/shaders/root/pframe_drift.effect")==std::string::npos)
      passes=false;
    obs_data_set_int(data,"preset",-101);
    bool bulge_refreshed=obs_property_modified(fchooser,data);
    std::string selected_bulge=obs_data_get_string(data,"shader_file");
    std::cout<<"SELECTED_BULGE="<<selected_bulge<<" refreshed="<<bulge_refreshed<<"\n";
    if(!bulge_refreshed || selected_bulge.find("/shaders/root/realworld_focus_bulge.effect")==std::string::npos)
      passes=false;
    if(plasmaId < -103){
      obs_data_set_int(data,"preset",plasmaId);
      bool refreshed=obs_property_modified(fchooser,data);
      const std::string resolved=obs_data_get_string(data,"shader_file");
      std::cout<<"SELECTED_INSTALLED_PLASMA="<<resolved<<"\n";
      passes &= refreshed && resolved.find("/shaders/root/plasma.effect")!=std::string::npos;
    }
    if(rikiyaId < -103){
      obs_data_set_int(data,"preset",rikiyaId);
      bool refreshed=obs_property_modified(fchooser,data);
      const std::string resolved=obs_data_get_string(data,"shader_file");
      std::cout<<"SELECTED_INSTALLED_RIKIYA="<<resolved<<"\n";
      passes &= refreshed && resolved.find("/shaders/root/rikiya/rikiya_202401.effect")!=std::string::npos;
    }
    if(expectLiquid){
      obs_data_set_int(data,"preset",liquidId);
      const bool selected_ok=obs_property_modified(fchooser,data);
      const std::string resolved=obs_data_get_string(data,"shader_file");
      const bool correct=selected_ok && resolved.find("/shaders/root/filter.liquid_light.effect")!=std::string::npos;
      std::cout<<"LIQUID_LIGHT_PRESET_SELECTION="<<correct<<"\n";
      passes &= correct;
    }
    for (auto [id, basename] : std::vector<std::pair<long long, const char *>>{
         {moshId,"filter.motion_mosh.effect"},
         {isolateId,"filter.motion_isolate.effect"}}) {
      obs_data_set_int(data,"preset",id);
      const bool selected_ok=obs_property_modified(fchooser,data);
      const std::string resolved=obs_data_get_string(data,"shader_file");
      const bool correct=selected_ok && resolved.find(std::string("/shaders/root/")+basename)!=std::string::npos;
      std::cout<<"MOTION_PRESET_SELECTION="<<basename<<" PASS="<<correct<<"\n";
      passes &= correct;
    }
    if(expectReshadeDeband){
      obs_data_set_int(data,"preset",reshadeId);
      const bool selected_ok=obs_property_modified(fchooser,data);
      const std::string resolved=obs_data_get_string(data,"shader_file");
      const bool correct=selected_ok && resolved.find("/shaders/root/filter.reshade_deband.effect")!=std::string::npos;
      std::cout<<"RESHADE_PRESET_SELECTION=filter.reshade_deband.effect PASS="<<correct<<"\n";
      passes &= correct;
    }
    if(expectSlitScan){
      obs_data_set_int(data,"preset",slitId);
      const bool selected_ok=obs_property_modified(fchooser,data);
      const std::string resolved=obs_data_get_string(data,"shader_file");
      const bool correct=selected_ok && resolved.find("/shaders/root/filter.gnomalab_slitscan.effect")!=std::string::npos;
      std::cout<<"GNOMALAB_PRESET_SELECTION=filter.gnomalab_slitscan.effect PASS="<<correct<<"\n";
      passes &= correct;
    }
    for(const auto& test : std::vector<std::pair<long long,const char*>>{
      {expectTopo?topoId:0, "filter.gnomalab_topo_scan.effect"},
      {expectBroke?brokeId:0,"filter.gnomalab_broke_in_bad.effect"}}){
      if(test.first==0)continue;
      obs_data_set_int(data,"preset",test.first);
      const bool selected_ok=obs_property_modified(fchooser,data);
      const std::string resolved=obs_data_get_string(data,"shader_file");
      const bool correct=selected_ok &&
        resolved.find(std::string("/shaders/root/")+test.second)!=std::string::npos;
      std::cout<<"GNOMALAB_PRESET_SELECTION="<<test.second<<" PASS="<<correct<<"\n";
      passes &= correct;
    }
    obs_data_set_int(data,"preset",1);
    bool custom_refreshed=obs_property_modified(fchooser,data);
    std::cout<<"CUSTOM_FILE_VISIBLE="<<obs_property_visible(fpath)
             <<" refreshed="<<custom_refreshed<<"\n";
    if(!custom_refreshed || !obs_property_visible(fpath))passes=false;
  }
  obs_data_release(data);
  if(instance)obs_properties_destroy(instance);
  obs_source_release(filter);
 }
 // Old master assigned preset=0 to the first user entry and saved a
 // shader_file. Migration must use that file, not reinterpret 0 as P-frame.
 auto* legacyData=obs_data_create();
 obs_data_set_int(legacyData,"preset",0);
 obs_data_set_string(legacyData,"shader_file","shaders/tests/stdlib_wrapper_probe.effect");
 auto* legacy=obs_source_create("hypershader_filter","Legacy scene",legacyData,nullptr);
 obs_data_release(legacyData);
 if(!legacy){passes=false;} else {
   auto* migrated=obs_source_get_settings(legacy);
   bool preserved=std::string(obs_data_get_string(migrated,"shader_file"))==
       "shaders/tests/stdlib_wrapper_probe.effect";
   bool mapped=obs_data_get_int(migrated,"preset")==1 &&
       obs_data_get_int(migrated,"preset_schema")==2;
   std::cout<<"LEGACY_SCENE_PRESERVED="<<preserved
            <<" MIGRATED_TO_CUSTOM="<<mapped<<"\n";
   passes &= preserved && mapped;
   obs_data_release(migrated);
   obs_source_release(legacy);
 }
 obs_shutdown();
 std::cout<<(passes?"PASS: runtime type UI chooser and initial visibility\n":
                     "FAIL: runtime type UI contract\n");
 return passes?0:1;
}
