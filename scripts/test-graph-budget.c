#include <assert.h>
#include <stdio.h>
#include "shader/graph-budget.h"

int main(void)
{
    uint64_t bytes = 0;
    const uint32_t full[2] = {1, 1}, scaled[2] = {2, 4};
    const uint32_t rgba[2] = {4, 4}, half[2] = {8, 8};
    const uint64_t MiB = UINT64_C(1024) * 1024;
    assert(hs_graph_budget_bytes(1920, 1080, 4, full, rgba, 2, &bytes));
    assert(bytes == UINT64_C(1920)*1080*4*7 + 4);
    assert(hs_graph_budget_bytes(1920, 1080, 4, scaled, half, 2, &bytes));
    assert(bytes == UINT64_C(1920)*1080*4*3 + UINT64_C(960)*540*16 + UINT64_C(480)*270*16 + 4);
    assert(hs_graph_budget_bytes(237, 133, 4, scaled, half, 2, &bytes));
    assert(bytes == UINT64_C(237)*133*12 + UINT64_C(119)*67*16 + UINT64_C(60)*34*16 + 4);
    assert(hs_graph_budget_bytes(3840, 2160, 8, full, half, 2, &bytes) && bytes <= 512*MiB);
    assert(hs_graph_budget_bytes(7680, 4320, 4, full, rgba, 2, &bytes) && bytes > 512*MiB);
    assert(hs_graph_budget_bytes(240, 136, 4, full, half, 2, &bytes) && bytes > MiB);
    assert(hs_graph_budget_bytes(120, 68, 4, full, half, 2, &bytes) && bytes < MiB);
    assert(!hs_graph_budget_bytes(UINT32_MAX, UINT32_MAX, 8, full, half, 2, &bytes));
    assert(!hs_graph_budget_bytes(0, 1080, 4, full, rgba, 2, &bytes));
    const uint32_t invalid[2] = {0, 2};
    assert(!hs_graph_budget_bytes(1920, 1080, 4, invalid, rgba, 2, &bytes));
    assert(!hs_graph_budget_bytes(1920, 1080, 1, full, rgba, 2, &bytes));
    const uint32_t d3[] = {2,4,8}, b3[] = {8,8,8};
    assert(hs_graph_budget_bytes(237,133,4,d3,b3,3,&bytes));
    assert(bytes == UINT64_C(237)*133*12 + UINT64_C(119)*67*16 + UINT64_C(60)*34*16 + UINT64_C(30)*17*16 + 4);
    assert(!hs_graph_budget_bytes(240,136,4,d3,b3,0,&bytes));
    assert(!hs_graph_budget_bytes(240,136,4,d3,b3,9,&bytes));
    const uint32_t d4[]={2,2,2,2}, b4[]={4,4,4,4};
    const bool f4[]={false,false,false,false}, t4[]={false,true,true,true};
    const unsigned last[]={4,2,3,4};
    unsigned owner[4];
    assert(hs_graph_plan_slots(4,d4,f4,t4,last,owner));
    assert(owner[0]==0 && owner[1]==1 && owner[2]==2 && owner[3]==1);
    assert(hs_graph_planned_bytes(1920,1080,4,d4,b4,t4,owner,4,&bytes));
    assert(bytes==UINT64_C(1920)*1080*12+UINT64_C(960)*540*4*4+4);
    const unsigned overlap[]={4,3,3,4};
    assert(hs_graph_plan_slots(4,d4,f4,t4,overlap,owner)&&owner[3]==3);
    const bool different_format[]={false,false,false,true};
    assert(hs_graph_plan_slots(4,d4,different_format,t4,last,owner)&&owner[3]==3);
    const uint32_t different_size[]={2,2,2,4};
    assert(hs_graph_plan_slots(4,different_size,f4,t4,last,owner)&&owner[3]==3);
    const unsigned invalid_end[]={4,0,3,4};
    assert(!hs_graph_plan_slots(4,d4,f4,t4,invalid_end,owner));
    const unsigned invalid_future[]={4,5,3,4};
    assert(!hs_graph_plan_slots(4,d4,f4,t4,invalid_future,owner));
    const unsigned invalid_owner[]={0,0,2,3};
    assert(!hs_graph_planned_bytes(1920,1080,4,d4,b4,t4,invalid_owner,4,&bytes));
    assert(hs_render_scale_percent(0)==100 && hs_render_scale_percent(INT64_MAX)==100);
    assert(hs_render_scale_extent(97,75)==73 && hs_render_scale_extent(73,25)==19);
    assert(hs_render_scale_extent(UINT32_MAX,100)==UINT32_MAX);
    assert(hs_render_scale_extent(1,25)==1);
    assert(hs_graph_plan_slots(4,d4,f4,t4,last,owner));
    assert(hs_graph_scaled_bytes(1920,1080,960,540,4,d4,b4,t4,owner,4,&bytes));
    assert(bytes==UINT64_C(1920)*1080*8+UINT64_C(960)*540*4+UINT64_C(480)*270*4*4+4);
    assert(!hs_graph_scaled_bytes(1920,1080,0,540,4,d4,b4,t4,owner,4,&bytes));
    puts("GRAPH_BUDGET_UNIT_PASS");
    return 0;
}
