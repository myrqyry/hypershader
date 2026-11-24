#include "interop/sight_text_mask.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
static uint8_t mask[HS_TEXT_EDGE*HS_TEXT_EDGE];
static int16_t scratch[HS_TEXT_EDGE*(HS_TEXT_EDGE+1)];
int main(void) {
    sight_text_v1 s={0};
    s.abi_version=SIGHT_TEXT_ABI_V1;s.struct_size=sizeof(s);
    s.source_width=800;s.source_height=400;s.sequence=1;s.count=2;
    s.lines[0]=(sight_text_line_v1){.25f,.25f,.5f,.5f,.9f};
    s.lines[1]=(sight_text_line_v1){.375f,.375f,.625f,.625f,.8f};
    int valid=hs_text_valid(&s,800,400);
    hs_text_raster(&s,mask,scratch);
    unsigned n=0;for(unsigned i=0;i<sizeof(mask);++i)n+=mask[i]==255;
    int union_ok=n==28672&&mask[128*512+128]==255&&mask[320*512+320]==0;
    s.count=0;hs_text_raster(&s,mask,scratch);
    int empty=hs_text_valid(&s,800,400);
    for(unsigned i=0;i<sizeof(mask);++i)empty&=mask[i]==0;
    s.count=1;s.lines[0]=(sight_text_line_v1){.999f,.999f,1.f,1.f,0.f};
    hs_text_raster(&s,mask,scratch);
    int edge=mask[sizeof(mask)-1]==255&&mask[sizeof(mask)-2]==0;
    int reject=!hs_text_valid(&s,800,401);
    s.lines[0].confidence=NAN;reject&=!hs_text_valid(&s,800,400);
    s.lines[0].confidence=.5f;s.lines[0].x0=1.5f;reject&=!hs_text_valid(&s,800,400);
    s.count=SIGHT_TEXT_MAX_LINES;
    for(unsigned i=0;i<s.count;++i)s.lines[i]=(sight_text_line_v1){0,0,1,1,1};
    int maximum=hs_text_valid(&s,800,400);
    hs_text_raster(&s,mask,scratch);
    for(unsigned i=0;i<sizeof(mask);++i)maximum&=mask[i]==255;
    s.count=513;reject&=!hs_text_valid(&s,800,400);
    printf("SIGHT_TEXT_MASK valid=%d union=%d empty=%d edge=%d maximum=%d reject=%d\n",valid,union_ok,empty,edge,maximum,reject);
    return !(valid&&union_ok&&empty&&edge&&maximum&&reject);
}
