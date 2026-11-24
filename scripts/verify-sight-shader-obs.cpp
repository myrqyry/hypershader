#include <obs.h>
#include <graphics/graphics.h>
#include <cstdio>
int main(int argc, char**argv)
{
    if (argc!=2 || !obs_startup("en-US",nullptr,nullptr)) return 2;
    obs_video_info v={};
    v.graphics_module="libobs-opengl";
    v.fps_num=30;v.fps_den=1;v.base_width=128;v.base_height=128;
    v.output_width=128;v.output_height=128;v.output_format=VIDEO_FORMAT_RGBA;
    v.scale_type=OBS_SCALE_DISABLE;
    if(obs_reset_video(&v)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 3;}
    obs_enter_graphics();
    char* errors=nullptr;
    gs_effect_t* effect=gs_effect_create_from_file(argv[1],&errors);
    bool pass=effect &&
        gs_effect_get_technique(effect,"Draw") &&
        gs_effect_get_param_by_name(effect,"hs_sight_present") &&
        ((gs_effect_get_param_by_name(effect,"hs_sight_hands") &&
          gs_effect_get_param_by_name(effect,"hs_sight_hand_count")) ||
         (gs_effect_get_param_by_name(effect,"hs_sight_points") &&
          gs_effect_get_param_by_name(effect,"hs_sight_point_count")));
    if(errors) {fprintf(stderr,"SHADER_DIAGNOSTICS=%s\n",errors);bfree(errors);}
    if(effect)gs_effect_destroy(effect);
    obs_leave_graphics();
    obs_shutdown();
    printf("HYPERSHADER_SIGHT_EFFECT_PASS=%d\n",int(pass));
    return pass?0:1;
}
