// Native opt-in texture addressing and floor remainder primitives.
// Include hypershader_standard.glsl first for its linear/clamp sampler.
#ifndef HYPERSHADER_SAMPLING_H
#define HYPERSHADER_SAMPLING_H

// GLSL floor remainder; unlike HLSL fmod with negative operands.
// Macro supports scalar/vector broadcasting without OBS function overloads.
// Arguments must be side-effect-free and divisor nonzero.
#define hsFloorMod(x, y) ((x) - (y) * floor((x) / (y)))

// Explicit size belongs to the sampled resource, not the output viewport.
// Size must be the positive integral mip-0 dimensions.
float2 hsTexelUV(int2 pixel, float2 size)
{
    return (clamp(float2(pixel), float2(0.0, 0.0), size - 1.0) + 0.5) / size;
}

float2 hsRepeatTexelUV(float2 pixel, float2 size)
{
    return (hsFloorMod(pixel, size) + 0.5) / size;
}

float2 hsMirrorUV(float2 uv)
{
    return 1.0 - abs(hsFloorMod(uv, 2.0) - 1.0);
}

// One sampler per texture is reliable in OBS; mixing hardware sampler
// states on the same resource can alias. Address in shader math instead.
#define hsFetchTexel(t, pixel, size) t.Sample(textureSampler, hsTexelUV(pixel, size))
#define hsSamplePointClamp(t, uv, size) hsFetchTexel(t, int2(floor((uv) * (size))), size)
#define hsSamplePointRepeat(t, uv, size) t.Sample(textureSampler, hsRepeatTexelUV(floor((uv) * (size)), size))
#define hsSampleLinearMirror(t, uv) t.Sample(textureSampler, hsMirrorUV(uv))

// Wrap both bilinear neighbors independently, including across a repeat seam.
#define HS_REPEAT_BASE(uv, size) floor((uv) * (size) - 0.5)
#define HS_REPEAT_FRACTION(uv, size) frac((uv) * (size) - 0.5)
#define HS_REPEAT_TAP(t, uv, size, offset) t.Sample(textureSampler, hsRepeatTexelUV(HS_REPEAT_BASE(uv, size) + (offset), size))
#define hsSampleLinearRepeat(t, uv, size) lerp(lerp(HS_REPEAT_TAP(t, uv, size, float2(0.0, 0.0)), HS_REPEAT_TAP(t, uv, size, float2(1.0, 0.0)), HS_REPEAT_FRACTION(uv, size).x), lerp(HS_REPEAT_TAP(t, uv, size, float2(0.0, 1.0)), HS_REPEAT_TAP(t, uv, size, float2(1.0, 1.0)), HS_REPEAT_FRACTION(uv, size).x), HS_REPEAT_FRACTION(uv, size).y)

#endif
