#include <obs.h>
#include <obs-module.h>
#include "interop/sight_client.h"
#include <cstdio>
int main(int argc, char** argv)
{
    if ((argc != 3 && argc != 5) || !obs_startup("en-US", nullptr, nullptr)) return 2;
    obs_video_info v = {};
    v.graphics_module = "libobs-opengl";
    v.fps_num = 30; v.fps_den = 1;
    v.base_width = 128; v.base_height = 128;
    v.output_width = 128; v.output_height = 128;
    v.output_format = VIDEO_FORMAT_RGBA;
    v.scale_type = OBS_SCALE_DISABLE;
    if (obs_reset_video(&v) != OBS_VIDEO_SUCCESS) { obs_shutdown(); return 3; }
    if (hs_sight_reader_create() != nullptr) { obs_shutdown(); return 4; }
    obs_module_t* sight = nullptr;
    if (obs_open_module(&sight, argv[1], argv[2]) != MODULE_SUCCESS || !obs_init_module(sight)) {
        obs_shutdown(); return 5;
    }
    obs_source_t* reader = hs_sight_reader_create();
    if (!reader) { obs_shutdown(); return 6; }
    sight_snapshot_v1 snap = {};
    snap.point_count = 100; // the reader must never return stale contents
    bool available = hs_sight_reader_read(reader, "not-published", 750, &snap);
    const bool pass = !available && snap.abi_version == SIGHT_SNAPSHOT_ABI_V1 &&
        snap.point_count == 0 && snap.hands_count == 0;
    auto* scene=obs_scene_create("Discovery scene");
    auto* parent=obs_scene_get_source(scene);
    auto* settings=obs_data_create();
    obs_data_set_string(settings,"tracking_mode","holistic");
    obs_data_set_string(settings,"holistic_model_path","");
    obs_data_set_string(settings,"holistic_signal_channel_face","camera/custom-face");
    obs_data_set_string(settings,"holistic_signal_channel_body","camera/custom-body");
    obs_data_set_string(settings,"holistic_signal_channel_hand","camera/custom-hand");
    auto* producer=obs_source_create("sight_unified_tracking_filter","Discovery producer",settings,nullptr);
    obs_data_release(settings);
    if(!producer)return 8;
    obs_source_set_enabled(producer,false);
    obs_source_filter_add(parent,producer);
    auto* props=obs_properties_create();
    auto* list=obs_properties_add_list(props,"sight_tracking_channel","Channel",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
    auto has=[&](const char* key,bool disabled){
        for(size_t i=0;i<obs_property_list_item_count(list);++i)
            if(!strcmp(obs_property_list_item_string(list,i),key))
                return obs_property_list_item_disabled(list,i)==disabled;
        return false;
    };
    bool discovered=hs_sight_populate_channels(list,"old/saved")&&
        has("camera/custom-face",false)&&has("camera/custom-body",false)&&
        has("camera/custom-hand",false)&&has("old/saved",true);
    if(argc==5) {
        obs_module_t* hyper=nullptr;
        if(obs_open_module(&hyper,argv[3],argv[4])!=MODULE_SUCCESS||!obs_init_module(hyper))return 10;
        auto* cfg=obs_data_create();
        obs_data_set_bool(cfg,"sight_use_tracking",true);
        obs_data_set_string(cfg,"sight_tracking_channel","camera/custom-hand");
        auto* filter=obs_source_create("hypershader_filter","Discovery shader",cfg,nullptr);
        obs_data_release(cfg);
        if(!filter)return 11;
        obs_source_filter_add(parent,filter);
        auto* actual=obs_source_properties(filter);
        auto* picker=obs_properties_get(actual,"sight_tracking_channel");
        bool actual_choice=false;
        if(picker&&obs_property_get_type(picker)==OBS_PROPERTY_LIST&&
           obs_property_list_type(picker)==OBS_COMBO_TYPE_LIST)
            for(size_t i=0;i<obs_property_list_item_count(picker);++i)
                actual_choice|=!strcmp(obs_property_list_item_string(picker,i),"camera/custom-hand");
        obs_properties_destroy(actual);
        obs_source_filter_remove(parent,filter);
        obs_source_release(filter);
        printf("HYPERSHADER_SIGHT_PROPERTY_PICKER_PASS=%d\n",actual_choice);
        if(!actual_choice)return 12;
    }
    obs_source_filter_remove(parent,producer);
    obs_source_release(producer);
    hs_sight_populate_channels(list,"camera/custom-hand");
    bool removed=has("camera/custom-hand",true)&&!has("camera/custom-face",false);
    obs_properties_destroy(props);
    obs_scene_release(scene);
    if(!discovered||!removed)return 9;
    puts("HYPERSHADER_SIGHT_INVENTORY_PASS=1");
    obs_source_release(reader);
    obs_shutdown();
    if (!pass) return 7;
    puts("HYPERSHADER_SIGHT_CLIENT_OBS_PASS=1");
    return 0;
}
