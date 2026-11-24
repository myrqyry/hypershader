// HyperShader Standard Library - ISF Compatibility
// Maps ISF globals to the native HyperShader runtime.

#ifndef _STDLIB_ISF_H_
#define _STDLIB_ISF_H_

#include "hypershader_standard.glsl"

// --- ISF Uniform Configuration ---
// ISF uses specific names for time and resolution.
// We map them to the native HyperShader runtime equivalents.

#define RENDERSIZE iResolution.xy
#define TIME iTime
#define DATE iDate
#define FRAMEINDEX iFrame

// Normalized coordinate (0..1)
// Note: In ISF, this is often provided as varying 'isf_FragNormCoord'.
// Users must calculate it manually in main() if not using a wrapper:
// vec2 isf_FragNormCoord = fragCoord / RENDERSIZE;

// ISF Input Mapping
// Usually ISF inputs are defined in JSON. Here we map standard texture units.
#define inputImage iChannel0
#define inputImage1 iChannel1
#define inputImage2 iChannel2
#define inputImage3 iChannel3

// Helper macro to wrap ISF-style code.
// NOTE: We avoid "gl_" prefix as it's reserved in OpenGL.

#define ISF_MAIN \
void mainImage(out vec4 fragColor, in vec2 fragCoord) { \
    vec2 isf_FragNormCoord = fragCoord / RENDERSIZE; \
    vec2 isf_fragCoord = fragCoord; \
    vec4 isf_outColor; \
    mainISF(isf_outColor, isf_fragCoord); \
/* ISF outColor is converted to native float4 below. */ \
    fragColor = float4(isf_outColor.rgb, isf_outColor.a); \
} \
HYPERSHADER_MAINIMAGE_WRAPPER

// Users should define:
// void mainISF(out vec4 isf_outColor, in vec2 isf_fragCoord) { ... }

#endif
