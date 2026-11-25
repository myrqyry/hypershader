#ifndef HS_SEMANTIC_REGIONS_H
#define HS_SEMANTIC_REGIONS_H
// Requires the native hs_sight_semantic_* uniforms. Categories are model IDs.
// A/B/C are ordered priorities, -1 means disabled. Prefer fresh over held,
// then confidence, then smaller stable ID within a slot. Return -1 outside.
int hs_semantic_route(float2 uv, int a, int b, int c, out float4 bounds, out float4 metadata) {
    bounds=float4(0,0,0,0);metadata=float4(0,0,0,0);
    if(hs_sight_semantic_present<0.5||hs_sight_semantic_count<1)return -1;
    int best=3;
    for(int i=0;i<min(hs_sight_semantic_count,256);i++){
        float4 m=hs_sight_semantic_meta.Load(int3(i,0,0));
        int slot=3;
        if(a>=0&&m.x==float(a))slot=0;
        else if(b>=0&&m.x==float(b))slot=1;
        else if(c>=0&&m.x==float(c))slot=2;
        if(slot>best||slot==3)continue;
        float4 r=hs_sight_semantic_bounds.Load(int3(i,0,0));
        if(uv.x<r.x||uv.y<r.y||uv.x>r.z||uv.y>r.w)continue;
        bool wins=slot<best||m.w<metadata.w||
            (m.w==metadata.w&&(m.y>metadata.y||(m.y==metadata.y&&m.z<metadata.z)));
        if(wins){best=slot;bounds=r;metadata=m;}
    }
    return best==3?-1:best;
}
float2 hs_semantic_local(float2 uv,float4 bounds) {
    return saturate((uv-bounds.xy)/max(bounds.zw-bounds.xy,float2(0.00001,0.00001)));
}
#endif
