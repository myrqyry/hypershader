#pragma once
#include "sight_flow_v1.h"
#include <stdbool.h>
#include <math.h>
static inline bool hs_sight_flow_valid(const sight_flow_v1 *s,uint32_t w,uint32_t h) {
    if(!s||s->abi_version!=SIGHT_FLOW_ABI_V1||s->struct_size!=sizeof(*s)||!s->sequence||
       !w||!h||s->source_width!=w||s->source_height!=h||
       !isfinite(s->interval_seconds)||s->interval_seconds<=0||s->interval_seconds>.75f||
       !isfinite(s->coverage)||s->coverage<.2f||s->coverage>1)return false;
    unsigned valid=0;
    for(unsigned i=0;i<SIGHT_FLOW_CELLS;++i){
        const sight_flow_cell_v1 *v=&s->cells[i];
        if(!isfinite(v->dx)||!isfinite(v->dy)||!isfinite(v->confidence)||
           fabsf(v->dx)>.25f||fabsf(v->dy)>.25f||v->confidence<0||v->confidence>1||
           (v->confidence==0&&(v->dx!=0||v->dy!=0)))return false;
        valid+=v->confidence>0;
    }
    return fabsf(s->coverage-(float)valid/SIGHT_FLOW_CELLS)<.0001f;
}
