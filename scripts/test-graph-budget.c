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
    puts("GRAPH_BUDGET_UNIT_PASS");
    return 0;
}
