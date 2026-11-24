#pragma once
/* Render-local consumer state. Sight supplies snapshots; no model ownership.
 * Coordinates must match the native target dimensions. Eight bounded events,
 * launched by cumulative index fingertip motion, survive between snapshots. */
#include "sight_snapshot_v1.h"
#include <stdbool.h>
#include <string.h>
#include <math.h>

#define HS_TOUCH_CAPACITY 8
struct hs_touch_event {float x,y,pinch;double time;bool valid;};
struct hs_sight_touch {
    float fingers[2][4], anchor[2][2];
    bool anchored[2];
    struct hs_touch_event events[HS_TOUCH_CAPACITY];
    unsigned next;
    uint64_t sequence;
    uint32_t width,height;
};
static inline void hs_touch_reset(struct hs_sight_touch* t){memset(t,0,sizeof(*t));}
static inline void hs_touch_emit(struct hs_sight_touch* t,float x,float y,float pinch,double time){
    struct hs_touch_event* e=&t->events[t->next];
    e->x=x;e->y=y;e->pinch=pinch;e->time=time;e->valid=true;
    t->next=(t->next+1)%HS_TOUCH_CAPACITY;
}
static inline bool hs_touch_consume(struct hs_sight_touch* t,const sight_snapshot_v1* s,
                                   uint32_t w,uint32_t h,double time){
    if(!s||s->kind!=SIGHT_SNAPSHOT_HAND||!s->sequence||!w||!h||
       s->source_width!=w||s->source_height!=h||!s->hands_count||
       s->hands_count>SIGHT_SNAPSHOT_MAX_HANDS){
        hs_touch_reset(t);return false;
    }
    if(t->width!=w||t->height!=h){hs_touch_reset(t);t->width=w;t->height=h;}
    if(t->sequence==s->sequence)return false;
    t->sequence=s->sequence;
    memset(t->fingers,0,sizeof(t->fingers));
    unsigned count=0;
    for(unsigned i=0;i<s->hands_count;i++){
        const sight_snapshot_point_v1* p=&s->hands[i][8];
        if(!isfinite(p->x)||!isfinite(p->y)||!isfinite(s->pinch[i])||
           p->x<0||p->x>1||p->y<0||p->y>1)continue;
        float* f=t->fingers[count++];
        f[0]=p->x;f[1]=p->y;f[2]=fminf(1,fmaxf(0,s->pinch[i]));f[3]=1;
    }
    if(!count){hs_touch_reset(t);return false;}
    bool emitted=false;
    for(unsigned i=0;i<2;i++){
        if(i>=count){t->anchored[i]=false;continue;}
        const float* f=t->fingers[i];
        if(!t->anchored[i]){
            hs_touch_emit(t,f[0],f[1],f[2],time);emitted=true;
        }else{
            float dx=f[0]-t->anchor[i][0],dy=f[1]-t->anchor[i][1];
            float distance=hypotf(dx*(float)w/(float)h,dy);
            if(distance<.022f)continue;
            unsigned steps=(unsigned)fminf(4,fmaxf(1,floorf(distance/.022f)));
            for(unsigned j=1;j<=steps;j++){
                float u=(float)j/(float)steps;
                hs_touch_emit(t,t->anchor[i][0]+dx*u,t->anchor[i][1]+dy*u,f[2],time);
            }
            emitted=true;
        }
        t->anchor[i][0]=f[0];t->anchor[i][1]=f[1];t->anchored[i]=true;
    }
    return emitted;
}
/* Fourth component is 1+pinch for an active wave, 0 otherwise. Shader applies
 * its adjustable pinch boost, so no shader-specific settings live here. */
static inline void hs_touch_wave(const struct hs_sight_touch* t,unsigned i,double time,float out[4]){
    memset(out,0,4*sizeof(float));
    if(i>=HS_TOUCH_CAPACITY)return;
    const struct hs_touch_event* e=&t->events[i];
    double age=time-e->time;
    if(e->valid&&age>=0&&age<1.6){out[0]=e->x;out[1]=e->y;out[2]=(float)age;out[3]=1+e->pinch;}
}
