#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <cstdio>
#include <thread>
#include <chrono>
#include <vector>
#include <fstream>
#include <cstring>
using namespace std::chrono_literals;
constexpr unsigned W=256,H=256;
struct Fixture {gs_texture_t *texture=nullptr;};
Fixture *active=nullptr;
const char* fixture_name(void*){return "MediaPipe face fixture";}
void* fixture_create(obs_data_t*,obs_source_t*){
 auto* f=new Fixture();
 obs_enter_graphics();
 f->texture=gs_texture_create(W,H,GS_RGBA,1,nullptr,GS_DYNAMIC);
 obs_leave_graphics();active=f;return f;
}
void fixture_destroy(void*p){
 auto* f=(Fixture*)p;
 obs_enter_graphics();
 if(f->texture)gs_texture_destroy(f->texture);
 obs_leave_graphics();active=nullptr;delete f;
}
uint32_t fixture_width(void*){return W;}
uint32_t fixture_height(void*){return H;}
void fixture_render(void*p,gs_effect_t*){auto*f=(Fixture*)p; if(f->texture)obs_source_draw(f->texture,0,0,W,H,false);}
obs_source_info fixture_info={
 .id="mp_audit_fixture",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_VIDEO,
 .get_name=fixture_name,.create=fixture_create,.destroy=fixture_destroy,
 .get_width=fixture_width,.get_height=fixture_height,.video_render=fixture_render
};
static unsigned last_bright_pixels=0;
float render(obs_source_t*source){
 float sample=-1;
 obs_enter_graphics();
 auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
 auto* stage=gs_stagesurface_create(W,H,GS_RGBA);
 if(target&&stage&&gs_texrender_begin(target,W,H)){
    gs_projection_push();gs_matrix_push();gs_matrix_identity();
    gs_ortho(0,float(W),0,float(H),-100,100);
    struct vec4 clear={0,0,0,0};
    gs_clear(GS_CLEAR_COLOR,&clear,0,0);
    obs_source_video_render(source);
    gs_matrix_pop();gs_projection_pop();gs_texrender_end(target);
    if(auto* tex=gs_texrender_get_texture(target)){
      gs_stage_texture(stage,tex);
      uint8_t* pix=nullptr;uint32_t pitch=0;
      if(gs_stagesurface_map(stage,&pix,&pitch)){
        sample=float(pix[(H/2)*pitch+(W/2)*4])/255.0f;
        last_bright_pixels=0;
        for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++){
          const uint8_t *pixel=pix+y*pitch+x*4;
          if(pixel[0]>25 || pixel[1]>25 || pixel[2]>25) last_bright_pixels++;
        }
        gs_stagesurface_unmap(stage);
      }
    }
 }
 if(stage)gs_stagesurface_destroy(stage);
 if(target)gs_texrender_destroy(target);
 obs_leave_graphics();return sample;
}
int main(int argc,char**argv){
 if(argc!=5)return 2;
 std::ifstream input(argv[4],std::ios::binary);
 std::vector<uint8_t> face(W*H*4);
 if(!input.read(reinterpret_cast<char*>(face.data()),face.size()))return 3;
 if(!obs_startup("en-US",nullptr,nullptr))return 4;
 obs_video_info video{}; video.graphics_module="libobs-opengl";video.fps_num=30;video.fps_den=1;
 video.base_width=W;video.base_height=H;video.output_width=W;video.output_height=H;
 video.output_format=VIDEO_FORMAT_RGBA;video.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&video)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 5;}
 obs_register_source(&fixture_info);
 obs_module_t* module=nullptr;
 if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS || !obs_init_module(module)){
    obs_shutdown();return 6;
 }
 auto* videoSource=obs_source_create("mp_audit_fixture","Public face input",nullptr,nullptr);
 auto* opts=obs_data_create();obs_data_set_string(opts,"shader_file",argv[3]);
 obs_data_set_int(opts,"mp_controlnet_model",2);
 auto* filter=obs_source_create("hypershader_filter","Face GPU readback",opts,nullptr);
 obs_data_release(opts);
 if(!videoSource||!filter){obs_shutdown();return 7;}
 obs_source_filter_add(videoSource,filter);obs_source_inc_showing(videoSource);
 obs_set_output_source(0,videoSource);
 std::vector<uint8_t> blank(W*H*4,0);
 for(size_t i=3;i<blank.size();i+=4)blank[i]=255;
 obs_enter_graphics();gs_texture_set_image(active->texture,face.data(),W*4,false);obs_leave_graphics();
 float initial=0,faceLast=0;
 bool modeUi=false;
 for(int i=0;i<90;i++){float x=render(videoSource);if(i==20)initial=x;if(i>70)faceLast=x;std::this_thread::sleep_for(35ms);}
 obs_enter_graphics();gs_texture_set_image(active->texture,blank.data(),W*4,false);obs_leave_graphics();
 float blankLast=0;
 unsigned blankBright=0;
 for(int i=0;i<90;i++){
    float x=render(videoSource);
    if(i>70){blankLast=x;blankBright=last_bright_pixels;}
    std::this_thread::sleep_for(35ms);
 }
 auto* properties=obs_source_properties(filter);
 auto* modes=properties?obs_properties_get(properties,"mp_controlnet_model"):nullptr;
 if (modes) {
    modeUi=obs_property_list_item_disabled(modes,2)&&
        obs_property_list_item_disabled(modes,3)&&
        obs_property_list_item_disabled(modes,4)&&
        obs_property_list_item_disabled(modes,5)&&
        !obs_property_list_item_disabled(modes,1);
 }
 if(properties)obs_properties_destroy(properties);
 // The face fixture is centered in its native 256x256 frame. Capturing
 // only its upper-left 192x192 corner shifts the detected nose far right.
 bool pass=faceLast>0.25f&&faceLast<0.75f&&blankLast<0.05f&&blankBright<10&&modeUi;
 printf("MP_FILTER_GPU initial=%.4f face=%.4f blank=%.4f blank_bright=%u mode_ui=%d PASS=%d\n",initial,faceLast,blankLast,blankBright,modeUi,pass);
 obs_set_output_source(0,nullptr);

 obs_source_dec_showing(videoSource);
 obs_source_filter_remove(videoSource,filter);
 obs_source_release(filter);obs_source_release(videoSource);
 obs_shutdown();
 return pass?0:1;
}
