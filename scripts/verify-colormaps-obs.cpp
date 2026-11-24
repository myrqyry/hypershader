// Real libobs/OpenGL colormap pixel readback, isolated under Xvfb.
// Compile-only tests can pass shaders that render nothing; this verifies actual output.
#include <obs.h>
#include <graphics/graphics.h>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

constexpr unsigned W=64, H=64;
using Pixel=std::array<unsigned char,4>;

Pixel render(const char *filename) {
    gs_set_cull_mode(GS_NEITHER);
    Pixel result{};
    char *error=nullptr;
    auto *effect=gs_effect_create_from_file(filename,&error);
    if(error){ std::cerr<<"Compile error "<<filename<<": "<<error<<"\n"; bfree(error); }
    if(!effect)return result;
    auto *target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
    auto *stage=gs_stagesurface_create(W,H,GS_RGBA);
    if(target && stage && gs_texrender_begin(target,W,H)) {
        gs_projection_push(); gs_matrix_push(); gs_matrix_identity();
        gs_ortho(0,float(W),0,float(H),-100,100);
        while(gs_effect_loop(effect,"Draw")) gs_draw_sprite(nullptr,0,W,H);
        gs_matrix_pop(); gs_projection_pop(); gs_texrender_end(target);
        if(auto *tex=gs_texrender_get_texture(target)) {
            gs_stage_texture(stage,tex);
            unsigned char* data=nullptr; uint32_t pitch=0;
            if(gs_stagesurface_map(stage,&data,&pitch)) {
                for(int i=0;i<4;++i)result[i]=data[(H/2)*pitch+(W/2)*4+i];
                gs_stagesurface_unmap(stage);
            }
        }
    }
    if(stage)gs_stagesurface_destroy(stage);
    if(target)gs_texrender_destroy(target);
    gs_effect_destroy(effect);
    return result;
}
int main(int argc,char**argv){
    if(argc!=5)return 2;
    if(!obs_startup("en-US",nullptr,nullptr))return 1;
    obs_video_info info{};
    info.graphics_module="libobs-opengl";
    info.fps_num=30;info.fps_den=1;
    info.base_width=W;info.base_height=H;
    info.output_width=W;info.output_height=H;
    info.output_format=VIDEO_FORMAT_RGBA;
    info.scale_type=OBS_SCALE_DISABLE;
    if(obs_reset_video(&info)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 1;}
    std::array<Pixel,4> pixels{};
    obs_enter_graphics();
    for(int i=0;i<4;++i) pixels[i]=render(argv[i+1]);
    obs_leave_graphics();
    obs_shutdown();
    // Regression colors measured at t=0.5 from the pinned upstream formulas.
    // Allow a little backend rounding variation, but reject wrong color math.
    constexpr int baseline[4][3]={{128,255,128},{255,255,191},{124,72,131},{137,0,104}};
    bool ok=true;
    for(int i=0;i<4;++i) {
        auto c=pixels[i];
        std::cout<<"COLORMAP_NATIVE_PIXEL "<<argv[i+1]<<" = "
                 <<int(c[0])<<","<<int(c[1])<<","<<int(c[2])<<","<<int(c[3])<<"\n";
        // Opaque, with at least one channel nonzero (not an empty render).
        if(c[3]<253 || (int(c[0])+int(c[1])+int(c[2])<15))ok=false;
        for(int ch=0;ch<3;++ch)if(std::abs(int(c[ch])-baseline[i][ch])>10) {
            std::cerr<<"Unexpected reference color "<<i<<" channel "<<ch<<"\n";
            ok=false;
        }
        for(int j=0;j<i;++j){
            int sum=0;
            for(int ch=0;ch<3;++ch)sum+=std::abs(int(c[ch])-int(pixels[j][ch]));
            if(sum<25){std::cerr<<"Duplicate/near-identical colormap output "<<i<<","<<j<<"\n";ok=false;}
        }
    }
    std::cout<<(ok?"PASS":"FAIL")<<": 4 distinct opaque native libobs colormap pixels\n";
    return ok?0:1;
}
