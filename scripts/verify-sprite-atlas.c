#include "sprite_atlas.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    struct hs_atlas_clock s={0}, other={0};
    struct hs_atlas_config c={4,2,2,3,HS_ATLAS_LOOP,5.0,0.0,0.0};
    uint64_t t=1000000000ULL, dt=200000000ULL;
    assert(hs_atlas_step(&s,c,t,0,false,false,123)==2);
    assert(hs_atlas_step(&s,c,t+dt,0,false,false,123)==3);
    assert(hs_atlas_step(&s,c,t+dt,0,false,false,123)==3);
    assert(hs_atlas_step(&s,c,t+2*dt,0,false,false,123)==4);
    assert(hs_atlas_step(&s,c,t+3*dt,0,false,false,123)==2);
    assert(hs_atlas_step(&s,c,t+20*dt,0,false,false,123)==2);
    assert(!other.initialized);
    c.mode=HS_ATLAS_ONCE;
    t+=25*dt;
    for(int i=0;i<7;i++) assert(hs_atlas_step(&s,c,t+i*dt,0,false,true,123)==(unsigned)(i<2?2+i:4));
    c.mode=HS_ATLAS_PINGPONG;
    t+=10*dt;
    unsigned ping[]={2,3,4,3,2,3};
    for(int i=0;i<6;i++) assert(hs_atlas_step(&s,c,t+i*dt,0,false,true,123)==ping[i]);
    c.mode=HS_ATLAS_RANDOM;
    t+=10*dt; unsigned last=0, seen=0;
    for(int i=0;i<30;i++){
        unsigned f=hs_atlas_step(&s,c,t+i*dt,0,false,true,123);
        assert(f>=2&&f<=4&&(i==0||last!=f)); seen|=1u<<f; last=f;
    }
    assert(seen==28);
    c.mode=HS_ATLAS_LOOP;
    t+=40*dt;
    assert(hs_atlas_step(&s,c,t,1,false,false,123)==2);
    assert(hs_atlas_step(&s,c,t+dt,1,true,true,123)==2);
    assert(hs_atlas_step(&s,c,t+2*dt,1,true,true,123)==3);
    assert(hs_atlas_step(&s,c,t+3*dt,1,false,false,123)==3);
    assert(hs_atlas_step(&s,c,t+4*dt,1,true,true,123)==2);
    t+=10*dt;
    assert(hs_atlas_step(&s,c,t,2,true,true,123)==2);
    assert(hs_atlas_step(&s,c,t+dt,2,true,true,123)==3);
    assert(hs_atlas_step(&s,c,t+2*dt,2,false,false,123)==3);
    assert(hs_atlas_step(&s,c,t+3*dt,2,false,false,123)==3);
    assert(hs_atlas_step(&s,c,t+4*dt,2,true,true,123)==4);
    c=(struct hs_atlas_config){4,2,2,3,HS_ATLAS_IDLE_BURSTS,5.0,.4,.4};
    t+=10*dt;
    unsigned burst[]={2,2,2,3,4,2,2,2,3,4,2};
    for(int i=0;i<11;i++) assert(hs_atlas_step(&s,c,t+i*dt,0,false,true,123)==burst[i]);
    c.rest_min=.2;c.rest_max=.6;
    t+=20*dt;
    hs_atlas_step(&s,c,t,0,false,true,123);
    assert(s.rest>=.2&&s.rest<=.6);
    double previous=s.rest;
    bool varied=false;
    for(int i=1;i<100;i++){
        unsigned before=s.frame;
        hs_atlas_step(&s,c,t+i*dt,0,false,true,123);
        if(before==2&&s.frame==0) {
            assert(s.rest>=0&&s.rest<=.6);
            varied|=fabs(s.rest-previous)>.01;
            previous=s.rest;
        }
    }
    assert(varied);
    c=(struct hs_atlas_config){0,900,999,999,99,NAN,NAN,NAN};
    c=hs_atlas_sanitize(c);
    assert(c.columns==1&&c.rows==64&&c.first==63&&c.count==1&&c.mode==4&&c.fps==12.0);
    puts("SPRITE_ATLAS_CLOCK_PASS=1");
}
