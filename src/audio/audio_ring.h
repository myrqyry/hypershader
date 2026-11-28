#ifndef HS_AUDIO_RING_H
#define HS_AUDIO_RING_H
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

/* One producer owns write_pos; one consumer owns read_pos and history.
 * All storage is preallocated. Both capacities must be powers of two.
 * On overflow drop incoming samples rather than overwrite unread memory. */
struct hs_audio_ring {
    float *data;
    size_t capacity;
    atomic_size_t write_pos, read_pos, reset_pos;
    atomic_bool reset_pending;
    float *history;
    size_t history_capacity, history_pos, history_count;
};

static inline size_t hs_audio_ring_write(struct hs_audio_ring *r, const float *in, size_t n)
{
    if (!r->data || !r->capacity || !n) return 0;
    size_t w=atomic_load_explicit(&r->write_pos,memory_order_relaxed);
    size_t rd=atomic_load_explicit(&r->read_pos,memory_order_acquire);
    size_t free_count=r->capacity-(w-rd);
    if(n>free_count) n=free_count;
    size_t start=w&(r->capacity-1), first=r->capacity-start;
    if(first>n) first=n;
    memcpy(r->data+start,in,first*sizeof(float));
    memcpy(r->data,in+first,(n-first)*sizeof(float));
    atomic_store_explicit(&r->write_pos,w+n,memory_order_release);
    return n;
}

static inline size_t hs_audio_ring_read(struct hs_audio_ring *r, float *out, size_t n)
{
    if(!r->data || !r->capacity || !n) return 0;
    size_t rd=atomic_load_explicit(&r->read_pos,memory_order_relaxed);
    size_t w=atomic_load_explicit(&r->write_pos,memory_order_acquire);
    if(n>w-rd) n=w-rd;
    size_t start=rd&(r->capacity-1), first=r->capacity-start;
    if(first>n) first=n;
    memcpy(out,r->data+start,first*sizeof(float));
    memcpy(out+first,r->data,(n-first)*sizeof(float));
    atomic_store_explicit(&r->read_pos,rd+n,memory_order_release);
    return n;
}

/* Called after the previous producer is disconnected, before a new
 * producer starts. The consumer alone advances its read sequence. */
static inline void hs_audio_ring_request_reset(struct hs_audio_ring *r)
{
    size_t w=atomic_load_explicit(&r->write_pos,memory_order_acquire);
    atomic_store_explicit(&r->reset_pos,w,memory_order_relaxed);
    atomic_store_explicit(&r->reset_pending,true,memory_order_release);
}

static inline void hs_audio_ring_apply_reset(struct hs_audio_ring *r)
{
    if(!atomic_exchange_explicit(&r->reset_pending,false,memory_order_acq_rel))
        return;
    size_t reset=atomic_load_explicit(&r->reset_pos,memory_order_acquire);
    size_t rd=atomic_load_explicit(&r->read_pos,memory_order_relaxed);
    size_t w=atomic_load_explicit(&r->write_pos,memory_order_acquire);
    if(reset-rd<=w-rd)
        atomic_store_explicit(&r->read_pos,reset,memory_order_release);
    r->history_count=0;
    r->history_pos=0;
}

/* Drain one bounded snapshot, retaining an overlapping FFT window locally.
 * No repeated FFT for the same capture block. */
static inline size_t hs_audio_ring_latest(struct hs_audio_ring *r, float *out, size_t n)
{
    if(!r->history || !r->history_capacity || !r->data || !r->capacity) return 0;
    hs_audio_ring_apply_reset(r);
    size_t rd=atomic_load_explicit(&r->read_pos,memory_order_relaxed);
    size_t w=atomic_load_explicit(&r->write_pos,memory_order_acquire);
    size_t remaining=w-rd;
    if(!remaining) return 0;
    while(remaining) {
        size_t span=r->history_capacity-r->history_pos;
        if(span>remaining) span=remaining;
        size_t got=hs_audio_ring_read(r,r->history+r->history_pos,span);
        if(!got) break;
        r->history_pos=(r->history_pos+got)&(r->history_capacity-1);
        r->history_count+=got;
        if(r->history_count>r->history_capacity) r->history_count=r->history_capacity;
        remaining-=got;
    }
    if(n>r->history_count) n=r->history_count;
    size_t start=(r->history_pos-n)&(r->history_capacity-1);
    size_t first=r->history_capacity-start;
    if(first>n) first=n;
    memcpy(out,r->history+start,first*sizeof(float));
    memcpy(out+first,r->history,(n-first)*sizeof(float));
    return n;
}

static inline void hs_audio_ring_discard(struct hs_audio_ring *r)
{
    size_t w=atomic_load_explicit(&r->write_pos,memory_order_acquire);
    atomic_store_explicit(&r->read_pos,w,memory_order_release);
    r->history_count=0;
    r->history_pos=0;
}
#endif
