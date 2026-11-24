// --- Shadertoy Import Compatibility ---
// Adapter for imported Shadertoy-style code.
// HyperShader-native effects should include hypershader_standard.glsl instead.

#ifndef SHADERTOY_COMPAT_H
#define SHADERTOY_COMPAT_H

#include "hypershader_standard.glsl"

// Legacy spelling accepted only for imported compatibility shaders.
#define SHADERTOY_MAINIMAGE_WRAPPER HYPERSHADER_MAINIMAGE_WRAPPER

#endif
