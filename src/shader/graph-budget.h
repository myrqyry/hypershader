#ifndef HS_GRAPH_BUDGET_H
#define HS_GRAPH_BUDGET_H
#include <stdbool.h>
#include <stdint.h>

#define HS_GRAPH_MAX_MIB 512
#define HS_GRAPH_AGGREGATE_DEFAULT_MIB 1024
#define HS_GRAPH_AGGREGATE_MAX_MIB 16384
#define HS_GRAPH_MAX_PASSES 8
#define HS_GRAPH_MIB UINT64_C(1048576)

/* Graphics-context-owned reservation arithmetic. Failed admission is
 * non-mutating; the caller destroys old targets before relinquishing bytes. */
static inline bool hs_graph_reserve(uint64_t *total,uint64_t *owned,uint64_t bytes,uint64_t limit)
{
    if (*owned>*total) return false;
    const uint64_t others=*total-*owned;
    if (others>limit || bytes>limit-others) return false;
    *total=others+bytes;
    *owned=bytes;
    return true;
}

static inline void hs_graph_unreserve(uint64_t *total,uint64_t *owned)
{
    *total-=*owned;
    *owned=0;
}

static inline uint32_t hs_render_scale_percent(int64_t value)
{
    return value==25 || value==50 || value==75 || value==100 ? (uint32_t)value : 100U;
}

static inline uint32_t hs_render_scale_extent(uint32_t full, uint32_t percent)
{
    return (uint32_t)(((uint64_t)full * hs_render_scale_percent(percent) + 99) / 100);
}

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
                                         uint32_t output_bpp, const uint32_t *divisor,
                                         const uint32_t *buffer_bpp, unsigned count, uint64_t *out)
{
    if (!count || count > HS_GRAPH_MAX_PASSES)
        return false;
    uint64_t total = 4;
    if (!hs_graph_add_surface(&total, width, height, output_bpp, 3))
        return false;
    for (unsigned i = 0; i < count; i++) {
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
/* Immutable interval allocation. A transient producer stays live through its
 * last consumer (inclusive); equal descriptors alone are not sufficient.
 * Owners are pass indices, never shared with persistent history. */
static inline bool hs_graph_plan_slots(unsigned count, const uint32_t *divisor,
    const bool *float16, const bool *transient, const unsigned *last_use,
    unsigned *owner)
{
    if (!count || count > HS_GRAPH_MAX_PASSES) return false;
    unsigned end[HS_GRAPH_MAX_PASSES] = {0};
    for (unsigned i=0;i<count;i++) {
        owner[i]=i;
        if (!transient[i]) continue;
        if (last_use[i]<i || last_use[i]>count) return false;
        for (unsigned j=0;j<i;j++) {
            if (owner[j]==j && transient[j] && end[j]<i &&
                divisor[j]==divisor[i] && float16[j]==float16[i]) {
                owner[i]=j;
                break;
            }
        }
        end[owner[i]]=last_use[i];
    }
    return true;
}

static inline bool hs_graph_scaled_bytes(uint32_t width,uint32_t height,
    uint32_t render_width,uint32_t render_height,uint32_t output_bpp,const uint32_t *divisor,const uint32_t *buffer_bpp,
    const bool *transient,const unsigned *owner,unsigned count,uint64_t *out)
{
    if (!count || count>HS_GRAPH_MAX_PASSES) return false;
    uint64_t total=4;
    if (!hs_graph_add_surface(&total,width,height,output_bpp,2) ||
        !hs_graph_add_surface(&total,render_width,render_height,output_bpp,1)) return false;
    for (unsigned i=0;i<count;i++) {
        if (owner[i]>i || owner[owner[i]]!=owner[i]) return false;
        uint32_t d=divisor[i];
        if (d!=1 && d!=2 && d!=4 && d!=8) return false;
        if (owner[i]!=i) {
            if (!transient[i] || !transient[owner[i]] ||
                divisor[owner[i]]!=d || buffer_bpp[owner[i]]!=buffer_bpp[i])
                return false;
            continue;
        }
        if (!hs_graph_add_surface(&total,hs_graph_extent(render_width,d),
                hs_graph_extent(render_height,d),buffer_bpp[i],transient[i]?1:2))
            return false;
    }
    *out=total;
    return true;
}

static inline bool hs_graph_planned_bytes(uint32_t width,uint32_t height,
    uint32_t output_bpp,const uint32_t *divisor,const uint32_t *buffer_bpp,
    const bool *transient,const unsigned *owner,unsigned count,uint64_t *out)
{
    return hs_graph_scaled_bytes(width,height,width,height,output_bpp,divisor,
        buffer_bpp,transient,owner,count,out);
}

#endif
