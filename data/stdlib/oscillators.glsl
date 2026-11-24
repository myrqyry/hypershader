// HyperShader Standard Library - Oscillators
// Robust waveform generators for modulation (range [-1, 1])

#ifndef _STDLIB_OSC_H_
#define _STDLIB_OSC_H_

#ifndef PI
#define PI 3.14159265359
#endif

#ifndef TAU
#define TAU 6.28318530718
#endif

// Sine Wave: Smooth, clean oscillation
// Returns [-1, 1]
float osc_sine(float t) {
    return sin(t * TAU);
}

// Square Wave: Binary on/off style modulation
// Returns [-1, 1]
float osc_square(float t) {
    return sign(sin(t * TAU));
}

// Saw Wave: Linear ramp up
// Returns [-1, 1]
float osc_saw(float t) {
    return 2.0 * frac(t) - 1.0;
}

// Triangle Wave: Linear bounce
// Returns [-1, 1]
float osc_triangle(float t) {
    return 1.0 - 2.0 * abs(2.0 * frac(t) - 1.0);
}

// Pulse Wave: Square wave with variable pulse width (width 0..1)
// Returns [-1, 1]
float osc_pulse(float t, float width) {
    float x = frac(t);
    return step(x, width) * 2.0 - 1.0;
}

// Noise-based S&H (Sample and Hold) effect
// Returns [-1, 1] (Requires noise.glsl included before this, or uses simple hash)
float osc_noise_step(float t) {
    float id = floor(t);
    return frac(sin(id * 12.9898) * 43758.5453) * 2.0 - 1.0;
}

#endif
