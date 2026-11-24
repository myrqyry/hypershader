// Pure naming contract; labels change but stable storage and path IDs do not.
#include "../src/shader/shader_display_name.h"
#include <iostream>
#include <string>
struct Case { const char* path; const char* expected; };
int main() {
    const Case cases[] = {
        {"root/plasma.effect","Source / Plasma"},
        {"root/blurred_gradient.effect","Source / Blurred Gradient"},
        {"root/bubble_physics.effect","Source / Bubble Physics"},
        {"root/spectrum_bars.effect","Source / Spectrum Bars"},
        {"root/numbers_tunnel.effect","Source / Numbers Tunnel"},
        {"root/pframe_drift.effect","Filter / P-Frame Drift"},
        {"root/Gaussian_Billboard.effect","Filter / Gaussian Billboard"},
        {"root/ink_reaction_buffers.effect","Filter / Ink Reaction Buffers"},
        {"root/rikiya/rikiya_202401.effect","Filter / Rikiya / 202401"},
        {"root/collection/noise.effect","Filter / Collection / Noise"},
        {"root/source.ambient.effect","Source / Ambient"},
        {"root/transition.crossfade.effect","Transition / Crossfade"},
        {"root/filter.bloom.effect","Filter / Bloom"},
        {"root/source/atmosphere.effect","Source / Atmosphere"},
        {"root/transition/wipe.effect","Transition / Wipe"},
        {"root/filter/glow.effect","Filter / Glow"},
        {"source.analog.effect","Source / Analog"},
        {"root/source.effect","Filter / Source"},
        {"root/source_file.effect","Filter / Source File"},
        {"root/3d_mesh_gallery.effect","Source / 3D Mesh Gallery"},
        {"root/ps1_filter.effect","Filter / PS1 Filter"},
        {"root/crt_retro.effect","Filter / CRT Retro"},
        {"root/siren_video_filter.effect","Filter / SIREN Video Filter"},
        {"root/filter.lostjared_opal_ferrofluid.effect","Filter / LostJared Opal Ferrofluid"},
        {"root/filter.vidicon_afterglow.effect","Filter / Vidicon Afterglow"},
        {"root/noisemaker_prismatic_aberration.effect","Filter / NoiseMaker Prismatic Aberration"},
    };
    bool good=true;
    for(const auto& c:cases){
        char text[512];
        const bool ok=hs_shader_display_label(text,sizeof(text),c.path);
        bool pass=ok && std::string(text)==c.expected;
        good &= pass;
        if(!pass)std::cerr<<"FAIL "<<c.path<<" expected "<<c.expected
                          <<" got "<<(ok?text:"<invalid>")<<"\n";
    }
    const struct { const char *relative, *file, *expected; } titled[]={
        {"root/3d_showcase.effect","data/shaders/root/3d_showcase.effect",
         "Source / 3D Effects Showcase"},
        {"root/filter.lostjared_opal_ferrofluid.effect",
         "data/shaders/root/filter.lostjared_opal_ferrofluid.effect",
         "Filter / LostJared / Opal Ferrofluid"},
        {"root/Gaussian_Billboard.effect","data/shaders/root/Gaussian_Billboard.effect",
         "Filter / Gaussian Billboard"},
        {"root/fun_face.effect","data/shaders/root/fun_face.effect",
         "Filter / Fun Face"}, // its first @label belongs to a parameter
        {"root/plasma.effect","/not/a/file.effect","Source / Plasma"}
    };
    for(const auto &c:titled){
        char displayed[512];
        const bool ok=hs_shader_display_label_from_file(
            displayed,sizeof(displayed),c.relative,c.file);
        const bool pass=ok && std::string(displayed)==c.expected;
        if(!pass)std::cerr<<"FAIL authored title "<<c.relative
                          <<" expected "<<c.expected<<" got "
                          <<(ok?displayed:"<invalid>")<<"\n";
        good &= pass;
    }
    const struct {const char* role; const char* label; bool expected;} priorities[]={
        {"filter","Filter / Vidicon Afterglow",true},
        {"source","Filter / Vidicon Afterglow",false},
        {"source","Source / Blurred Gradient",true},
        {"transition","Transition / Iris Wipe",true},
        {"filter","Transition / Iris Wipe",false},
        {"transition","Source / 3D Mesh Gallery",false},
        {"filter","Filter / Rikiya / 202401",true},
    };
    for(const auto& caseRole:priorities){
        bool actual=hs_shader_role_is_preferred(caseRole.label,caseRole.role);
        if(actual!=caseRole.expected)
            std::cerr<<"ROLE_PRIORITY_ERROR "<<caseRole.role<<" "<<caseRole.label<<"\n";
        good &= actual==caseRole.expected;
    }
    char tiny[3]={'a','b','c'};
    const bool truncation=!hs_shader_display_label(tiny,sizeof(tiny),"root/plasma.effect")
        && tiny[sizeof(tiny)-1]==0;
    good &= truncation;
    std::cout<<"SHADER_NAMES_CONTRACT count="<<std::size(cases)+std::size(titled)+std::size(priorities)
             <<" truncation="<<truncation<<" PASS="<<good<<"\n";
    return good?0:1;
}
