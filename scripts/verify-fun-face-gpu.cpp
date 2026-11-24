// Isolated real-libobs/OpenGL test of a camera image and 468 RGBA32F face landmarks.
// No webcam, model inference, installed OBS plugin or mock shader execution.
#include <obs.h>
#include <graphics/graphics.h>
#include <util/bmem.h>
#include <png.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
namespace fs=std::filesystem;
constexpr unsigned W=320,H=192;
using Frame=std::vector<uint8_t>;
using Landmarks=std::vector<float>;
static Frame camera(){
    Frame f(W*H*4);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        float u=float(x)/W,v=float(y)/H;
        float ellipse=std::pow((u-.5f)/.265f,2.f)+std::pow((v-.52f)/.42f,2.f);
        bool skin=ellipse<1;
        float l=1.f-std::min(1.f,ellipse);
        unsigned i=4*(y*W+x);
        f[i]=skin?uint8_t(155+std::round(l*50)):uint8_t(30+u*40);
        f[i+1]=skin?uint8_t(110+std::round(l*40)):uint8_t(44+v*30);
        f[i+2]=skin?uint8_t(101+std::round(l*38)):uint8_t(80+u*25);
        f[i+3]=255;
        // High-contrast checkers and facial features reveal distortion, aliasing.
        bool grid=(x%18<2)||(y%18<2);
        if(skin && grid){f[i]=std::min(255,int(f[i])+22);f[i+1]=std::max(0,int(f[i+1])-15);}
        if((std::pow((u-.395f)/.045f,2)+std::pow((v-.39f)/.022f,2)<1)||
           (std::pow((u-.615f)/.045f,2)+std::pow((v-.39f)/.022f,2)<1))
            {f[i]=19;f[i+1]=21;f[i+2]=33;}
        if(std::pow((u-.5f)/.10f,2)+std::pow((v-.735f)/.022f,2)<1)
            {f[i]=75;f[i+1]=23;f[i+2]=45;}
    }
    return f;
}
static Landmarks landmarkData(bool valid){
    Landmarks data(468*4,0.f);
    auto put=[&](int i,float x,float y){data[i*4]=x;data[i*4+1]=y;data[i*4+3]=1;};
    if(!valid)return data;
    for(int i=0;i<468;i++)put(i,.5f,.5f);
    put(1,.5f,.535f);put(4,.5f,.56f);
    put(13,.50f,.71f);put(14,.50f,.75f);put(78,.395f,.73f);put(308,.605f,.73f);
    put(33,.34f,.39f);put(133,.45f,.39f);
    put(362,.56f,.39f);put(263,.67f,.39f);
    put(159,.395f,.37f);put(145,.395f,.42f);
    put(386,.615f,.37f);put(374,.615f,.42f);
    put(205,.305f,.62f);put(425,.695f,.62f);
    return data;
}
static void param(gs_effect_t* fx,const char*name,float v){
    auto* p=gs_effect_get_param_by_name(fx,name);
    if(p)gs_effect_set_float(p,v);
}
static void setTime(gs_effect_t*fx,float seconds){
    param(fx,"hs_time",seconds);
    vec3 res;vec3_set(&res,float(W),float(H),float(W)/H);
    if(auto*p=gs_effect_get_param_by_name(fx,"hs_resolution"))gs_effect_set_vec3(p,&res);
}
static Frame draw(gs_effect_t* fx,gs_texture_t*image,gs_texture_t* landmarks,bool useChannel,bool facePresent){
    gs_set_cull_mode(GS_NEITHER);
    auto*target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
    auto*stage=gs_stagesurface_create(W,H,GS_RGBA);
    Frame result;
    if(target&&stage&&gs_texrender_begin(target,W,H)){
        gs_projection_push();gs_matrix_push();gs_matrix_identity();
        gs_ortho(0,float(W),0,float(H),-100,100);
        if(auto*p=gs_effect_get_param_by_name(fx,"hs_input0"))
            gs_effect_set_texture(p,useChannel?image:nullptr);
        if(auto*p=gs_effect_get_param_by_name(fx,"image"))
            gs_effect_set_texture(p,image);
        if(auto*p=gs_effect_get_param_by_name(fx,"hs_input_resolution0")){
            vec4 dim;vec4_set(&dim,useChannel?float(W):0.0f,useChannel?float(H):0.0f,float(W)/H,0);
            gs_effect_set_vec4(p,&dim);
        }
        if(auto*p=gs_effect_get_param_by_name(fx,"hs_landmarks"))gs_effect_set_texture(p,landmarks);
        if(auto*p=gs_effect_get_param_by_name(fx,"hs_face_present"))gs_effect_set_float(p,facePresent?1.0f:0.0f);
        while(gs_effect_loop(fx,"Draw"))gs_draw_sprite(nullptr,0,W,H);
        gs_matrix_pop();gs_projection_pop();gs_texrender_end(target);
        if(auto*t=gs_texrender_get_texture(target)){
            gs_stage_texture(stage,t);
            uint8_t*map=nullptr;uint32_t stride=0;
            if(gs_stagesurface_map(stage,&map,&stride)){
                result.resize(W*H*4);
                for(unsigned y=0;y<H;y++)
                    std::memcpy(result.data()+W*4*y,map+y*stride,W*4);
                gs_stagesurface_unmap(stage);
            }
        }
    }
    if(stage)gs_stagesurface_destroy(stage);
    if(target)gs_texrender_destroy(target);
    return result;
}
static void png(const fs::path& dest,const Frame& f){
    if(f.size()!=W*H*4)return;
    png_image image{};
    image.version=PNG_IMAGE_VERSION;image.width=W;image.height=H;image.format=PNG_FORMAT_RGBA;
    if(!png_image_write_to_file(&image,dest.c_str(),0,f.data(),W*4,nullptr))
        {std::fprintf(stderr,"PNG error %s\n",image.message);std::exit(6);}
}
int main(int argc,char**argv){
    if(argc!=4){std::fprintf(stderr,"usage: %s effect output-dir mode\n",argv[0]);return 2;}
    const std::string mode=argv[3];
    bool known=false;
    for(auto candidate:{"base","enhanced","blank","bypass","mouth","laser","eyes","blush","sparkles","fallback"})
        if(mode==candidate)known=true;
    if(!known)return 2;
    if(!obs_startup("en-US",nullptr,nullptr))return 3;
    obs_video_info video{};video.graphics_module="libobs-opengl";
    video.fps_num=30;video.fps_den=1;video.base_width=W;video.base_height=H;
    video.output_width=W;video.output_height=H;video.output_format=VIDEO_FORMAT_RGBA;
    video.scale_type=OBS_SCALE_DISABLE;
    if(obs_reset_video(&video)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 4;}
    bool success=false;
    obs_enter_graphics();
    char*errors=nullptr;
    auto*fx=gs_effect_create_from_file(argv[1],&errors);
    if(errors){std::fprintf(stderr,"Shader compiler: %s\n",errors);bfree(errors);}
    if(fx){
        auto rgb=camera();auto points=landmarkData(mode!="blank");
        const uint8_t* rgbData=rgb.data();
        const uint8_t* lmData=reinterpret_cast<const uint8_t*>(points.data());
        auto*tex=gs_texture_create(W,H,GS_RGBA,1,&rgbData,0);
        auto*lm=gs_texture_create(468,1,GS_RGBA32F,1,&lmData,0);
        if(tex&&lm){
            for(auto name:{"toggle_nose","toggle_laser","toggle_mouth","toggle_big_eyes","toggle_blush","toggle_sparkles"}){
                bool active=mode=="enhanced" || mode=="blank" ||
                            ((mode=="base" || mode=="fallback") && std::string(name)=="toggle_nose") ||
                            (mode=="mouth" && std::string(name)=="toggle_mouth") ||
                            (mode=="laser" && std::string(name)=="toggle_laser") ||
                            (mode=="eyes" && std::string(name)=="toggle_big_eyes") ||
                            (mode=="blush" && std::string(name)=="toggle_blush") ||
                            (mode=="sparkles" && std::string(name)=="toggle_sparkles");
                auto*parameter=gs_effect_get_param_by_name(fx,name);
                if(!parameter){std::fprintf(stderr,"Missing effect parameter: %s\n",name);return 8;}
                gs_effect_set_float(parameter,active?1.f:0.f);
            }
            // Raw libobs shaders default uniforms to zero. Supply the same
            // @default values HyperShader's metadata/UI loader normally sets.
            for(const auto& control:std::array<std::pair<const char*,float>,8>{{
                {"nose_size",1.0f},{"mouth_strength",0.65f},
                {"mouth_radius",0.17f},{"eye_scale",0.65f},
                {"laser_intensity",0.75f},{"laser_width",0.005f},
                {"blush_strength",0.6f},{"sparkle_strength",0.75f}
            }}){
                auto*parameter=gs_effect_get_param_by_name(fx,control.first);
                if(!parameter){std::fprintf(stderr,"Missing control: %s\n",control.first);return 9;}
                gs_effect_set_float(parameter,control.second);
            }
            if(auto*p=gs_effect_get_param_by_name(fx,"laser_color")){
                vec4 c;vec4_set(&c,1.0f,.22f,.18f,1.0f);gs_effect_set_vec4(p,&c);
            }
            for(float t:{0.5f,2.25f}){
                setTime(fx,t);
                Frame out=draw(fx,tex,lm,mode!="fallback",mode!="blank");
                if(out.size()==W*H*4){
                    const auto name=mode+"-"+(t<1?"0":"2")+".png";
                    png(fs::path(argv[2])/name,out);
                    success=true;
                }
            }
            gs_texture_destroy(lm);gs_texture_destroy(tex);
        }
        gs_effect_destroy(fx);
    }
    obs_leave_graphics();
    obs_shutdown();
    std::printf("FACE_CAPTURE_%s %s\n",success?"PASS":"FAIL",mode.c_str());
    return success?0:1;
}
