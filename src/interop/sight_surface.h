#pragma once
#include "sight_flow_v1.h"
#include <stdbool.h>
#include <math.h>
#include <string.h>
/* A fixed-resolution backward coordinate field. R/G are reference UV
 * minus current UV; B is confidence. Not metric or instance surface UV. */
/* Coarse depth cache, sampled from the existing CPU snapshot only on
 * publication. Values are relative inverse depth, not metric distance. */
struct hs_surface_depth {
 uint64_t sequence;
 uint32_t width,height;
 uint8_t cells[SIGHT_FLOW_CELLS];
};
static inline void hs_surface_depth_set(struct hs_surface_depth *d,const uint8_t *pixels,
    uint32_t w,uint32_t h,uint32_t sw,uint32_t sh,uint64_t sequence){
 if(!pixels||!w||!h||!sw||!sh||!sequence){d->sequence=0;return;}
 d->sequence=sequence;d->width=sw;d->height=sh;
 for(unsigned y=0;y<SIGHT_FLOW_HEIGHT;y++)for(unsigned x=0;x<SIGHT_FLOW_WIDTH;x++){
  unsigned px=(unsigned)(((uint64_t)(2*x+1)*w)/(2*SIGHT_FLOW_WIDTH));
  unsigned py=(unsigned)(((uint64_t)(2*y+1)*h)/(2*SIGHT_FLOW_HEIGHT));
  d->cells[y*SIGHT_FLOW_WIDTH+x]=pixels[(size_t)py*w+px];
 }
}
struct hs_sight_surface {
 uint64_t sequence;
 uint32_t width,height;
 sight_flow_cell_v1 field[SIGHT_FLOW_CELLS];
 struct hs_surface_depth depth;
};
static inline void hs_surface_reset(struct hs_sight_surface *s){memset(s,0,sizeof(*s));}
static inline float hs_surface_clamp(float v,float lo,float hi){return fminf(fmaxf(v,lo),hi);}
static inline sight_flow_cell_v1 hs_surface_sample(const struct hs_sight_surface *s,float u,float v){
 float x=hs_surface_clamp(u*SIGHT_FLOW_WIDTH-.5f,0,SIGHT_FLOW_WIDTH-1);
 float y=hs_surface_clamp(v*SIGHT_FLOW_HEIGHT-.5f,0,SIGHT_FLOW_HEIGHT-1);
 unsigned x0=(unsigned)x,y0=(unsigned)y,x1=x0+1<SIGHT_FLOW_WIDTH?x0+1:x0,y1=y0+1<SIGHT_FLOW_HEIGHT?y0+1:y0;
 float fx=x-x0,fy=y-y0;
 unsigned idx[4]={y0*SIGHT_FLOW_WIDTH+x0,y0*SIGHT_FLOW_WIDTH+x1,y1*SIGHT_FLOW_WIDTH+x0,y1*SIGHT_FLOW_WIDTH+x1};
 float w[4]={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
 sight_flow_cell_v1 r={0};
 for(unsigned i=0;i<4;i++){
  const sight_flow_cell_v1 *c=&s->field[idx[i]];
  float k=w[i]*c->confidence;
  r.dx+=c->dx*k;r.dy+=c->dy*k;r.confidence+=k;
 }
 if(r.confidence>.001f){r.dx/=r.confidence;r.dy/=r.confidence;}
 return r;
}
/* Caller validates snapshot. The producer explicitly names its predecessor,
 * so gaps, replacements and discontinuities reseed instead of drifting. */
static inline bool hs_surface_update_depth(struct hs_sight_surface *s,const sight_flow_v1 *f,uint64_t predecessor,const struct hs_surface_depth *depth){
 if(!f->sequence)return false;
 if(s->sequence==f->sequence&&s->width==f->source_width&&s->height==f->source_height)return false;
 bool chain=s->sequence&&predecessor==s->sequence&&f->sequence>s->sequence&&
    s->width==f->source_width&&s->height==f->source_height;
 bool have_depth=depth&&depth->sequence&&depth->width==f->source_width&&depth->height==f->source_height;
 bool compare_depth=chain&&have_depth&&s->depth.sequence&&depth->sequence!=s->depth.sequence&&
    s->depth.width==depth->width&&s->depth.height==depth->height;
 sight_flow_cell_v1 next[SIGHT_FLOW_CELLS]={0};
 for(unsigned y=0;y<SIGHT_FLOW_HEIGHT;y++)for(unsigned x=0;x<SIGHT_FLOW_WIDTH;x++){
  unsigned i=y*SIGHT_FLOW_WIDTH+x;
  const sight_flow_cell_v1 *c=&f->cells[i];
  if(c->confidence<.25f)continue;
  float u=(x+.5f)/SIGHT_FLOW_WIDTH,v=(y+.5f)/SIGHT_FLOW_HEIGHT;
  if(!chain){next[i].confidence=c->confidence;continue;}
  float pu=u+c->dx,pv=v+c->dy;
  if(pu<0||pu>1||pv<0||pv>1)continue;
  if(compare_depth){
   unsigned px=(unsigned)hs_surface_clamp(pu*SIGHT_FLOW_WIDTH,0,SIGHT_FLOW_WIDTH-1);
   unsigned py=(unsigned)hs_surface_clamp(pv*SIGHT_FLOW_HEIGHT,0,SIGHT_FLOW_HEIGHT-1);
   // Conservative discontinuity gate (roughly 30% of normalized range).
   // Reject only across different observations; no temporal sync assumed.
   int jump=(int)depth->cells[i]-(int)s->depth.cells[py*SIGHT_FLOW_WIDTH+px];
   if(jump>76||jump<-76)continue;
  }
  sight_flow_cell_v1 prior=hs_surface_sample(s,pu,pv);
  float refu=pu+prior.dx,refv=pv+prior.dy;
  if(refu<0||refu>1||refv<0||refv>1){refu=pu;refv=pv;}
  next[i]=(sight_flow_cell_v1){refu-u,refv-v,c->confidence,0};
 }
 memcpy(s->field,next,sizeof(next));
 s->sequence=f->sequence;s->width=f->source_width;s->height=f->source_height;
 if(have_depth)s->depth=*depth;else s->depth.sequence=0;
 return true;
}
static inline bool hs_surface_update(struct hs_sight_surface *s,const sight_flow_v1 *f,uint64_t predecessor){
 return hs_surface_update_depth(s,f,predecessor,NULL);
}
