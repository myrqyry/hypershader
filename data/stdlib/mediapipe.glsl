// --- HyperShader MediaPipe & Features Library ---
// High-level access to MediaPipe tracking, Audio analysis, and Physics entities.

#ifndef HYPERSHADER_MEDIAPIPE_H
#define HYPERSHADER_MEDIAPIPE_H

#include "hypershader_standard.glsl"

// --- MediaPipe Vision ---
uniform texture2d iSegmentation;
// 1 when a fresh source-local Sight foreground matte is available.
// Native face-region approximation is not a Sight matte.
uniform float iSightMattePresent;
// Backward displacement: current UV + RG samples the previous capture.
uniform texture2d iSightFlow; // 32x18 RGBA32F: dx,dy,confidence,reserved
uniform float iSightFlowPresent;
uniform float iSightFlowInterval; // seconds between captures
uniform float iSightFlowCoverage; // valid cell fraction
// Graph-only: 1 for a newly consumed snapshot after initialization.
uniform float iSightFlowStep;
// Relative inverse depth R8, 0 far / 1 near, independently normalized.
uniform texture2d iSightDepth;
uniform float iSightDepthPresent;
// Local OCR line geometry; a fresh empty result has count zero.
uniform float iSightTextPresent;
uniform int iSightTextCount;
uniform texture2d iSightTextMask;
uniform texture2d iSightTextBounds; // 512x1 RGBA32F, normalized xyxy
// Whole-frame normalized luma change between consecutive Sight captures.
uniform float iSightMotionPresent;
uniform float iSightMotionEnergy;
// Binary foreground geometry from the shared R8 matte (>=128).
// Bounds: minX,minY,maxX,maxY (outer pixel edges), normalized source UV.
uniform float iSightSubjectPresent;
uniform float4 iSightSubjectBounds;
uniform float2 iSightSubjectCenter;
uniform float iSightSubjectCoverage;
uniform texture2d iLandmarks;
uniform texture2d iReferenceLandmarks;
uniform texture2d iFaceSurface;
uniform texture2d iFaceTriangles;
uniform float iFacePresent;

// Face surface: R=(triangle index+.5)/898; G/B=barycentric weights;
// A=coverage. The remaining weight is 1-G-B.
#define face_surface_at(uv) iFaceSurface.Sample(textureSampler, (uv))
#define face_triangle_indices(id) iFaceTriangles.Load(int3((id), 0, 0)).rgb


// segmentation_mask: 1.0 at face/person, 0.0 at background
#define segmentation_mask(uv) iSegmentation.Sample(textureSampler, uv).r

// landmark_pos: Returns vec4(x, y, z, visibility) for landmark ID (0-467)
// Coordinates are normalized [0, 1]
#define landmark_pos(id) iLandmarks.Load(int3(id, 0, 0))

#include "face_regions.glsl"

// reference_pos: Returns the static reference pose landmarks (for puppeteering)
#define reference_pos(id) iReferenceLandmarks.Load(int3(id, 0, 0))

// --- Advanced Audio ---
#define bass     audio_band(0)
#define mid      audio_band(3)
#define treble   audio_band(6)
#define volume   audio_level
#define peak     audio_peak

// --- Physics Entities ---
uniform float4 entities[100]; // x, y, scale, type
uniform int entityCount;

struct Entity {
    vec2 pos;
    float scale;
    int type;
};

Entity getEntity(int id) {
    Entity e;
    float4 raw = entities[id];
    e.pos = raw.xy;
    e.scale = raw.z;
    e.type = int(raw.w);
    return e;
}

// --- Common SDF Helpers ---
float sdCircle(vec2 p, vec2 center, float r) {
    return length(p - center) - r;
}

#endif
