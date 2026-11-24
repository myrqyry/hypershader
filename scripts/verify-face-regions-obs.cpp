#include <obs.h>
#include <graphics/graphics.h>
#include <util/bmem.h>
#include <array>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>
constexpr unsigned W=256,H=128;
using Points=std::array<float,468*4>;
using Frame=std::vector<unsigned char>;
static Points mesh(bool blink,bool far_eye,bool rotated){
    Points p{};
    auto put=[&](int id,float x,float y){
        if(rotated){float dx=x-W*.5f,dy=y-H*.5f;x=W*.5f-dy;y=H*.5f+dx;}
        p[id*4]=x/W;p[id*4+1]=y/H;p[id*4+3]=1;
    };
    const int right[]={133,158,159,160,33,144,145,153};
    const int left[]={362,385,386,387,263,373,374,380};
    for(int e=0;e<2;++e){
        const int* ids=e?left:right;float cx=e?180:76;
        float rx=far_eye&&e?5:25,ry=blink?0:10;
        for(int i=0;i<8;++i){float a=3.14159265f+i*3.14159265f/4;
            float x=cx+rx*std::cos(a),y=64+ry*std::sin(a);
            put(ids[i],x,y);
        }
    }
    return p;
}
static Frame draw(gs_effect_t* fx,const Points& points,bool present){
    auto* rt=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
    auto* stage=gs_stagesurface_create(W,H,GS_RGBA);
    const uint8_t* src=reinterpret_cast<const uint8_t*>(points.data());
    auto* lm=gs_texture_create(468,1,GS_RGBA32F,1,&src,0);
    Frame out;
    if(rt&&stage&&lm&&gs_texrender_begin(rt,W,H)){
        vec4 clear{};gs_clear(GS_CLEAR_COLOR,&clear,0,0);
        gs_blend_state_push();gs_enable_blending(false);gs_set_cull_mode(GS_NEITHER);
        gs_projection_push();gs_matrix_push();gs_matrix_identity();gs_ortho(0,W,0,H,-100,100);
        vec3 res;vec3_set(&res,W,H,float(W)/H);
        gs_effect_set_vec3(gs_effect_get_param_by_name(fx,"hs_resolution"),&res);
        gs_effect_set_texture(gs_effect_get_param_by_name(fx,"hs_landmarks"),lm);
        gs_effect_set_float(gs_effect_get_param_by_name(fx,"hs_face_present"),present?1:0);
        while(gs_effect_loop(fx,"Draw"))gs_draw_sprite(nullptr,0,W,H);
        gs_matrix_pop();gs_projection_pop();gs_blend_state_pop();gs_texrender_end(rt);
        gs_stage_texture(stage,gs_texrender_get_texture(rt));
        uint8_t* data=nullptr;uint32_t stride=0;
        if(gs_stagesurface_map(stage,&data,&stride)){
            out.resize(W*H*4);for(unsigned y=0;y<H;++y)memcpy(out.data()+y*W*4,data+y*stride,W*4);
            gs_stagesurface_unmap(stage);
        }
    }
    if(lm)gs_texture_destroy(lm);if(stage)gs_stagesurface_destroy(stage);if(rt)gs_texrender_destroy(rt);
    return out;
}
static int at(const Frame& f,unsigned x,unsigned y,unsigned c){return f.empty()?-1:f[(y*W+x)*4+c];}
int main(int argc,char**argv){
    if(argc!=2||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_video_info v{};v.graphics_module="libobs-opengl";v.fps_num=30;v.fps_den=1;
    v.output_width=v.base_width=W;v.output_height=v.base_height=H;v.output_format=VIDEO_FORMAT_RGBA;v.scale_type=OBS_SCALE_DISABLE;
    if(obs_reset_video(&v)!=OBS_VIDEO_SUCCESS)return 3;
    obs_enter_graphics();char* err=nullptr;
    auto* fx=gs_effect_create_from_file(argv[1],&err);
    if(err){fprintf(stderr,"%s\n",err);bfree(err);}
    if(!fx){obs_leave_graphics();obs_shutdown();return 4;}
    auto open=draw(fx,mesh(false,false,false),true),closed=draw(fx,mesh(true,false,false),true);
    auto side=draw(fx,mesh(false,true,false),true),lost=draw(fx,mesh(false,false,false),false);
    auto turned=draw(fx,mesh(false,false,true),true);
    printf("PIXELS n=%zu center=%d,%d,%d left=%d,%d,%d right=%d,%d,%d turned=%d,%d\n",open.size(),at(open,128,64,0),at(open,128,64,1),at(open,128,64,2),at(open,76,64,0),at(open,76,64,1),at(open,76,64,2),at(open,180,64,0),at(open,180,64,1),at(open,180,64,2),at(turned,128,12,0),at(turned,128,116,1));
    bool region=at(open,76,64,0)>200&&at(open,180,64,1)>200&&at(open,128,64,0)==0&&at(open,76,42,0)==0;
    bool blink=at(closed,76,64,0)==0&&at(closed,180,64,1)==0;
    bool occlusion=at(side,76,64,0)>200&&at(side,180,64,1)==0;
    bool missing=at(lost,76,64,0)==0&&at(lost,180,64,1)==0;
    bool rotation=at(turned,128,12,0)>200&&at(turned,128,116,1)>200;
    bool tint=std::abs(at(open,128,64,2)-128)<=2;
    gs_effect_destroy(fx);obs_leave_graphics();obs_shutdown();
    printf("FACE_REGIONS_GPU mask=%d blink=%d occlusion=%d missing=%d rotation=%d luminance=%d\n",region,blink,occlusion,missing,rotation,tint);
    return !(region&&blink&&occlusion&&missing&&rotation&&tint);
}
