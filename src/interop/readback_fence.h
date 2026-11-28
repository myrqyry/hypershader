#pragma once
#include <stdbool.h>

/* All operations require the current OBS graphics context. A ready fence
 * establishes GPU completion before the staging map; unsupported backends
 * retain their existing bounded synchronous capture path. */
enum hs_readback_status {
    HS_READBACK_PENDING,
    HS_READBACK_READY,
    HS_READBACK_FAILED
};

bool hs_readback_fence_supported(void);
void *hs_readback_fence_insert(void);
enum hs_readback_status hs_readback_fence_poll(void *fence, bool flush);
void hs_readback_fence_destroy(void *fence);
