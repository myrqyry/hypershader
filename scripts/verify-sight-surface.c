#include "interop/sight_surface.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void fill(sight_flow_v1 *s,uint64_t seq,float dx,float dy,float conf){
 s->sequence=seq;s->source_width=640;s->source_height=360;
 for(unsigned i=0;i<SIGHT_FLOW_CELLS;i++)s->cells[i]=(sight_flow_cell_v1){dx,dy,conf,0};
}
int main(void){
 struct hs_sight_surface state={0};sight_flow_v1 s={0};
 const unsigned i=9*SIGHT_FLOW_WIDTH+16;
 fill(&s,10,-.03125f,0,1);
 CHECK(hs_surface_update(&state,&s,0));CHECK(state.field[i].dx==0);
 fill(&s,12,-.03125f,0,1); // another producer can interleave global IDs
 CHECK(hs_surface_update(&state,&s,10));CHECK(fabsf(state.field[i].dx+.03125f)<1e-6f);
 CHECK(!hs_surface_update(&state,&s,10));CHECK(fabsf(state.field[i].dx+.03125f)<1e-6f);
 fill(&s,13,-.03125f,0,1);
 CHECK(hs_surface_update(&state,&s,12));CHECK(fabsf(state.field[i].dx+.0625f)<1e-6f);
 fill(&s,14,0,0,1);
 CHECK(hs_surface_update(&state,&s,13));CHECK(fabsf(state.field[i].dx+.0625f)<1e-6f);
 // A skipped snapshot cannot be chained. Re-seed, don't integrate blindly.
 fill(&s,16,-.03125f,0,1);
 CHECK(hs_surface_update(&state,&s,15));CHECK(state.field[i].dx==0);
 fill(&s,17,0,0,0);CHECK(hs_surface_update(&state,&s,16));CHECK(state.field[i].confidence==0);
 fill(&s,18,.03125f,0,1);CHECK(hs_surface_update(&state,&s,17));
 CHECK(state.field[i].confidence>0&&fabsf(state.field[i].dx-.03125f)<1e-6f);
 CHECK(state.field[SIGHT_FLOW_WIDTH-1].confidence==0); // leaves previous frame
 fill(&s,19,0,0,1);s.source_width=1280;
 CHECK(hs_surface_update(&state,&s,18));CHECK(state.field[i].dx==0);
 hs_surface_reset(&state);CHECK(state.sequence==0);
 puts("SIGHT_SURFACE_TRANSPORT_PASS=1");return 0;
}
