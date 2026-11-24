#ifndef HS_GRAPH_BUDGET_H
#define HS_GRAPH_BUDGET_H
#include <stdbool.h>
#include <stdint.h>

#define HS_GRAPH_MAX_MIB 512
#define HS_GRAPH_MIB UINT64_C(1048576)

static inline uint32_t hs_graph_extent(uint32_t full, uint32_t divisor)
{
    return full / divisor + (full % divisor != 0);
}

static inline bool hs_graph_add_surface(uint64_t *total, uint32_t width, uint32_t height,
                                       uint32_t bytes_per_pixel, uint32_t count)
{
    if (!width || !height || (bytes_per_pixel != 4 && bytes_per_pixel != 8) || !count)
        return false;
    uint64_t pixels = (uint64_t)width * height;
    uint64_t multiplier = (uint64_t)bytes_per_pixel * count;
    if (pixels > (UINT64_MAX - *total) / multiplier)
        return false;
    *total += pixels * multiplier;
    return true;
}

/* Logical color payload: reserve capture, both compositor surfaces and
 * transparent pixel even when compositing is currently off; two textures
 * per persistent buffer. Excludes unrelated source/ML/channel resources,
 * graphics-driver overhead and the caller's output target. */
static inline bool hs_graph_budget_bytes(uint32_t width, uint32_t height,
                                         uint32_t output_bpp, const uint32_t divisor[2],
                                         const uint32_t buffer_bpp[2], uint64_t *out)
{
    uint64_t total = 4;
    if (!hs_graph_add_surface(&total, width, height, output_bpp, 3))
        return false;
    for (unsigned i = 0; i < 2; i++) {
        uint32_t d = divisor[i];
        if (d != 1 && d != 2 && d != 4 && d != 8)
            return false;
        if (!hs_graph_add_surface(&total, hs_graph_extent(width, d),
                                  hs_graph_extent(height, d), buffer_bpp[i], 2))
            return false;
    }
    *out = total;
    return true;
}
#endif
