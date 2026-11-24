// Real flow-spark shader techniques with controlled history and motion.
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
    Frame matte(W*H*4,0);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        size_t i=4*(y*W+x);matte[i]=x<W/2?255:0;matte[i+3]=255;
    }
    auto* mask=texture(matte);
    float vectors[32*18*4]{};
    for(unsigned i=0;i<32*18;++i){vectors[i*4]=-8.f/W;vectors[i*4+2]=1.f;}
    const uint8_t* bytes=reinterpret_cast<const uint8_t*>(vectors);
    auto* flow=gs_texture_create(32,18,GS_RGBA32F,1,&bytes,GS_DYNAMIC);
    auto* target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
    auto* stage=gs_stagesurface_create(W,H,GS_RGBA);
    auto draw=[&](float step,bool present,float tracking,float mix,bool clear,int frame,float dt,const char* technique="BufferB",float emission=0.f,int region=0,bool matte_present=true,bool depth_on=false,bool depth_present=true,float plane=.5f,int music=0,float amount=.75f,float pulse=0.f) {
        Frame result;
        if(!effect||!input||!old||!flow||!target||!stage)return result;
        gs_texrender_reset(target);
        if(!gs_texrender_begin(target,W,H))return result;
        gs_projection_push();gs_matrix_push();gs_matrix_identity();gs_ortho(0,float(W),0,float(H),-100,100);
        gs_blend_state_push();gs_enable_blending(false);
        auto p=[&](const char* name){return gs_effect_get_param_by_name(effect,name);};
        gs_effect_set_texture(p("image"),input);
        gs_effect_set_texture(p("hs_buffer_a"),input);
        gs_effect_set_texture(p("hs_buffer_b"),old);
        struct vec3 size;vec3_set(&size,W,H,float(W)/H);
        gs_effect_set_vec3(p("hs_resolution"),&size);
        gs_effect_set_float(p("hs_sight_flow_interval"),.05f);
        if(auto* a=p("music_response"))gs_effect_set_int(a,music);
        if(auto* a=p("music_amount"))gs_effect_set_float(a,amount);
        for(auto* name:{"audio_kick","audio_snare","audio_beat","audio_flux","audio_band0"})
            if(auto* a=p(name))gs_effect_set_float(a,pulse);
        if(auto* a=p("audio_envelope"))gs_effect_set_float(a,0);
        gs_effect_set_float(p("emission"),emission);
        gs_effect_set_float(p("spacing"),12.f);
        gs_effect_set_float(p("spark_size"),2.f);
        gs_effect_set_float(p("speed_threshold"),35.f);
        struct vec4 color;vec4_set(&color,1,0,0,1);
        gs_effect_set_vec4(p("spark_color"),&color);
        gs_effect_set_texture(p("hs_buffer_b_previous"),old);
        gs_effect_set_texture(p("hs_sight_flow"),flow);
        if(auto* param=p("hs_segmentation"))gs_effect_set_texture(param,mask);
        if(auto* param=p("hs_sight_matte_present"))gs_effect_set_float(param,matte_present?1.f:0.f);
        if(auto* param=p("spark_region"))gs_effect_set_int(param,region);
        // Left half is near (1); right half far (0).
        if(auto* param=p("hs_sight_depth"))gs_effect_set_texture(param,mask);
        if(auto* param=p("hs_sight_depth_present"))gs_effect_set_float(param,depth_present?1.f:0.f);
        if(auto* param=p("depth_occlusion"))gs_effect_set_bool(param,depth_on);
        if(auto* param=p("spark_depth"))gs_effect_set_float(param,plane);
        if(auto* param=p("depth_softness"))gs_effect_set_float(param,.05f);
        gs_effect_set_int(p("hs_graph_frame"),frame);
        gs_effect_set_float(p("hs_sight_flow_present"),present?1.f:0.f);
        gs_effect_set_float(p("hs_sight_flow_step"),step);
        gs_effect_set_float(p("hs_delta_time"),dt);
        gs_effect_set_float(p("retention"),.2f);
        gs_effect_set_float(p("tracking"),tracking);
        gs_effect_set_float(p("effect_mix"),mix);
        gs_effect_set_bool(p("clear_history"),clear);
        while(gs_effect_loop(effect,technique))gs_draw_sprite(input,0,W,H);
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
    auto seeds=draw(1,true,1,1,false,2,0,"BufferA",1);
    auto no_seeds=draw(0,true,1,1,false,2,0,"BufferA",1);
    auto no_emission=draw(1,true,1,1,false,2,0,"BufferA",0);
    auto seed_count=[](const Frame& f){size_t n=0;for(size_t i=0;i<f.size();i+=4)n+=f[i]>20;return n;};
    bool emitted=seed_count(seeds)>10&&seed_count(seeds)<W*H/2;
    bool step_gate=no_seeds==fresh&&no_emission==fresh;
    auto display=draw(0,true,1,1,false,2,0,"Draw");
    bool composited=red(display,16)>100&&red(display,24)==0;
    bool display_absent=draw(0,false,1,1,false,2,0,"Draw")==fresh;
    bool display_bypass=draw(0,true,1,0,false,2,0,"Draw")==fresh;
    auto front=draw(1,true,1,1,false,2,0,"BufferA",1,1);
    auto behind=draw(1,true,1,1,false,2,0,"BufferA",1,2);
    auto side=[](const Frame& f,bool right){
        size_t n=0;
        if(f.size()!=W*H*4)return n;
        for(unsigned y=0;y<H;++y)for(unsigned x=right?W/2+2:0;x<(right?W:W/2-2);++x)
            n+=f[(y*W+x)*4]>20;
        return n;
    };
    bool region_gate=side(front,false)>20&&side(front,true)==0&&
        side(behind,true)>20&&side(behind,false)==0;
    bool missing_matte=draw(1,true,1,1,false,2,0,"BufferA",1,1,false)==fresh&&
        draw(0,true,1,1,false,2,0,"Draw",0,2,false)==fresh;
    bool occlusion=draw(0,true,1,1,false,2,0,"Draw",0,2)==fresh&&
        red(draw(0,true,1,1,false,2,0,"Draw",0,1),16)>100;
    auto depth_seed=draw(1,true,1,1,false,2,0,"BufferA",1,0,true,true);
    bool depth_gate=side(depth_seed,false)==0&&side(depth_seed,true)>20;
    bool depth_hidden=draw(0,true,1,1,false,2,0,"Draw",0,0,true,true)==fresh;
    bool depth_missing=draw(1,true,1,1,false,2,0,"BufferA",1,0,true,true,false)==fresh&&
        draw(0,true,1,1,false,2,0,"BufferB",0,0,true,true,false)==fresh&&
        draw(0,true,1,1,false,2,0,"Draw",0,0,true,true,false)==fresh;
    bool depth_ignored=draw(0,true,1,1,false,2,0,"Draw",0,0,true,false,false)==display;
    bool combined=draw(1,true,1,1,false,2,0,"BufferA",1,1,true,true)==fresh&&
        draw(1,true,1,1,false,2,0,"BufferA",1,2,true,true)==depth_seed;
    auto soft=draw(1,true,1,1,false,2,0,"BufferA",.25f);
    auto pulse=draw(1,true,1,1,false,2,0,"BufferA",.25f,0,true,false,true,.5f,1,1,1);
    bool music=seed_count(pulse)>seed_count(soft)&&
         draw(1,true,1,1,false,2,0,"BufferA",.25f,0,true,false,true,.5f,1,1,0)==soft&&
         draw(1,true,1,1,false,2,0,"BufferA",.25f,0,true,false,true,.5f,0,1,1)==soft&&
         draw(1,true,1,1,false,2,0,"BufferA",.25f,0,true,false,true,.5f,1,0,1)==soft&&
         seed_count(draw(1,true,1,1,false,2,0,"BufferA",.25f,0,true,false,true,.5f,3,1,1))>seed_count(soft)&&
         seed_count(draw(1,true,1,1,false,2,0,"BufferA",.25f,0,true,false,true,.5f,4,1,1))>seed_count(soft);
    // Valid stationary vectors must not emit, even with full emission.
    for(unsigned i=0;i<32*18;++i)vectors[i*4]=0;
    gs_texture_set_image(flow,bytes,32*16,false);
    bool motion_gate=draw(1,true,1,1,false,2,0,"BufferA",1)==fresh;
    // Zero confidence must also prevent emission/advection.
    for(unsigned i=0;i<32*18;++i){vectors[i*4]=-8.f/W;vectors[i*4+2]=0;}
    gs_texture_set_image(flow,bytes,32*16,false);
    bool confidence_gate=draw(1,true,1,1,false,2,0,"BufferA",1)==fresh&&
        draw(1,true,1,1,false,2,0)==plain;
    if(stage)gs_stagesurface_destroy(stage);
    if(target)gs_texrender_destroy(target);
    if(flow)gs_texture_destroy(flow);
    if(old)gs_texture_destroy(old);
    if(mask)gs_texture_destroy(mask);
    if(input)gs_texture_destroy(input);
    if(effect)gs_effect_destroy(effect);
    obs_leave_graphics();obs_shutdown();
    printf("SIGHT_FLOW_SPARKS_GPU still=%d advected=%d zero=%d half_life=%d absent=%d clear=%d first=%d bypass=%d moved_red=%d\n",stationary,advected,no_tracking,decay,absent,cleared,initial,bypass,red(moved,24));
    printf("SPARKS_SEED emitted=%d count=%zu gate=%d composite=%d absent=%d bypass=%d motion=%d confidence=%d\n",emitted,seed_count(seeds),step_gate,composited,display_absent,display_bypass,motion_gate,confidence_gate);
    printf("SPARKS_REGION front=%d back=%d gated=%d absent=%d occlusion=%d\n",int(side(front,false)),int(side(behind,true)),region_gate,missing_matte,occlusion);
    printf("SPARKS_DEPTH gate=%d hidden=%d missing=%d ignored=%d combined=%d\n",depth_gate,depth_hidden,depth_missing,depth_ignored,combined);
    printf("SPARKS_MUSIC=%d quiet=%zu pulse=%zu\n",music,seed_count(soft),seed_count(pulse));
    return !(music&&depth_gate&&depth_hidden&&depth_missing&&depth_ignored&&combined&&region_gate&&missing_matte&&occlusion&&stationary&&advected&&no_tracking&&decay&&absent&&cleared&&initial&&bypass&&emitted&&step_gate&&composited&&display_absent&&display_bypass&&motion_gate&&confidence_gate);
}
