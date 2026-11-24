#include "shader/shader_categories.h"
#include <string>
#include <iostream>
static int fails=0;
static void check(bool ok,const char*name) {
 if(!ok){std::cerr<<"FAIL "<<name<<"\n";fails++;}
}
int main(){
 char category[160];
 check(hs_shader_category_for("root/shabon/source.shabon_aurora_waves.effect","Source / Shabon / Aurora Waves",false,category,sizeof(category)) &&
       std::string(category)=="collection:shabon","Shabon collection");
 check(hs_shader_category_for("root/rikiya/rikiya_202401.effect","Filter / Rikiya / 202401",false,category,sizeof(category)) &&
       std::string(category)=="collection:rikiya","Rikiya collection");
 check(hs_shader_category_for("root/plasma.effect","Source / Plasma",false,category,sizeof(category)) &&
       std::string(category)=="source","Source role");
 check(hs_shader_category_for("root/pframe_drift.effect","Filter / P-Frame Drift",false,category,sizeof(category)) &&
       std::string(category)=="filter","Filter role");
 check(hs_shader_category_for("/user/presets/my.effect","Filter / My",true,category,sizeof(category)) &&
       std::string(category)=="user","User preset");
 check(hs_shader_category_matches("source","root/shabon/source.shabon_aurora_waves.effect","Source / Shabon / Aurora Waves",false),"Shabon also in Source");
 check(hs_shader_category_matches("collection:shabon","root/shabon/source.shabon_aurora_waves.effect","Source / Shabon / Aurora Waves",false),"Shabon collection matches");
 check(!hs_shader_category_matches("collection:rikiya","root/shabon/source.shabon_aurora_waves.effect","Source / Shabon / Aurora Waves",false),"No collection crossover");
 check(!hs_shader_category_matches("filter","root/shabon/source.shabon_aurora_waves.effect","Source / Shabon / Aurora Waves",false),"Role separates source");
 check(hs_shader_category_matches("all","root/shabon/source.shabon_aurora_waves.effect","Source / Shabon / Aurora Waves",false),"All includes all");
 check(hs_shader_category_matches("user","/user/presets/test.effect","Filter / Test",true),"User presets group");
 check(!hs_shader_category_matches("user","root/pframe_drift.effect","Filter / P-Frame Drift",false),"User excludes built-in");
 check(hs_shader_category_display_name("collection:shabon",category,sizeof(category)) &&
       std::string(category)=="Shabon FX","Shabon display name");
 check(hs_shader_category_display_name("collection:rikiya",category,sizeof(category)) &&
       std::string(category)=="Rikiya","Rikiya display name");
 check(hs_shader_category_display_name("collection:my_fx",category,sizeof(category)) &&
       std::string(category)=="My Fx","Generic collection");
 check(hs_shader_category_item_label("collection:shabon","Source / Shabon / Aurora Waves",category,sizeof(category)) &&
       std::string(category)=="Aurora Waves","Collection strips prefix");
 check(hs_shader_category_item_label("filter","Filter / Rikiya / 202401",category,sizeof(category)) &&
       std::string(category)=="Rikiya / 202401","Role strips prefix");
 check(hs_shader_category_item_label("all","Source / Plasma",category,sizeof(category)) &&
       std::string(category)=="Source / Plasma","All retains role");
 std::cout<<"CATEGORY_NAMING_UNIT PASS="<<(fails==0)<<" checks=17\n";
 return fails?1:0;
}
