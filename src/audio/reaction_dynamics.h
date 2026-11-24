#ifndef HS_REACTION_DYNAMICS_H
#define HS_REACTION_DYNAMICS_H
#include <stdbool.h>
#include <math.h>

struct hs_reaction_config {
    float threshold[3], width, hysteresis;
    float attack, release, hold, frequency, damping;
};
struct hs_reaction_state {
    float gate[3], target[3], remaining[3];
    bool open[3];
    float position, velocity;
};

static inline float hs_reaction_clamp(float x,float lo,float hi)
{
    return isfinite(x) ? fminf(hi,fmaxf(lo,x)) : lo;
}

static inline void hs_reaction_step(struct hs_reaction_state *s,
                                    const struct hs_reaction_config *c,
                                    float signal,float dt)
{
    dt=hs_reaction_clamp(dt,0.0f,0.1f);
    signal=hs_reaction_clamp(signal,0.0f,100.0f);
    float edge=0.0f;
    for (unsigned i=0;i<3;i++) {
        edge=fmaxf(edge,hs_reaction_clamp(c->threshold[i],0.0f,1.0f));
        float close=fmaxf(0.0f,edge-hs_reaction_clamp(c->hysteresis,0.0f,1.0f));
        if(signal>edge) {
            s->open[i]=true;
            s->remaining[i]=hs_reaction_clamp(c->hold,0.0f,2.0f);
            float u=c->width>0.00001f ? hs_reaction_clamp((signal-edge)/c->width,0.0f,1.0f) : 1.0f;
            s->target[i]=u*u*(3.0f-2.0f*u);
        } else if(s->open[i] && (signal<=close || signal<=0.0f)) {
            s->remaining[i]=fmaxf(0.0f,s->remaining[i]-dt);
            if(s->remaining[i]<=0.0f) {
                s->open[i]=false;
                s->target[i]=0.0f;
            }
        }
        float tau=s->target[i]>s->gate[i] ? c->attack : c->release;
        float a=tau>0.00001f ? -expm1f(-dt/tau) : 1.0f;
        s->gate[i]=hs_reaction_clamp(s->gate[i]+(s->target[i]-s->gate[i])*a,0.0f,1.0f);
    }
    // Integrate a real second-order spring with bounded substeps. The position
    // is deliberately not clamped to [0,1] so overshoot remains visible.
    float omega=6.28318530718f*hs_reaction_clamp(c->frequency,0.5f,8.0f);
    float damping=hs_reaction_clamp(c->damping,0.15f,2.0f);
    unsigned steps=(unsigned)ceilf(dt*240.0f);
    if(!steps) return;
    float h=dt/steps;
    for(unsigned j=0;j<steps;j++) {
        float accel=omega*omega*(s->target[0]-s->position)-2.0f*damping*omega*s->velocity;
        s->velocity+=accel*h;
        s->position+=s->velocity*h;
    }
    s->position=hs_reaction_clamp(s->position,-0.5f,1.6f);
    if(fabsf(s->position-s->target[0])<0.0001f && fabsf(s->velocity)<0.001f) {
        s->position=s->target[0]; s->velocity=0.0f;
    }
}
#endif
