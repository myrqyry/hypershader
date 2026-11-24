// Explicit external-format adapter. Never include from first-party effects.
#ifndef SHADERTOY_COMPAT_H
#define SHADERTOY_COMPAT_H
#include "hypershader_standard.glsl"

#define iTime hs_time
#define iTimeDelta hs_delta_time
#define iResolution hs_resolution
#define iFrame hs_frame_index
#define iFrameRate hs_frame_rate
#define iSampleRate hs_sample_rate
#define iMouse hs_pointer
#define iDate hs_date
#define iScale hs_scale
#define iProgress hs_transition_progress
#define iChannel0 hs_input0
#define iChannel1 hs_input1
#define iChannel2 hs_input2
#define iChannel3 hs_input3
#define iChannelResolution0 hs_input_resolution0
#define iChannelResolution1 hs_input_resolution1
#define iChannelResolution2 hs_input_resolution2
#define iChannelResolution3 hs_input_resolution3
#define iChannelResolution hs_input_resolution
#define mainImage hs_shade_pixel
#define SHADERTOY_MAINIMAGE_WRAPPER HYPERSHADER_PIXEL_WRAPPER

#endif
