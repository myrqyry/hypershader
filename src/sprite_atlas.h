#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

enum hs_atlas_mode { HS_ATLAS_LOOP, HS_ATLAS_ONCE, HS_ATLAS_PINGPONG, HS_ATLAS_RANDOM, HS_ATLAS_IDLE_BURSTS };
struct hs_atlas_config { int columns, rows, first, count, mode; double fps, rest_min, rest_max; };
struct hs_atlas_clock {
    struct hs_atlas_config config;
    uint64_t last_ns;
    double fractional, rest;
    unsigned phase, frame;
    int playback;
    uint32_t rng;
    bool triggered, initialized;
};

static inline int hs_atlas_limit(int x,int lo,int hi)
{
    return x<lo?lo:x>hi?hi:x;
}
static inline struct hs_atlas_config hs_atlas_sanitize(struct hs_atlas_config c)
{
    c.columns=hs_atlas_limit(c.columns,1,64);
    c.rows=hs_atlas_limit(c.rows,1,64);
    int total=c.columns*c.rows;
    c.first=hs_atlas_limit(c.first,0,total-1);
    c.count=c.count<=0?total-c.first:hs_atlas_limit(c.count,1,total-c.first);
    c.mode=hs_atlas_limit(c.mode,0,4);
    c.fps=isfinite(c.fps)?fmin(60.0,fmax(0.0,c.fps)):12.0;
    c.rest_min=isfinite(c.rest_min)?fmin(60.0,fmax(0.0,c.rest_min)):1.0;
    c.rest_max=isfinite(c.rest_max)?fmin(60.0,fmax(c.rest_min,c.rest_max)):fmax(c.rest_min,4.0);
    return c;
}
static inline bool hs_atlas_equal(struct hs_atlas_config a,struct hs_atlas_config b)
{
    return a.columns==b.columns&&a.rows==b.rows&&a.first==b.first&&a.count==b.count&&a.mode==b.mode&&a.fps==b.fps&&a.rest_min==b.rest_min&&a.rest_max==b.rest_max;
}
static inline uint32_t hs_atlas_random(uint32_t *state)
{
    uint32_t x=*state?*state:0x9e3779b9u;
    x^=x<<13; x^=x>>17; x^=x<<5;
    return *state=x;
}
static inline double hs_atlas_rest(struct hs_atlas_clock *s)
{
    double u=(double)hs_atlas_random(&s->rng)/(double)UINT32_MAX;
    return s->config.rest_min+u*(s->config.rest_max-s->config.rest_min);
}

/* Keep an entire idle gesture in sequence, with a randomized rest on frame
 * zero between gestures. Consume time continuously across rest boundaries. */
static inline void hs_atlas_burst_step(struct hs_atlas_clock *s,double seconds)
{
    double fps=s->config.fps;
    if(fps<=0.0) return;
    while(seconds>1e-9) {
        if(s->rest>0.0) {
            double used=fmin(s->rest,seconds);
            s->rest-=used;
            seconds-=used;
            if(seconds<=1e-9) break;
        }
        double next=(1.0-s->fractional)/fps;
        if(seconds+1e-9<next) {
            s->fractional+=seconds*fps;
            break;
        }
        seconds=fmax(0.0,seconds-next);
        s->fractional=0.0;
        if(++s->frame>=(unsigned)s->config.count) {
            s->frame=0;
            s->rest=hs_atlas_rest(s);
        }
    }
}

/* mode: 0 continuous, 1 restart on rising target, 2 pause/resume.
 * Random is a non-repeating frame selection, not random clip selection. */
static inline unsigned hs_atlas_step(struct hs_atlas_clock *s,struct hs_atlas_config c,
                                     uint64_t now,int playback,bool trigger,bool visible,uint32_t seed)
{
    c=hs_atlas_sanitize(c);
    playback=hs_atlas_limit(playback,0,2);
    if(!s->initialized||!hs_atlas_equal(s->config,c)||s->playback!=playback) {
        *s=(struct hs_atlas_clock){0};
        s->initialized=true; s->config=c; s->rng=seed; s->playback=playback;
        if(c.mode==HS_ATLAS_IDLE_BURSTS) s->rest=hs_atlas_rest(s);
    }
    if(now==s->last_ns&&now) return c.first+s->frame;
    uint64_t delta=now>s->last_ns&&s->last_ns?now-s->last_ns:0;
    s->last_ns=now;
    bool rising=trigger&&!s->triggered;
    s->triggered=trigger;
    if(playback==1&&rising) {
        s->phase=s->frame=0; s->fractional=0;
        if(c.mode==HS_ATLAS_IDLE_BURSTS) s->rest=hs_atlas_rest(s);
        return c.first;
    }
    if((playback!=0&&!visible)||delta>250000000ULL||c.count<=1)
        return c.first+s->frame;
    if(c.mode==HS_ATLAS_IDLE_BURSTS) {
        hs_atlas_burst_step(s,(double)delta*1e-9);
        return c.first+s->frame;
    }
    s->fractional+=(double)delta*1e-9*c.fps;
    unsigned steps=(unsigned)s->fractional;
    s->fractional-=steps;
    unsigned n=(unsigned)c.count;
    while(steps--) {
        switch(c.mode) {
        case HS_ATLAS_ONCE: if(s->frame+1<n) ++s->frame; break;
        case HS_ATLAS_PINGPONG:
            s->phase=(s->phase+1)%(2*(n-1));
            s->frame=s->phase<n?s->phase:2*(n-1)-s->phase; break;
        case HS_ATLAS_RANDOM:
            s->frame=(s->frame+1+hs_atlas_random(&s->rng)%(n-1))%n; break;
        default: s->frame=(s->frame+1)%n; break;
        }
    }
    return c.first+s->frame;
}
