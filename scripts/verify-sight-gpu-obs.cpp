#include <obs.h>
#include "interop/sight_gpu.h"
#include <cstdio>
#include <cmath>
int main()
{
    if (!obs_startup("en-US", nullptr, nullptr)) return 2;
    obs_video_info v = {};
    v.graphics_module="libobs-opengl";
    v.fps_num=30; v.fps_den=1; v.base_width=128; v.base_height=128;
    v.output_width=128; v.output_height=128; v.output_format=VIDEO_FORMAT_RGBA;
    v.scale_type=OBS_SCALE_DISABLE;
    if (obs_reset_video(&v)!=OBS_VIDEO_SUCCESS) {obs_shutdown();return 3;}
    bool ok=false;
    obs_enter_graphics();
    gs_texture_t* points=nullptr;
    float data[8] = {0.1f,0.2f,0.3f,1.f,0.4f,0.5f,0.6f,.9f};
    if (hs_sight_upload_rgba32f(&points,2,1,data)) {
        auto* staging=gs_stagesurface_create(2,1,GS_RGBA32F);
        if (staging) {
            gs_stage_texture(staging, points);
            uint8_t* mapped=nullptr;
            uint32_t stride=0;
            if (gs_stagesurface_map(staging,&mapped,&stride)) {
                float *p=reinterpret_cast<float*>(mapped);
                ok=std::fabs(p[0]-.1f)<.001f&&std::fabs(p[4]-.4f)<.001f&&
                   std::fabs(p[7]-.9f)<.001f;
                gs_stagesurface_unmap(staging);
            }
            gs_stagesurface_destroy(staging);
        }
    }
    // A lost signal must clear texture ownership (no stale preview pixels).
    ok=ok&&hs_sight_upload_rgba32f(&points,0,0,nullptr)&&points==nullptr;
    obs_leave_graphics();
    obs_shutdown();
    std::printf("HYPERSHADER_SIGHT_GPU_PASS=%d\n",int(ok));
    return ok?0:1;
}
