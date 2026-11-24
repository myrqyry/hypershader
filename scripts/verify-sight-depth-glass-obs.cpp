#include <obs.h>
#include <graphics/graphics.h>
#include <vector>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>
constexpr unsigned W=64,H=48;
using Frame=std::vector<uint8_t>;
static gs_texture_t* texture(const Frame& f){
    const uint8_t* p=f.data();return gs_texture_create(W,H,GS_RGBA,1,&p,GS_DYNAMIC);
}
int main(int argc,char** argv){
    if(argc!=2||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_video_info vi{};vi.graphics_module="libobs-opengl";vi.fps_num=30;vi.fps_den=1;
    vi.base_width=W;vi.base_height=H;vi.output_width=W;vi.output_height=H;
    vi.output_format=VIDEO_FORMAT_RGBA;vi.scale_type=OBS_SCALE_DISABLE;
    if(obs_reset_video(&vi)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 3;}
    obs_enter_graphics();gs_set_cull_mode(GS_NEITHER);
    char* err=nullptr;auto* e=gs_effect_create_from_file(argv[1],&err);
    if(err){fprintf(stderr,"%s\n",err);bfree(err);}
    Frame source(W*H*4),depth(W*H*4),matte(W*H*4),strip(W*H*4);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        size_t i=4*(y*W+x);
        source[i]=x*4;source[i+1]=y*5;source[i+2]=x<W/2?20:230;source[i+3]=y<4?128:255;
        depth[i]=x<W/2?64:230;depth[i+3]=255;
        matte[i]=x>=W/2?255:0;matte[i+3]=255;
        strip[i]=(x>=27&&x<=29)?230:64;strip[i+3]=255;
    }
    auto* input=texture(source);auto* map=texture(depth);auto* mask=texture(matte);
    auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);auto* stage=gs_stagesurface_create(W,H,GS_RGBA);
    auto draw=[&](bool present=true,float strength=12,float mix=1,bool protect=false,bool matte_present=true,bool debug=false,float center=.25f,float width=.15f,float time=0.f,int music=0,float amount=.75f,float pulse=0.f){
        Frame result;if(!e||!input||!map||!mask||!stage||!target)return result;
        gs_texrender_reset(target);if(!gs_texrender_begin(target,W,H))return result;
        gs_projection_push();gs_matrix_push();gs_matrix_identity();gs_ortho(0,float(W),0,float(H),-100,100);
        gs_blend_state_push();gs_enable_blending(false);
        auto p=[&](const char* name){return gs_effect_get_param_by_name(e,name);};
        auto f=[&](const char* name,float v){if(auto* a=p(name))gs_effect_set_float(a,v);};
        auto b=[&](const char* name,bool v){if(auto* a=p(name))gs_effect_set_bool(a,v);};
        gs_effect_set_texture(p("image"),input);
        if(auto* a=p("iSightDepth"))gs_effect_set_texture(a,map);
        if(auto* a=p("iSegmentation"))gs_effect_set_texture(a,mask);
        struct vec3 size;vec3_set(&size,W,H,float(W)/H);
        if(auto* a=p("iResolution"))gs_effect_set_vec3(a,&size);
        f("iSightDepthPresent",present);f("iSightMattePresent",matte_present);
        f("audio_kick",pulse);f("audio_snare",pulse);f("audio_beat",pulse);f("audio_envelope",0);f("audio_band0",pulse);f("audio_flux",pulse);
        if(auto* a=p("music_response"))gs_effect_set_int(a,music);
        f("music_amount",amount);
        f("iTime",time);f("glass_strength",strength);f("wave_length",48);f("wave_speed",.25f);
        f("depth_center",center);f("depth_width",width);f("edge_tolerance",.08f);
        f("effect_mix",mix);b("protect_subject",protect);b("debug_band",debug);
        while(gs_effect_loop(e,"Draw"))gs_draw_sprite(input,0,W,H);
        gs_blend_state_pop();gs_matrix_pop();gs_projection_pop();gs_texrender_end(target);
        gs_stage_texture(stage,gs_texrender_get_texture(target));
        uint8_t* data=nullptr;uint32_t pitch=0;
        if(gs_stagesurface_map(stage,&data,&pitch)){
            if(pitch>=W*4){result.resize(W*H*4);for(unsigned y=0;y<H;++y)memcpy(result.data()+y*W*4,data+y*pitch,W*4);}
            gs_stagesurface_unmap(stage);
        }return result;
    };
    auto equal=[](const Frame& a,const Frame& b,unsigned x,unsigned y){
        if(a.size()!=W*H*4||b.size()!=a.size())return false;
        size_t i=4*(y*W+x);for(unsigned c=0;c<4;++c)if(std::abs(int(a[i+c])-int(b[i+c]))>1)return false;return true;
    };
    auto out=draw();bool warp=!equal(out,source,8,12)&&out.size()==source.size();
    bool band=true,edge=true,alpha=true;
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        if(x>=32)band&=equal(out,source,x,y);
        if(out.size()!=source.size())alpha=false;else alpha&=out[(y*W+x)*4+3]==source[(y*W+x)*4+3];
    }
    // At y=12 the positive horizontal wave attempts to cross x=32.
    edge=equal(out,source,27,12);
    bool absent=draw(false)==source,zero=draw(true,0)==source,dry=draw(true,12,0)==source;
    auto preview=draw(true,12,1,false,true,true);
    bool debug=preview.size()==source.size()&&preview[(12*W+8)*4]>245&&preview[(12*W+50)*4]<5;
    bool animated=draw(true,12,1,false,true,false,.25f,.15f,2.f)!=out;
    auto music_idle=draw(true,12,1,false,true,false,.25f,.15f,0,1,.75f,0);
    auto music_peak=draw(true,12,1,false,true,false,.25f,.15f,0,1,.75f,1);
    bool music=music_idle==out&&music_peak!=out&&equal(music_peak,source,50,12)&&
         draw(true,12,1,false,true,false,.25f,.15f,0,0,.75f,1)==out&&
         draw(true,12,1,false,true,false,.25f,.15f,0,1,0,1)==out&&
         draw(true,12,1,false,true,false,.25f,.15f,0,3,.75f,1)!=out&&
         draw(true,12,1,false,true,false,.25f,.15f,0,4,.75f,1)!=out;
    // Flat far depth: matte protection, not depth, must block subject and crossings.
    Frame flat=depth;for(size_t i=0;i<flat.size();i+=4)flat[i]=64;
    gs_texture_set_image(map,flat.data(),W*4,false);
    auto unprotected=draw();auto protected_frame=draw(true,12,1,true);
    bool subject=equal(protected_frame,source,40,12)&&equal(protected_frame,source,27,12)&&
        !equal(unprotected,source,27,12)&&!equal(protected_frame,source,8,12);
    bool matte_missing=draw(true,12,1,true,false)==source;
    gs_texture_set_image(map,strip.data(),W*4,false);
    // Both endpoints far; midpath crosses a near strip, and must reject.
    bool midpath=equal(draw(),source,22,12)&&!equal(unprotected,source,22,12);
    gs_stagesurface_destroy(stage);gs_texrender_destroy(target);
    gs_texture_destroy(mask);gs_texture_destroy(map);gs_texture_destroy(input);if(e)gs_effect_destroy(e);
    obs_leave_graphics();obs_shutdown();
    printf("SIGHT_DEPTH_GLASS_GPU warp=%d band=%d edge=%d alpha=%d absent=%d zero=%d dry=%d debug=%d animated=%d subject=%d matte_missing=%d midpath=%d\n",warp,band,edge,alpha,absent,zero,dry,debug,animated,subject,matte_missing,midpath);
    printf("GLASS_MUSIC=%d\n",music);
    return !(music&&warp&&band&&edge&&alpha&&absent&&zero&&dry&&debug&&animated&&subject&&matte_missing&&midpath);
}
