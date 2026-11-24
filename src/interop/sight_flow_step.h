#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Only consume displacement once per completed graph pass sequence. */
static inline bool hs_sight_flow_step(uint64_t previous, uint64_t current, bool present)
{
    return present && previous != 0 && current > previous;
}
