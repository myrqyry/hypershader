// Render the actual native BufferB/Draw techniques against controlled textures.
#include <obs.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <vector>
#include <cstdio>
#include <cmath>
#include <cstring>
constexpr unsigned W=64,H=48;
using Frame=std::vector<uint8_t>;
static gs_texture_t* texture(const Frame& frame) {
    const uint8_t* ptr=frame.data();
    return gs_texture_create(W,H,GS_RGBA,1,&ptr,0);
}
int main(int argc,char** argv) {
    if(argc!=2||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_video_info vi{};
    vi.graphics_module="libobs-opengl";vi.fps_num=30;vi.fps_den=1;
    vi.base_width=W;vi.base_height=H;vi.output_width=W;vi.output_height=H;
    vi.output_format=VIDEO_FORMAT_RGBA;vi.scale_type=OBS_SCALE_DISABLE;
    if(obs_reset_video(&vi)!=OBS_VIDEO_SUCCESS){obs_shutdown();return 3;}
    obs_enter_graphics();
    gs_set_cull_mode(GS_NEITHER);
    char* errors=nullptr;
    auto* effect=gs_effect_create_from_file(argv[1],&errors);
    if(errors){fprintf(stderr,"%s\n",errors);bfree(errors);}
    Frame fresh(W*H*4,0),history(W*H*4,0);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        const auto i=4*(y*W+x);
        fresh[i+3]=history[i+3]=255;
        if(x>=12&&x<20&&y>=16&&y<32)history[i]=240;
    }
    auto* input=texture(fresh);auto* old=texture(history);
    float vectors[32*18*4]{};
    for(unsigned i=0;i<32*18;++i){vectors[i*4]=-8.f/W;vectors[i*4+2]=1.f;}
    const uint8_t* bytes=reinterpret_cast<const uint8_t*>(vectors);
    auto* flow=gs_texture_create(32,18,GS_RGBA32F,1,&bytes,0);
    auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
    auto* stage=gs_stagesurface_create(W,H,GS_RGBA);
    auto draw=[&](float step,bool present,float tracking,float mix,bool clear,int frame,float dt) {
        Frame result;
        if(!effect||!input||!old||!flow||!target||!stage)return result;
        gs_texrender_reset(target);
        if(!gs_texrender_begin(target,W,H))return result;
        gs_projection_push();gs_matrix_push();gs_matrix_identity();gs_ortho(0,float(W),0,float(H),-100,100);
        gs_blend_state_push();gs_enable_blending(false);
        auto p=[&](const char* name){return gs_effect_get_param_by_name(effect,name);};
        gs_effect_set_texture(p("iBufferA"),input);
        gs_effect_set_texture(p("iBufferBPrevious"),old);
        gs_effect_set_texture(p("iSightFlow"),flow);
        gs_effect_set_int(p("iGraphFrame"),frame);
        gs_effect_set_float(p("iSightFlowPresent"),present?1.f:0.f);
        gs_effect_set_float(p("iSightFlowStep"),step);
        gs_effect_set_float(p("iTimeDelta"),dt);
        gs_effect_set_float(p("retention"),.2f);
        gs_effect_set_float(p("tracking"),tracking);
        gs_effect_set_float(p("effect_mix"),mix);
        gs_effect_set_bool(p("clear_history"),clear);
        while(gs_effect_loop(effect,"BufferB"))gs_draw_sprite(input,0,W,H);
        gs_blend_state_pop();gs_matrix_pop();gs_projection_pop();gs_texrender_end(target);
        gs_stage_texture(stage,gs_texrender_get_texture(target));
        uint8_t* data=nullptr;uint32_t pitch=0;
        if(gs_stagesurface_map(stage,&data,&pitch)){
            if(pitch>=W*4){result.resize(W*H*4);for(unsigned y=0;y<H;++y)memcpy(result.data()+y*W*4,data+y*pitch,W*4);}
            gs_stagesurface_unmap(stage);
        }
        return result;
    };
    auto red=[](const Frame& f,unsigned x){return f.size()==W*H*4?int(f[(24*W+x)*4]):-1;};
    auto plain=draw(0,true,1,1,false,2,0);
    auto moved=draw(1,true,1,1,false,2,0);
    auto zero=draw(1,true,0,1,false,2,0);
    auto faded=draw(1,true,1,1,false,2,.2f);
    bool stationary=red(plain,16)==240&&red(plain,24)==0;
    bool advected=red(moved,16)==0&&red(moved,24)==240;
    bool no_tracking=zero==plain;
    bool decay=std::abs(red(faded,24)-120)<=2;
    bool absent=draw(1,false,1,1,false,2,0)==fresh;
    bool cleared=draw(1,true,1,1,true,2,0)==fresh;
    bool initial=draw(1,true,1,1,false,0,0)==fresh;
    bool bypass=draw(1,true,1,0,false,2,0)==fresh;
    if(stage)gs_stagesurface_destroy(stage);
    if(target)gs_texrender_destroy(target);
    if(flow)gs_texture_destroy(flow);
    if(old)gs_texture_destroy(old);
    if(input)gs_texture_destroy(input);
    if(effect)gs_effect_destroy(effect);
    obs_leave_graphics();obs_shutdown();
    printf("SIGHT_FLOW_ECHO_GPU still=%d advected=%d zero=%d half_life=%d absent=%d clear=%d first=%d bypass=%d moved_red=%d\n",stationary,advected,no_tracking,decay,absent,cleared,initial,bypass,red(moved,24));
    return !(stationary&&advected&&no_tracking&&decay&&absent&&cleared&&initial&&bypass);
}
