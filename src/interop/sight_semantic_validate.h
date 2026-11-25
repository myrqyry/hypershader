#pragma once
#include "sight_semantic_v1.h"
#include <stdbool.h>
#include <math.h>
#include <string.h>
static inline bool hs_semantic_valid(const sight_semantic_v1 *s,uint32_t w,uint32_t h) {
 if(!s||s->abi_version!=SIGHT_SEMANTIC_ABI_V1||s->struct_size!=sizeof(*s)||
    !s->sequence||!w||!h||s->source_width!=w||s->source_height!=h||
    s->count>SIGHT_SEMANTIC_MAX_REGIONS)return false;
 for(uint32_t i=0;i<s->count;++i){
  const sight_semantic_region_v1 *r=&s->regions[i];
  if(!isfinite(r->x0)||!isfinite(r->y0)||!isfinite(r->x1)||!isfinite(r->y1)||
     !isfinite(r->confidence)||r->confidence<0||r->confidence>1||
     r->x0<0||r->y0<0||r->x1>1||r->y1>1||r->x0>=r->x1||r->y0>=r->y1||
     r->category>1023||r->held>1||!r->id)return false;
 }
 return true;
}
static inline void hs_semantic_pack(const sight_semantic_v1 *s,float *bounds,float *meta) {
 memset(bounds,0,SIGHT_SEMANTIC_MAX_REGIONS*4*sizeof(float));
 memset(meta,0,SIGHT_SEMANTIC_MAX_REGIONS*4*sizeof(float));
 for(uint32_t i=0;i<s->count;++i){
  const sight_semantic_region_v1 *r=&s->regions[i];
  bounds[i*4]=r->x0;bounds[i*4+1]=r->y0;bounds[i*4+2]=r->x1;bounds[i*4+3]=r->y1;
  meta[i*4]=(float)r->category;meta[i*4+1]=r->confidence;
  meta[i*4+2]=(float)(r->id&0xffffffu);meta[i*4+3]=(float)r->held;
 }
}
