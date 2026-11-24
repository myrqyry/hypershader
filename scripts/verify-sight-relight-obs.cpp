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
    Frame source(W*H*4),depth(W*H*4),matte(W*H*4);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        size_t i=4*(y*W+x);
        source[i]=source[i+1]=source[i+2]=100;source[i+3]=y<4?128:255;
        depth[i]=uint8_t(x<W/2?70+x*4:70+(W-1-x)*4);depth[i+3]=255;
        matte[i]=x>=W/2?255:0;matte[i+3]=255;
    }
    auto* input=texture(source);auto* map=texture(depth);auto* mask=texture(matte);
    auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);auto* stage=gs_stagesurface_create(W,H,GS_RGBA);
    auto draw=[&](bool present=true,float x=1.0f,int mode=0,int region=0,bool matte_present=true,float mix=1.f,int debug=0,float gain=1.f,float radius=.8f,float ambient=.4f,float height=.35f,int color=0,float band_center=.5f,float band_width=1.f,float specular=0.f,float fill=0.f,float rim=0.f,float shadow=0.f,float ao=0.f){
        Frame result;if(!e||!input||!map||!mask||!stage||!target)return result;
        gs_texrender_reset(target);if(!gs_texrender_begin(target,W,H))return result;
        gs_projection_push();gs_matrix_push();gs_matrix_identity();gs_ortho(0,float(W),0,float(H),-100,100);
        gs_blend_state_push();gs_enable_blending(false);
        auto p=[&](const char* name){return gs_effect_get_param_by_name(e,name);};
        auto f=[&](const char* name,float v){if(auto* a=p(name))gs_effect_set_float(a,v);};
        gs_effect_set_texture(p("image"),input);
        if(auto* a=p("iSightDepth"))gs_effect_set_texture(a,map);
        if(auto* a=p("iSegmentation"))gs_effect_set_texture(a,mask);
        struct vec3 size;vec3_set(&size,W,H,float(W)/H);
        if(auto* a=p("iResolution"))gs_effect_set_vec3(a,&size);
        f("iSightDepthPresent",present);f("iSightMattePresent",matte_present);
        auto i=[&](const char* name,int v){if(auto* a=p(name))gs_effect_set_int(a,v);};
        auto rgb=[&](const char* name,float r,float g,float b){if(auto* a=p(name)){struct vec3 c;vec3_set(&c,r,g,b);gs_effect_set_vec3(a,&c);}};
        i("light_mode",mode);f("light_x",x);f("light_y",.5f);f("light_height",height);
        f("light_radius",radius);f("key_strength",gain);f("ambient",ambient);
        rgb("key_color",1,color?0.15f:1.0f,color?0.1f:1.0f);rgb("fill_color",1,1,1);rgb("rim_color",1,1,1);
        f("fill_strength",fill);f("rim_strength",rim);f("specular_strength",specular);f("specular_power",24);
        f("shadow_strength",shadow);f("shadow_radius",24);f("shadow_softness",.035f);f("shadow_bias",.015f);
        f("ao_strength",ao);f("ao_radius",12);f("ao_bias",.015f);
        f("relief",.55f);f("normal_radius",2);f("edge_tolerance",.10f);
        i("region",region);f("depth_center",band_center);f("depth_width",band_width);
        f("effect_mix",mix);i("debug_view",debug);
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
    auto pixel=[](const Frame& f,unsigned x,unsigned y,unsigned c=0){
        return f.size()==W*H*4?int(f[(y*W+x)*4+c]):-999;
    };
    auto right=draw(true,1),left=draw(true,0);
    bool direction=pixel(right,48,24)>pixel(right,16,24)+12 &&
        pixel(left,16,24)>pixel(left,48,24)+12;
    bool alpha=true;for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)
        alpha&=pixel(right,x,y,3)==pixel(source,x,y,3);
    bool absent=draw(false)==source, dry=draw(true,1,0,0,true,0)==source;
    auto normal=draw(true,1,0,0,true,1,1);
    bool normals=pixel(normal,16,24)<115&&pixel(normal,48,24)>140;
    auto colored=draw(true,1,0,0,true,1,0,1,.8f,.4f,.35f,1);
    bool tint=pixel(colored,48,24,0)>pixel(colored,48,24,1)+25 &&
        pixel(colored,48,24,0)>pixel(colored,48,24,2)+25;
    auto filled=draw(true,1,0,0,true,1,0,1,.8f,.4f,.35f,0,.5f,1,0,.7f);
    bool fill=pixel(filled,16,24)>pixel(right,16,24)+20;
    auto rimmed=draw(true,1,0,0,true,1,0,1,.8f,.4f,.35f,0,.5f,1,0,0,1);
    bool rim=pixel(rimmed,16,24)>pixel(right,16,24)+2;
    Frame flat=depth;for(size_t j=0;j<flat.size();j+=4)flat[j]=128;
    gs_texture_set_image(map,flat.data(),W*4,false);
    auto point=draw(true,.25f,1,0,true,1,0,1,.35f);
    bool falloff=pixel(point,16,24)>pixel(point,52,24)+15;
    auto highlight=draw(true,.5f,0,0,true,1,0,1,.8f,.4f,.35f,0,.5f,1,.2f);
    auto no_highlight=draw(true,.5f);
    bool spec=pixel(highlight,32,24)>pixel(no_highlight,32,24)+25;
    auto subject=draw(true,1,0,1);
    auto background=draw(true,1,0,2);
    bool regions=equal(subject,source,12,24)&&!equal(subject,source,52,24)&&
        equal(background,source,52,24)&&!equal(background,source,12,24);
    bool missing_matte=draw(true,1,0,1,false)==source;
    Frame step=depth;for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)
        step[(y*W+x)*4]=x<W/2?40:220;
    gs_texture_set_image(map,step.data(),W*4,false);
    auto selected=draw(true,.5f,0,0,true,1,0,1,.8f,.4f,.35f,0,.16f,.1f);
    bool band=!equal(selected,source,10,24)&&equal(selected,source,54,24);
    auto edge=draw(true,1,0,0,true,1,1);
    bool edge_protected=std::abs(pixel(edge,31,24)-128)<8&&std::abs(pixel(edge,32,24)-128)<8;
    // Thin foreground bar, otherwise flat far receiver. Light direction
    // must determine the shadow side, not just the distance to the bar.
    Frame bar=depth;
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)
        bar[(y*W+x)*4]=x>=30&&x<=33?220:40;
    gs_texture_set_image(map,bar.data(),W*4,false);
    auto cast=[&](float x,float shadow,float ao,int debug=0,int mode=0){
        return draw(true,x,mode,0,true,1,debug,1,.8f,.4f,.15f,0,.5f,1,0,0,0,shadow,ao);
    };
    auto right_dry=cast(1,0,0), right_shadow=cast(1,1,0);
    auto left_dry=cast(0,0,0), left_shadow=cast(0,1,0);
    bool shadow_direction=pixel(right_dry,23,24)-pixel(right_shadow,23,24)>20 &&
        equal(right_dry,right_shadow,41,24) &&
        pixel(left_dry,41,24)-pixel(left_shadow,41,24)>20 &&
        equal(left_dry,left_shadow,23,24);
    bool shadow_receiver=equal(right_dry,right_shadow,31,24)&&equal(right_dry,right_shadow,4,24);
    auto point_dry=cast(.42f,0,0,0,1),point_shadow=cast(.42f,1,0,0,1);
    bool point_limit=equal(point_dry,point_shadow,23,24);
    auto ao_only=cast(.5f,0,1),ao_dry=cast(.5f,0,0);
    bool ao_local=pixel(ao_dry,26,24)-pixel(ao_only,26,24)>5 &&
        pixel(ao_dry,37,24)-pixel(ao_only,37,24)>5 &&
        equal(ao_dry,ao_only,5,24)&&equal(ao_dry,ao_only,31,24);
    auto shadow_preview=cast(1,1,0,4),ao_preview=cast(.5f,0,1,5);
    bool visibility_previews=pixel(shadow_preview,23,24)<200&&pixel(shadow_preview,41,24)>245 &&
        pixel(ao_preview,26,24)<230&&pixel(ao_preview,5,24)>245;
    bool shadows_alpha=true;
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)
        shadows_alpha&=pixel(right_shadow,x,y,3)==pixel(source,x,y,3)&&pixel(ao_only,x,y,3)==pixel(source,x,y,3);
    gs_texture_set_image(map,flat.data(),W*4,false);
    bool flat_safe=cast(1,1,1)==cast(1,0,0);
    // A uniformly sloped plane should not self-occlude.
    Frame ramp=depth;
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)ramp[(y*W+x)*4]=40+x*2;
    gs_texture_set_image(map,ramp.data(),W*4,false);
    auto ramp_dry=cast(1,0,0),ramp_ao=cast(1,0,1);
    bool plane_safe=equal(ramp_dry,ramp_ao,24,24)&&equal(ramp_dry,ramp_ao,40,24);
    bool shadow_checks=shadow_direction&&shadow_receiver&&point_limit&&ao_local&&visibility_previews&&shadows_alpha&&flat_safe&&plane_safe;
    printf("SIGHT_RELIGHT_OCCLUSION direction=%d receiver=%d point_limit=%d ao_local=%d previews=%d alpha=%d flat=%d plane=%d PASS=%d\n",
        shadow_direction,shadow_receiver,point_limit,ao_local,visibility_previews,shadows_alpha,flat_safe,plane_safe,shadow_checks);
    gs_stagesurface_destroy(stage);gs_texrender_destroy(target);
    gs_texture_destroy(mask);gs_texture_destroy(map);gs_texture_destroy(input);if(e)gs_effect_destroy(e);
    obs_leave_graphics();obs_shutdown();
    bool pass=direction&&alpha&&absent&&dry&&normals&&falloff&&regions&&missing_matte&&edge_protected&&tint&&fill&&rim&&spec&&band&&shadow_checks;
    printf("SIGHT_RELIGHT_GPU direction=%d alpha=%d absent=%d dry=%d normals=%d falloff=%d regions=%d missing_matte=%d edge=%d tint=%d fill=%d rim=%d spec=%d band=%d PASS=%d\n",
        direction,alpha,absent,dry,normals,falloff,regions,missing_matte,edge_protected,tint,fill,rim,spec,band,pass);
    return pass?0:1;
}
