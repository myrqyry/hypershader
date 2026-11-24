// HyperShader Standard Library - Animation & Easing
// Time-based animation utilities

#ifndef _STDLIB_ANIMATION_H_
#define _STDLIB_ANIMATION_H_

// --- Easing Functions ---
// Standard easing equations (Robert Penner's formulas - public domain concept)

// Linear (no easing)
float easeLinear(float t) {
    return t;
}

// Quadratic
float easeInQuad(float t) {
    return t * t;
}

float easeOutQuad(float t) {
    return t * (2.0 - t);
}

float easeInOutQuad(float t) {
    return t < 0.5 ? 2.0 * t * t : -1.0 + (4.0 - 2.0 * t) * t;
}

// Cubic
float easeInCubic(float t) {
    return t * t * t;
}

float easeOutCubic(float t) {
    float f = t - 1.0;
    return f * f * f + 1.0;
}

float easeInOutCubic(float t) {
    return t < 0.5 ? 4.0 * t * t * t : (t - 1.0) * (2.0 * t - 2.0) * (2.0 * t - 2.0) + 1.0;
}

// Quartic
float easeInQuart(float t) {
    return t * t * t * t;
}

float easeOutQuart(float t) {
    float f = t - 1.0;
    return 1.0 - f * f * f * f;
}

// Exponential
float easeInExpo(float t) {
    return t == 0.0 ? 0.0 : pow(2.0, 10.0 * (t - 1.0));
}

float easeOutExpo(float t) {
    return t == 1.0 ? 1.0 : 1.0 - pow(2.0, -10.0 * t);
}

// Sine
float easeInSine(float t) {
    return 1.0 - cos(t * PI * 0.5);
}

float easeOutSine(float t) {
    return sin(t * PI * 0.5);
}

float easeInOutSine(float t) {
    return 0.5 * (1.0 - cos(PI * t));
}

// Elastic (bouncy overshoot)
float easeOutElastic(float t) {
    if (t == 0.0 || t == 1.0) return t;
    return pow(2.0, -10.0 * t) * sin((t - 0.075) * TAU / 0.3) + 1.0;
}

float easeInElastic(float t) {
    if (t == 0.0 || t == 1.0) return t;
    return -pow(2.0, 10.0 * (t - 1.0)) * sin((t - 1.075) * TAU / 0.3);
}

// Bounce
float easeOutBounce(float t) {
    if (t < 1.0 / 2.75) {
        return 7.5625 * t * t;
    } else if (t < 2.0 / 2.75) {
        t -= 1.5 / 2.75;
        return 7.5625 * t * t + 0.75;
    } else if (t < 2.5 / 2.75) {
        t -= 2.25 / 2.75;
        return 7.5625 * t * t + 0.9375;
    } else {
        t -= 2.625 / 2.75;
        return 7.5625 * t * t + 0.984375;
    }
}

float easeInBounce(float t) {
    return 1.0 - easeOutBounce(1.0 - t);
}

// Back (overshoot)
float easeInBack(float t) {
    const float s = 1.70158;
    return t * t * ((s + 1.0) * t - s);
}

float easeOutBack(float t) {
    const float s = 1.70158;
    float f = t - 1.0;
    return f * f * ((s + 1.0) * f + s) + 1.0;
}

// --- Time Utilities ---

// Ping-pong: oscillates 0->1->0 over period
float pingPong(float time, float period) {
    float t = mod(time, period * 2.0) / period;
    return t < 1.0 ? t : 2.0 - t;
}

// Triangle wave: linear version of ping-pong
float triangleWave(float t) {
    return abs(frac(t) * 2.0 - 1.0);
}

// Sawtooth wave: 0->1 repeating
float sawtoothWave(float t) {
    return frac(t);
}

// Square wave: alternates 0 and 1
float squareWave(float t, float dutyCycle) {
    return frac(t) < dutyCycle ? 1.0 : 0.0;
}

// Stepped animation: quantize to discrete steps
float stepped(float t, float steps) {
    return floor(t * steps) / (steps - 1.0);
}

// Smooth stepped: smoothstep between discrete values
float smoothStepped(float t, float steps) {
    float step = 1.0 / steps;
    float segment = floor(t / step);
    float localT = frac(t / step);
    float smoothT = smoothstep(0.0, 1.0, localT);
    return (segment + smoothT) / (steps - 1.0);
}

// Loop with hold: animate, then hold at end
float loopWithHold(float time, float animDuration, float holdDuration) {
    float totalDuration = animDuration + holdDuration;
    float t = mod(time, totalDuration);
    return clamp(t / animDuration, 0.0, 1.0);
}

// Delayed start
float delayedStart(float time, float delay, float duration) {
    return clamp((time - delay) / duration, 0.0, 1.0);
}

// Speed curve: time remapping with bezier
float bezierBlend(float t, float p1, float p2) {
    // Simple 1D cubic bezier with control points at (0.33, p1) and (0.66, p2)
    float t2 = t * t;
    float t3 = t2 * t;
    float mt = 1.0 - t;
    float mt2 = mt * mt;
    float mt3 = mt2 * mt;
    
    return mt3 * 0.0 + 3.0 * mt2 * t * p1 + 3.0 * mt * t2 * p2 + t3 * 1.0;
}

#endif
