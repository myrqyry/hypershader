#include "audio/reaction_dynamics.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void advance(struct hs_reaction_state *s, const struct hs_reaction_config *c,
                    float level, float seconds, unsigned fps)
{
    for(unsigned i=0;i<(unsigned)roundf(seconds*fps);i++)
        hs_reaction_step(s,c,level,1.0f/fps);
}

int main(void)
{
    struct hs_reaction_config c = {
        .threshold={.2f,.5f,.8f}, .width=0.0f, .hysteresis=.05f,
        .attack=.04f, .release=.12f, .hold=.15f, .frequency=3.0f, .damping=.45f
    };
    struct hs_reaction_state s={0}, other={0};
    advance(&s,&c,.9f,.3f,60);
    assert(s.gate[0]>.99f && s.gate[1]>.99f && s.gate[2]>.99f);
    assert(other.gate[0]==0 && other.position==0);
    // Hold all three gates, then release independently.
    advance(&s,&c,.0f,.1f,60);
    assert(s.gate[2]>.99f);
    advance(&s,&c,.0f,.6f,60);
    assert(s.gate[0]<.03f && s.gate[1]<.03f && s.gate[2]<.03f);
    // Jitter below the opening threshold cannot open; once open, it cannot close.
    advance(&s,&c,.19f,.3f,60);
    assert(s.gate[0]<.03f);
    advance(&s,&c,.23f,.3f,60);
    advance(&s,&c,.18f,.4f,60);
    assert(s.gate[0]>.99f && s.gate[1]<.03f);
    advance(&s,&c,.14f,.6f,60);
    assert(s.gate[0]<.03f);
    // True spring overshoots and settles; the same step at different FPS agrees.
    struct hs_reaction_state a={0}, b={0};
    advance(&a,&c,.4f,.2f,30);
    advance(&b,&c,.4f,.2f,120);
    assert(fabsf(a.position-b.position)<.04f);
    assert(a.position>1.0f);
    advance(&a,&c,.4f,1.0f,30);
    assert(fabsf(a.position-1.0f)<.03f);
    // Zero attack/release/hold preserves the instantaneous option.
    c.attack=c.release=c.hold=c.hysteresis=0;
    hs_reaction_step(&other,&c,.9f,1.0f/60);
    assert(other.gate[2]==1.0f);
    hs_reaction_step(&other,&c,0,1.0f/60);
    assert(other.gate[0]==0.0f);
    // Long frame and invalid input may never explode or leak NaN.
    hs_reaction_step(&other,&c,NAN,10.0f);
    assert(isfinite(other.position)&&fabsf(other.position)<2.0f);
    puts("REACTION_DYNAMICS_PASS");
}
