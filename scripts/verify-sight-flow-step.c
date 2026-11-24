#include "interop/sight_flow_step.h"
#include <stdio.h>
int main(void)
{
    bool first=!hs_sight_flow_step(0,100,true);
    bool newer=hs_sight_flow_step(100,101,true);
    bool repeated=!hs_sight_flow_step(101,101,true);
    bool missing=!hs_sight_flow_step(101,102,false);
    bool rewind=!hs_sight_flow_step(101,99,true);
    bool skipped=hs_sight_flow_step(101,105,true);
    printf("SIGHT_FLOW_STEP first=%d newer=%d repeated=%d missing=%d rewind=%d skipped=%d\n",
        first,newer,repeated,missing,rewind,skipped);
    return !(first&&newer&&repeated&&missing&&rewind&&skipped);
}
