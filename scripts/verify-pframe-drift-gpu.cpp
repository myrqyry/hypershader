// Isolated libobs/OpenGL regression for HyperShader P-Frame Drift feedback.
#include <obs.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <array>
#include <cmath>
#include <iostream>
constexpr unsigned W=160,H=96;
using Frame=std::vector<uint8_t>;
Frame pattern(int x) {
    Frame data(W*H*4,0);
    for(unsigned y=0;y<H;y++)for(unsigned z=0;z<W;z++) {
        unsigned i=4*(y*W+z);
        // Static textured background and a solid high contrast moving body.
        bool object= (z>=x&&z<x+30&&y>=22&&y<77);
        data[i]=object?232:uint8_t((z*2+y)%128);
        data[i+1]=object?155:uint8_t((z+y*2)%86);
        data[i+2]=object?35:uint8_t((z*3+y)%64);
        data[i+3]=255;
    }
    return data;
}
size_t difference(const Frame& a,const Frame& b) {
    if(a.size()!=b.size())return size_t(-1);
    size_t count=0;
    for(size_t i=0;i<a.size();i+=4) {
        count+=abs(int(a[i])-int(b[i]))>3;
        count+=abs(int(a[i+1])-int(b[i+1]))>3;
        count+=abs(int(a[i+2])-int(b[i+2]))>3;
    }
    return count;
}
void setf(gs_effect_t* e,const char* key,float value) {
    auto* p=gs_effect_get_param_by_name(e,key);
    if(p)gs_effect_set_float(p,value);
}
Frame draw(gs_effect_t* effect,gs_texrender_t* target,gs_texture_t* now,
           gs_texture_t* prev,int frame,float strength,float reset) {
    gs_set_cull_mode(GS_NEITHER);
    gs_texrender_reset(target);
    if(!gs_texrender_begin(target,W,H))return {};
    gs_projection_push();gs_matrix_push();gs_matrix_identity();
    gs_ortho(0,float(W),0,float(H),-100,100);
    vec4 clear{};vec4_zero(&clear);gs_clear(GS_CLEAR_COLOR,&clear,0,0);
    gs_effect_set_texture(gs_effect_get_param_by_name(effect,"image"),now);
    gs_effect_set_texture(gs_effect_get_param_by_name(effect,"iPreviousFrame"),prev);
    auto* rp=gs_effect_get_param_by_name(effect,"iResolution");
    vec3 resolution{};vec3_set(&resolution,float(W),float(H),float(W)/H);
    gs_effect_set_vec3(rp,&resolution);
    auto* fi=gs_effect_get_param_by_name(effect,"iFrame");
    gs_effect_set_int(fi,frame);
    setf(effect,"iTime",frame/30.f+.5f);
    setf(effect,"iTimeDelta",1.f/30.f);
    setf(effect,"pframe_loss",strength);
    setf(effect,"missing_residual",1.f);
    setf(effect,"vector_carry",1.6f);
    setf(effect,"macroblock_size",16.f);
    setf(effect,"residual_recovery",.015f);
    setf(effect,"iframe_interval",0.f);
    setf(effect,"force_keyframe",reset);
    setf(effect,"burst_period",0.f);
    setf(effect,"burst_duty",.68f);
    setf(effect,"chroma_loss",0.f);
    while(gs_effect_loop(effect,"Draw"))gs_draw_sprite(now,0,W,H);
    gs_matrix_pop();gs_projection_pop();gs_texrender_end(target);
    auto* stage=gs_stagesurface_create(W,H,GS_RGBA);
    if(!stage)return {};
    auto* output=gs_texrender_get_texture(target);
    gs_stage_texture(stage,output);
    uint8_t* ptr=nullptr;uint32_t stride=0;
    Frame data;
    if(gs_stagesurface_map(stage,&ptr,&stride)){
        data.resize(W*H*4);
        for(unsigned y=0;y<H;y++)memcpy(data.data()+y*W*4,ptr+y*stride,W*4);
        gs_stagesurface_unmap(stage);
    }
    gs_stagesurface_destroy(stage);
    return data;
}
int main(int argc,char** argv){
 if(argc!=2)return 2;
 if(!obs_startup("en-US",nullptr,nullptr)){std::cerr<<"startup failed\n";return 1;}
 obs_video_info v{};
 v.graphics_module="libobs-opengl";
 v.fps_num=30;v.fps_den=1;
 v.base_width=W;v.base_height=H;v.output_width=W;v.output_height=H;
 v.output_format=VIDEO_FORMAT_RGBA;
 v.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&v)!=OBS_VIDEO_SUCCESS){std::cerr<<"video failed\n";obs_shutdown();return 1;}
 bool success=false;gs_effect_t* shader=nullptr;
 obs_enter_graphics();
 char* errors=nullptr;
 shader=gs_effect_create_from_file(argv[1],&errors);
 if(errors){std::cerr<<"shader: "<<errors<<"\n";bfree(errors);}
 if(!shader){std::cerr<<"FAILED real OBS shader compile\n";}
 else {
     auto* a=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
     auto* b=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
     auto* prev=gs_texture_create(W,H,GS_RGBA,1,nullptr,GS_DYNAMIC);
     std::array<int,6> xs={8,8,17,29,42,54};
     Frame current,output,at4,at5,clean;
     size_t ghost=0,clean_delta=0;
     bool alpha_good=true;
     for(int i=0;i<6;i++){
        current=pattern(xs[i]);
        if(i==0)gs_texture_set_image(prev,current.data(),W*4,false);
        const uint8_t* data=current.data();
        auto* tex=gs_texture_create(W,H,GS_RGBA,1,&data,0);
        gs_texture_t* previous=i==0?prev:gs_texrender_get_texture((i-1)%2?b:a);
        output=draw(shader,i%2?b:a,tex,previous,i,.99f,0.f);
        if(i==4){at4=output;ghost=difference(output,current);}
        if(i==5)at5=output;
        for(size_t j=3;j<output.size();j+=4)alpha_good&=output[j]==255;
        gs_texture_destroy(tex);
     }
     {
        current=pattern(xs[5]);
        const uint8_t* data=current.data();
        auto* tex=gs_texture_create(W,H,GS_RGBA,1,&data,0);
        clean=draw(shader,a,tex,gs_texrender_get_texture(b),6,.99f,1.f);
        clean_delta=difference(clean,current);
        gs_texture_destroy(tex);
     }
     size_t disabled_delta=size_t(-1);
     {
        current=pattern(xs[5]);
        const uint8_t* data=current.data();
        auto* tex=gs_texture_create(W,H,GS_RGBA,1,&data,0);
        auto disabled=draw(shader,b,tex,gs_texrender_get_texture(a),7,0.f,0.f);
        disabled_delta=difference(disabled,current);
        gs_texture_destroy(tex);
     }
     std::cout<<"PFRAME_GPU_COMPILED=1"
              <<" GHOST_VS_LIVE="<<ghost
              <<" RESET_VS_LIVE="<<clean_delta
              <<" DISABLED_VS_LIVE="<<disabled_delta
              <<" GHOST_FRAME_EVOLUTION="<<difference(at4,at5)
              <<" ALPHA_OK="<<alpha_good<<"\n";
     success=ghost>50 && clean_delta<5 && disabled_delta<5 &&
             difference(at4,at5)>50 && alpha_good;
     gs_texture_destroy(prev);
     gs_texrender_destroy(a);
     gs_texrender_destroy(b);
 }
 if(shader)gs_effect_destroy(shader);
 obs_leave_graphics();
 obs_shutdown();
 if(success)std::cout<<"PASS: actual OBS shader compiles, carries damaged P-frame history and resets to current frame\n";
 return success?0:1;
}
