#pragma once
#include "sight_text_v1.h"
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#define HS_TEXT_EDGE 512
static inline bool hs_text_valid(const sight_text_v1 *s,uint32_t w,uint32_t h) {
    if(!s||s->abi_version!=SIGHT_TEXT_ABI_V1||s->struct_size!=sizeof(*s)||
       !s->sequence||!w||!h||s->source_width!=w||s->source_height!=h||
       s->count>SIGHT_TEXT_MAX_LINES)return false;
    for(uint32_t i=0;i<s->count;++i) {
        const sight_text_line_v1 *b=&s->lines[i];
        if(!isfinite(b->x0)||!isfinite(b->y0)||!isfinite(b->x1)||!isfinite(b->y1)||
           !isfinite(b->confidence)||b->confidence<0||b->confidence>1||
           b->x0<0||b->y0<0||b->x1>1||b->y1>1||b->x0>=b->x1||b->y0>=b->y1)return false;
    }
    return true;
}
// Validated input only. Per-row difference array bounds work even for
// 512 overlapping full-frame rectangles. Outward rounding is conservative.
static inline void hs_text_raster(const sight_text_v1 *s,uint8_t *pixels,int16_t *scratch) {
    const unsigned stride=HS_TEXT_EDGE+1;
    memset(scratch,0,sizeof(*scratch)*HS_TEXT_EDGE*stride);
    for(uint32_t i=0;i<s->count;++i) {
        const sight_text_line_v1 *b=&s->lines[i];
        unsigned x0=(unsigned)floorf(b->x0*HS_TEXT_EDGE),x1=(unsigned)ceilf(b->x1*HS_TEXT_EDGE);
        unsigned y0=(unsigned)floorf(b->y0*HS_TEXT_EDGE),y1=(unsigned)ceilf(b->y1*HS_TEXT_EDGE);
        for(unsigned y=y0;y<y1;++y) {
            ++scratch[y*stride+x0];--scratch[y*stride+x1];
        }
    }
    for(unsigned y=0;y<HS_TEXT_EDGE;++y) {
        int n=0;
        for(unsigned x=0;x<HS_TEXT_EDGE;++x) {
            n+=scratch[y*stride+x];
            pixels[y*HS_TEXT_EDGE+x]=n>0?255:0;
        }
    }
}
