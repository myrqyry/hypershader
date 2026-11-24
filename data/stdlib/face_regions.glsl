#ifndef HS_FACE_REGIONS_H
#define HS_FACE_REGIONS_H
// Reusable anatomical eye regions in pixel space. Person's left/right,
// not screen left/right. Requires only the base 468-point face topology.
float2 hs_face_point(int id, float2 resolution) {
    return landmark_pos(id).xy * resolution;
}
float hs_eye_width(bool left, float2 resolution) {
    return length(hs_face_point(left ? 263 : 33, resolution) -
                  hs_face_point(left ? 362 : 133, resolution));
}
float hs_eye_open_ratio(bool left, float2 resolution) {
    if(hs_face_present < 0.5) return 0.0;
    float2 a=hs_face_point(left ? 362 : 133,resolution);
    float2 b=hs_face_point(left ? 263 : 33,resolution);
    float2 axis=b-a;
    float width=length(axis);
    if(width<2.0) return 0.0;
    float2 normal=float2(-axis.y,axis.x)/width;
    float2 top=hs_face_point(left ? 386 : 159,resolution);
    float2 bottom=hs_face_point(left ? 374 : 145,resolution);
    return abs(dot(top-bottom,normal))/width;
}
float hs_eye_visibility(bool left,float2 resolution) {
    if(hs_face_present<0.5) return 0.0;
    float a=hs_eye_width(left,resolution);
    float b=hs_eye_width(!left,resolution);
    if(a<2.0) return 0.0;
    return smoothstep(0.40,0.65,a/max(b,2.0)) *
           smoothstep(0.035,0.14,hs_eye_open_ratio(left,resolution));
}
// Ordered ring; no iris landmarks required. Image-space polygon, so a
// rotated face does not flip the lid mask or turn it into a fixed ellipse.
int hs_eye_ring(bool left,int i) {
    if(i==0) return left ? 362 : 133;
    if(i==1) return left ? 385 : 158;
    if(i==2) return left ? 386 : 159;
    if(i==3) return left ? 387 : 160;
    if(i==4) return left ? 263 : 33;
    if(i==5) return left ? 373 : 144;
    if(i==6) return left ? 374 : 145;
    return left ? 380 : 153;
}
// Returns a softly feathered open-eye interior; closed or occluded eyes
// fade out. This is a geometric estimate, not an occlusion segmentation.
float hs_eye_mask(float2 uv,bool left,float2 resolution) {
    float visible=hs_eye_visibility(left,resolution);
    if(visible<=0.0) return 0.0;
    float width=hs_eye_width(left,resolution);
    float2 point=uv*resolution;
    float2 a=hs_face_point(hs_eye_ring(left,0),resolution);
    float2 b=hs_face_point(hs_eye_ring(left,4),resolution);
    if(length(point-(a+b)*0.5)>width*0.85) return 0.0;
    float distance_to_edge=width;
    bool inside=false;
    float2 prev=hs_face_point(hs_eye_ring(left,7),resolution);
    for(int i=0;i<8;i++) {
        float2 next=hs_face_point(hs_eye_ring(left,i),resolution);
        float2 edge=next-prev;
        float2 delta=point-prev;
        float k=saturate(dot(delta,edge)/max(dot(edge,edge),0.0001));
        distance_to_edge=min(distance_to_edge,length(delta-edge*k));
        if((prev.y>point.y)!=(next.y>point.y)) {
            float cross_x=prev.x+(point.y-prev.y)*edge.x/edge.y;
            if(point.x<cross_x) inside=!inside;
        }
        prev=next;
    }
    if(!inside) return 0.0;
    return visible*smoothstep(0.0,max(1.0,width*0.035),distance_to_edge);
}
// Tint helpers preserve image luminance and avoid out-of-gamut clipping.
float3 hs_face_tint_luma(float3 color,float luminance) {
    const float3 weights=float3(0.2126,0.7152,0.0722);
    float3 value=color+float3(1.0,1.0,1.0)*(luminance-dot(color,weights));
    float lo=min(value.r,min(value.g,value.b));
    float hi=max(value.r,max(value.g,value.b));
    float3 grey=float3(luminance,luminance,luminance);
    if(lo<0.0) value=grey+(value-grey)*luminance/max(luminance-lo,0.00001);
    hi=max(value.r,max(value.g,value.b));
    if(hi>1.0) value=grey+(value-grey)*(1.0-luminance)/max(hi-luminance,0.00001);
    return saturate(value);
}
#endif
