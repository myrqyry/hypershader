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
 // Relative depth is a heuristic: reject a large discontinuity only
 // when distinct, dimension-matched depth observations are available.
 struct hs_surface_depth depth={0};
 uint8_t pixels[SIGHT_FLOW_CELLS];
 memset(pixels,40,sizeof pixels);
 hs_surface_depth_set(&depth,pixels,SIGHT_FLOW_WIDTH,SIGHT_FLOW_HEIGHT,640,360,100);
 fill(&s,20,0,0,1);CHECK(hs_surface_update_depth(&state,&s,0,&depth));
 fill(&s,21,-.03125f,0,1);CHECK(hs_surface_update_depth(&state,&s,20,&depth));
 CHECK(fabsf(state.field[i].dx+.03125f)<1e-6f);
 pixels[i]=220;
 hs_surface_depth_set(&depth,pixels,SIGHT_FLOW_WIDTH,SIGHT_FLOW_HEIGHT,640,360,101);
 fill(&s,22,0,0,1);CHECK(hs_surface_update_depth(&state,&s,21,&depth));
 CHECK(state.field[i].confidence==0&&state.field[i].dx==0);
 CHECK(state.field[i+3].confidence>0&&fabsf(state.field[i+3].dx+.03125f)<1e-6f);
 fill(&s,23,0,0,1);CHECK(hs_surface_update_depth(&state,&s,22,&depth));
 CHECK(state.field[i].confidence>0&&state.field[i].dx==0);
 fill(&s,24,0,0,1);CHECK(hs_surface_update_depth(&state,&s,23,NULL));
 CHECK(state.field[i+3].confidence>0);
 // No comparison across a missing auxiliary observation.
 memset(pixels,240,sizeof pixels);
 hs_surface_depth_set(&depth,pixels,SIGHT_FLOW_WIDTH,SIGHT_FLOW_HEIGHT,640,360,102);
 fill(&s,25,0,0,1);CHECK(hs_surface_update_depth(&state,&s,24,&depth));
 CHECK(state.field[i+3].confidence>0);
 // At a moving depth edge, compare against the warped predecessor,
 // not the depth from the same screen location.
 hs_surface_reset(&state);
 for(unsigned y=0;y<SIGHT_FLOW_HEIGHT;y++)for(unsigned x=0;x<SIGHT_FLOW_WIDTH;x++)
    pixels[y*SIGHT_FLOW_WIDTH+x]=x>=16?220:40;
 hs_surface_depth_set(&depth,pixels,SIGHT_FLOW_WIDTH,SIGHT_FLOW_HEIGHT,640,360,110);
 fill(&s,30,0,0,1);CHECK(hs_surface_update_depth(&state,&s,0,&depth));
 for(unsigned y=0;y<SIGHT_FLOW_HEIGHT;y++)for(unsigned x=0;x<SIGHT_FLOW_WIDTH;x++)
    pixels[y*SIGHT_FLOW_WIDTH+x]=x>=17?220:40;
 hs_surface_depth_set(&depth,pixels,SIGHT_FLOW_WIDTH,SIGHT_FLOW_HEIGHT,640,360,111);
 fill(&s,31,-.03125f,0,1);CHECK(hs_surface_update_depth(&state,&s,30,&depth));
 CHECK(state.field[i].confidence>0&&fabsf(state.field[i].dx+.03125f)<1e-6f);
 // Incompatible source dimensions must not invalidate flow history.
 hs_surface_depth_set(&depth,pixels,SIGHT_FLOW_WIDTH,SIGHT_FLOW_HEIGHT,1280,720,112);
 fill(&s,32,0,0,1);CHECK(hs_surface_update_depth(&state,&s,31,&depth));
 CHECK(state.field[i].confidence>0&&fabsf(state.field[i].dx+.03125f)<1e-6f);
 puts("SIGHT_SURFACE_TRANSPORT_PASS=1");return 0;
}
