#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sight_matte_v1.h"

/* Union of pixels with R8 >=128; normalized outer pixel edges, centroid
 * of pixel centers, and foreground area fraction. Empty = all zeros. */
struct hs_sight_subject_stats {
    float bounds[4];
    float center[2];
    float coverage;
    bool present;
};

static inline struct hs_sight_subject_stats hs_sight_subject_measure(
    const uint8_t *pixels, uint32_t width, uint32_t height)
{
    struct hs_sight_subject_stats result = {0};
    if (!pixels || !width || !height || width>SIGHT_MATTE_MAX_EDGE ||
        height>SIGHT_MATTE_MAX_EDGE)
        return result;
    uint32_t count=0, xmin=width, ymin=height, xmax=0, ymax=0;
    uint64_t sumx=0, sumy=0;
    for (uint32_t y=0; y<height; ++y) {
        for (uint32_t x=0; x<width; ++x) {
            if (pixels[y*width+x]<128) continue;
            ++count;
            sumx+=x; sumy+=y;
            if (x<xmin) xmin=x;
            if (y<ymin) ymin=y;
            if (x>xmax) xmax=x;
            if (y>ymax) ymax=y;
        }
    }
    if (!count) return result;
    result.bounds[0]=(float)xmin/width;
    result.bounds[1]=(float)ymin/height;
    result.bounds[2]=(float)(xmax+1)/width;
    result.bounds[3]=(float)(ymax+1)/height;
    result.center[0]=((float)sumx/count+.5f)/width;
    result.center[1]=((float)sumy/count+.5f)/height;
    result.coverage=(float)count/(width*height);
    result.present=true;
    return result;
}
