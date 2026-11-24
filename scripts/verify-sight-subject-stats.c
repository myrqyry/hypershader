#include "interop/sight_subject_stats.h"
#include <math.h>
#include <stdio.h>

static int near(float a, float b) { return fabsf(a-b)<1e-6f; }
static int empty(struct hs_sight_subject_stats s)
{
    return !s.present && !s.coverage && !s.bounds[0] && !s.bounds[1]
        && !s.bounds[2] && !s.bounds[3] && !s.center[0] && !s.center[1];
}
int main(void)
{
    uint8_t mask[12]={0,127,128,0, 0,255,0,0, 0,255,255,0};
    struct hs_sight_subject_stats s=hs_sight_subject_measure(mask,4,3);
    int asym=s.present && near(s.bounds[0],.25f) && near(s.bounds[1],0.f)
        && near(s.bounds[2],.75f) && near(s.bounds[3],1.f)
        && near(s.center[0],.5f) && near(s.center[1],7.f/12.f)
        && near(s.coverage,1.f/3.f);
    uint8_t one[6]={0,0,0,0,0,128};
    s=hs_sight_subject_measure(one,3,2);
    int single=s.present && near(s.bounds[0],2.f/3.f) && near(s.bounds[1],.5f)
        && near(s.bounds[2],1.f) && near(s.bounds[3],1.f)
        && near(s.center[0],5.f/6.f) && near(s.center[1],.75f) && near(s.coverage,1.f/6.f);
    uint8_t all[6]={255,128,255,255,255,255};
    s=hs_sight_subject_measure(all,3,2);
    int full=s.present && near(s.coverage,1.f) && near(s.center[0],.5f)
        && near(s.center[1],.5f) && near(s.bounds[2],1.f) && near(s.bounds[3],1.f);
    uint8_t weak[3]={0,1,127};
    int invalid=empty(hs_sight_subject_measure(weak,3,1))
        && empty(hs_sight_subject_measure(NULL,3,1))
        && empty(hs_sight_subject_measure(weak,0,1))
        && empty(hs_sight_subject_measure(weak,321,1));
    printf("SIGHT_SUBJECT_STATS asym=%d single=%d full=%d empty_invalid=%d\n",asym,single,full,invalid);
    return !(asym&&single&&full&&invalid);
}
