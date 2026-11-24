#include "interop/sight_flow_validate.h"
#include <math.h>
#include <stdio.h>
int main(void){
    sight_flow_v1 s={0};
    s.abi_version=SIGHT_FLOW_ABI_V1;s.struct_size=sizeof(s);
    s.source_width=320;s.source_height=180;s.sequence=1;s.interval_seconds=.05f;s.coverage=1;
    for(unsigned i=0;i<SIGHT_FLOW_CELLS;++i)s.cells[i]=(sight_flow_cell_v1){-.015f,.02f,.8f,0};
    bool valid=hs_sight_flow_valid(&s,320,180);
    bool reject=!hs_sight_flow_valid(&s,320,181);
    s.cells[1].dx=NAN;reject&=!hs_sight_flow_valid(&s,320,180);
    s.cells[1].dx=.3f;reject&=!hs_sight_flow_valid(&s,320,180);
    s.cells[1].dx=0;s.cells[1].confidence=0;s.cells[1].dy=.1f;reject&=!hs_sight_flow_valid(&s,320,180);
    s.cells[1].dy=0;s.coverage=(SIGHT_FLOW_CELLS-1.f)/SIGHT_FLOW_CELLS;
    bool partial=hs_sight_flow_valid(&s,320,180);
    s.coverage=.5f;reject&=!hs_sight_flow_valid(&s,320,180);
    s.coverage=(SIGHT_FLOW_CELLS-1.f)/SIGHT_FLOW_CELLS;s.interval_seconds=1;reject&=!hs_sight_flow_valid(&s,320,180);
    printf("SIGHT_FLOW_VALIDATE valid=%d partial=%d reject=%d\n",valid,partial,reject);
    return !(valid&&partial&&reject);
}
