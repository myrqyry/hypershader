#include "interop/sight_semantic_validate.h"
#include <stdio.h>
int main(void) {
 sight_semantic_v1 s={0};s.abi_version=1;s.struct_size=sizeof(s);s.source_width=512;s.source_height=512;s.sequence=1;s.count=1;
 s.regions[0]=(sight_semantic_region_v1){.x0=.1f,.y0=.2f,.x1=.3f,.y1=.4f,.confidence=.9f,.category=3,.id=42};
 if(!hs_semantic_valid(&s,512,512))return 1;
 float bounds[SIGHT_SEMANTIC_MAX_REGIONS*4],meta[SIGHT_SEMANTIC_MAX_REGIONS*4];
 hs_semantic_pack(&s,bounds,meta);
 if(bounds[0]!=.1f||bounds[3]!=.4f||meta[0]!=3||meta[1]!=.9f||meta[2]!=42||meta[4]!=0)return 2;
 s.regions[0].confidence=NAN;if(hs_semantic_valid(&s,512,512))return 3;
 s.count=0;if(!hs_semantic_valid(&s,512,512))return 4;
 s.count=SIGHT_SEMANTIC_MAX_REGIONS+1;if(hs_semantic_valid(&s,512,512))return 5;
 puts("PASS: semantic validation, packed category/geometry and empty snapshots");
}
