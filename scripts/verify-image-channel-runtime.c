#include <obs.h>
#include <graphics/graphics.h>
#include <stdio.h>

#include "channel_image_compat.h"

/* Read actual GPU pixels, not merely decoder status. */
static bool color(gs_texture_t *texture, unsigned component)
{
    if (!texture) return false;
    gs_stagesurf_t *stage = gs_stagesurface_create(2, 2, GS_RGBA);
    if (!stage) return false;
    gs_stage_texture(stage, texture);
    uint8_t *pixels = NULL;
    uint32_t stride = 0;
    bool match = false;
    if (gs_stagesurface_map(stage, &pixels, &stride)) {
        match = pixels[component] > 100 && pixels[(component+1)%3] < 30 &&
                pixels[(component+2)%3] < 30 && pixels[3] > 200;
        gs_stagesurface_unmap(stage);
    }
    gs_stagesurface_destroy(stage);
    return match;
}

int main(int argc, char **argv)
{
	if (argc != 4)
		return 2;
	if (!obs_startup("en-US", NULL, NULL))
		return 3;

	struct obs_video_info info = {0};
	info.graphics_module = "libobs-opengl";
	info.fps_num = 30;
	info.fps_den = 1;
	info.base_width = info.output_width = 128;
	info.base_height = info.output_height = 128;
	info.output_format = VIDEO_FORMAT_RGBA;
	info.colorspace = VIDEO_CS_DEFAULT;
	info.range = VIDEO_RANGE_DEFAULT;
	info.scale_type = OBS_SCALE_DISABLE;
	if (obs_reset_video(&info) != OBS_VIDEO_SUCCESS) {
		obs_shutdown();
		return 4;
	}

	if (!hs_channel_image_runtime_init()) {
		obs_shutdown();
		return 5;
	}

	hs_channel_image_t image = {0};
	bool empty_safe = !hs_channel_image_loaded(&image) && hs_channel_image_texture(&image) == NULL;
	if (!empty_safe) {
		obs_shutdown();
		return 6;
	}
	obs_enter_graphics();
	bool loaded = hs_channel_image_load(&image, argv[1]);
	gs_texture_t *texture = hs_channel_image_texture(&image);
	bool valid = loaded && texture && gs_texture_get_width(texture) == 2 && gs_texture_get_height(texture) == 2;
    hs_channel_image_free(&image);
    bool animated=hs_channel_image_load(&image, argv[2]) && hs_channel_image_animated(&image);
    bool initial=animated && color(hs_channel_image_texture(&image),0);
    bool advanced=animated && hs_channel_image_advance(&image,150000000ULL) && color(hs_channel_image_texture(&image),1);
    bool restarted=animated && hs_channel_image_restart(&image) && color(hs_channel_image_texture(&image),0);
    bool advanced_again=animated && hs_channel_image_advance(&image,150000000ULL) && color(hs_channel_image_texture(&image),1);
    valid=valid && initial && animated && advanced && restarted && advanced_again;
    /* Restart-on-reaction: no hidden advance, duplicate frame is inert,
     * and an activation displays the first frame before advancing. */
    uint64_t t=1000000000ULL;
    hs_channel_image_play(&image,t,HS_IMAGE_RESTART,false,false);
    hs_channel_image_play(&image,t+150000000ULL,HS_IMAGE_RESTART,false,false);
    bool hidden=color(hs_channel_image_texture(&image),1);
    t+=200000000ULL;
    hs_channel_image_play(&image,t,HS_IMAGE_RESTART,true,true);
    bool edge=color(hs_channel_image_texture(&image),0);
    hs_channel_image_play(&image,t,HS_IMAGE_RESTART,true,true);
    bool duplicate=color(hs_channel_image_texture(&image),0);
    hs_channel_image_play(&image,t+150000000ULL,HS_IMAGE_RESTART,true,true);
    bool running=color(hs_channel_image_texture(&image),1);
    // Resume: paused time must not be added to the clip.
    t+=200000000ULL;
    hs_channel_image_play(&image,t,HS_IMAGE_RESUME,false,false);
    hs_channel_image_play(&image,t+200000000ULL,HS_IMAGE_RESUME,false,false);
    bool paused=color(hs_channel_image_texture(&image),1);
    hs_channel_image_play(&image,t+210000000ULL,HS_IMAGE_RESUME,true,true);
    bool resumed=color(hs_channel_image_texture(&image),1);
    hs_channel_image_play(&image,t+280000000ULL,HS_IMAGE_RESUME,true,true);
    bool resumed_next=color(hs_channel_image_texture(&image),2);
    // A long render gap never causes a fast-forward on return.
    hs_channel_image_play(&image,t+2000000000ULL,HS_IMAGE_CONTINUOUS,false,false);
    bool no_catchup=color(hs_channel_image_texture(&image),2);
    hs_channel_image_play(&image,t+2150000000ULL,HS_IMAGE_CONTINUOUS,false,false);
    bool continuous=color(hs_channel_image_texture(&image),0);
    bool playback=hidden&&edge&&duplicate&&running&&paused&&resumed&&resumed_next&&no_catchup&&continuous;
    valid=valid&&playback;
    printf("GIF_PLAYBACK hidden=%d edge=%d duplicate=%d running=%d paused=%d resumed=%d next=%d no_catchup=%d continuous=%d PASS=%d\n",
           hidden,edge,duplicate,running,paused,resumed,resumed_next,no_catchup,continuous,playback);
    printf("GIF animated=%d advanced=%d restarted=%d advanced_again=%d\n",animated,advanced,restarted,advanced_again);
    hs_channel_image_free(&image);
    bool finite=hs_channel_image_load(&image,argv[3]);
    for(int i=0;i<10;i++) hs_channel_image_advance(&image,150000000ULL);
    bool finished=finite&&color(hs_channel_image_texture(&image),2);
    bool replay=hs_channel_image_restart(&image)&&color(hs_channel_image_texture(&image),0)
        &&hs_channel_image_advance(&image,150000000ULL)&&color(hs_channel_image_texture(&image),1);
    valid=valid&&finished&&replay;
    printf("GIF_ONESHOT finished=%d replay=%d PASS=%d\n",finished,replay,finished&&replay);


	hs_channel_image_free(&image);
	obs_leave_graphics();

	printf("OBS_IMAGE_RUNTIME=%u EMPTY_SAFE=%d LOADED=%d PASS=%d\n",
	       obs_get_version(), empty_safe, loaded, valid);
	obs_shutdown();
	return valid ? 0 : 7;
}
