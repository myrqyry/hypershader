#include "interop/sight_touch.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"touch assertion line %d\n",__LINE__);return 1; } } while(0)
int main(void){
    struct hs_sight_touch trail={0};
    sight_snapshot_v1 s={0};
    s.kind=SIGHT_SNAPSHOT_HAND;s.hands_count=1;s.source_width=1920;s.source_height=1080;
    s.sequence=1;s.hands[0][8].x=.2f;s.hands[0][8].y=.45f;s.pinch[0]=.25f;
    CHECK(hs_touch_consume(&trail,&s,1920,1080,1.0));
    CHECK(!hs_touch_consume(&trail,&s,1920,1080,1.1));
    unsigned count=1;
    for(unsigned i=1;i<=80;i++){
        s.sequence=i+1;s.hands[0][8].x=.2f+.001f*i;
        count+=hs_touch_consume(&trail,&s,1920,1080,1.0+.02*i);
    }
    CHECK(count>=4);
    float out[4];unsigned live=0;
    for(unsigned i=0;i<8;i++){hs_touch_wave(&trail,i,2.6,out);live+=out[3]>0;}
    CHECK(live>=2);
    for(unsigned i=0;i<8;i++){hs_touch_wave(&trail,i,5.0,out);CHECK(out[3]==0);}
    CHECK(!hs_touch_consume(&trail,&s,512,288,5.0));
    CHECK(trail.fingers[0][3]==0);
    for(unsigned i=0;i<8;i++){hs_touch_wave(&trail,i,5.0,out);CHECK(out[3]==0);}
    CHECK(hs_touch_consume(&trail,&s,1920,1080,6.0));
    s.hands[0][8].x=NAN;s.sequence++;
    CHECK(!hs_touch_consume(&trail,&s,1920,1080,6.1));
    CHECK(trail.fingers[0][3]==0);
    s.sequence++;s.hands_count=2;
    s.hands[0][8].x=.25f;s.hands[0][8].y=.75f;s.pinch[0]=.8f;
    s.hands[1][8].x=.65f;s.hands[1][8].y=.35f;s.pinch[1]=2.f;
    CHECK(hs_touch_consume(&trail,&s,1920,1080,7.0));
    CHECK(trail.fingers[0][0]==.25f&&trail.fingers[1][1]==.35f&&trail.fingers[1][2]==1.f);
    s.kind=SIGHT_SNAPSHOT_FACE;
    CHECK(!hs_touch_consume(&trail,&s,1920,1080,7.1));
    CHECK(trail.sequence==0&&trail.fingers[0][3]==0);
    puts("HYPERSHADER_SIGHT_TOUCH_TRAIL_PASS=1");return 0;
}
