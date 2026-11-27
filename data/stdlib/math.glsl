// hypershader Standard Library - Math Utilities

#ifndef _STDLIB_MATH_H_
#define _STDLIB_MATH_H_

// Global Math Settings
uniform float global_rotation; // @min 0.0 @max 6.28 @label "Master Rotation"

// --- Constants ---
#define PI 3.14159265359
#define TAU 6.28318530718
#define HALF_PI 1.57079632679
#define PHI 1.61803398875
#define E 2.71828182845
#define SQRT2 1.41421356237

// --- Basic Utilities ---

// Luminance (standard Rec. 709 coefficients - for SDR)
float lumaRec709(float3 color) {
    return dot(color, float3(0.299, 0.587, 0.114));
}

// Compatibility macro for the old name
#define luma(c) lumaRec709(c)

// Square (avoids pow for simple cases)
float sq(float x) { return x * x; }
float2 sq(float2 x) { return x * x; }
float3 sq(float3 x) { return x * x; }

// Cube
float cube(float x) { return x * x * x; }

// Safe division (avoid divide by zero)
float safeDivide(float a, float b) {
    return b != 0.0 ? a / b : 0.0;
}

// --- Value Remapping ---

// Remap value v from range [a,b] to [c,d]
float remap(float v, float a, float b, float c, float d) {
    return c + (d - c) * clamp((v - a) / (b - a), 0.0, 1.0);
}

// Remap without clamping
float remapUnclamped(float v, float a, float b, float c, float d) {
    return c + (d - c) * ((v - a) / (b - a));
}

// Normalize to 0-1 range
float normalize01(float v, float minVal, float maxVal) {
    return clamp((v - minVal) / (maxVal - minVal), 0.0, 1.0);
}

// --- Rotation & Transforms ---

// Rotate vector p by angle a (2D rotation)
float2 rotate(float2 p, float a) {
    float s = sin(a);
    float c = cos(a);
    return float2(p.x * c - p.y * s, p.x * s + p.y * c);
}

// Rotate around a center point
float2 rotateAround(float2 p, float2 center, float angle) {
    return rotate(p - center, angle) + center;
}

// --- Polar Coordinates ---

// Cartesian to polar (returns: x=radius, y=angle)
float2 cartesianToPolar(float2 p) {
    return float2(length(p), atan2(p.y, p.x));
}

// Polar to cartesian (input: x=radius, y=angle)
float2 polarToCartesian(float2 polar) {
    return polar.x * float2(cos(polar.y), sin(polar.y));
}

// Get angle from center (0 to TAU)
float angleFromCenter(float2 uv, float2 center) {
    float2 delta = uv - center;
    float angle = atan2(delta.y, delta.x);
    return angle < 0.0 ? angle + TAU : angle;
}

// --- Smoothing Functions ---

// Smoother step (Ken Perlin's improved version)
float smootherstep(float edge0, float edge1, float x) {
    x = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}

// Cubic smooth (simple hermite)
float smoothCubic(float t) {
    return t * t * (3.0 - 2.0 * t);
}

// Quintic smooth
float smoothQuintic(float t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// Exponential smooth (good for audio/values)
float expSmooth(float current, float target, float speed) {
    return lerp(current, target, 1.0 - exp(-speed));
}

// --- Clamping & Limiting ---

// Soft clamp (approaches limit asymptotically)
float softClamp(float x, float limit) {
    return limit * tanh(x / limit);
}

// Symmetric soft clamp
float softClampSymmetric(float x, float limit) {
    return limit * tanh(x / limit);
}

// Bias function (attempt to push value toward 0 or 1)
float bias(float t, float b) {
    return pow(t, log(b) / log(0.5));
}

// Gain function (attempt to increase/decrease contrast around 0.5)
float gain(float t, float g) {
    if (t < 0.5) {
        return bias(2.0 * t, 1.0 - g) / 2.0;
    } else {
        return 1.0 - bias(2.0 - 2.0 * t, 1.0 - g) / 2.0;
    }
}

// --- Periodic Functions ---

// Normalized sine (0 to 1 output)
float sinNorm(float x) {
    return sin(x) * 0.5 + 0.5;
}

// Normalized cosine (0 to 1 output)
float cosNorm(float x) {
    return cos(x) * 0.5 + 0.5;
}

// Modulo that handles negatives correctly
float modPositive(float x, float m) {
    return mod(mod(x, m) + m, m);
}

// --- Geometric Helpers ---

// Aspect ratio correction for UV
float2 aspectCorrect(float2 uv, float2 resolution) {
    float aspect = resolution.x / resolution.y;
    return float2((uv.x - 0.5) * aspect + 0.5, uv.y);
}

// Distance from point to line segment
float distToSegment(float2 p, float2 a, float2 b) {
    float2 pa = p - a;
    float2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

// Signed distance to circle
float sdCircle(float2 p, float2 center, float radius) {
    return length(p - center) - radius;
}

// Signed distance to box
float sdBox(float2 p, float2 center, float2 size) {
    float2 d = abs(p - center) - size;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

#endif
