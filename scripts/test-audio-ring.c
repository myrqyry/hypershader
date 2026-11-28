#include "audio/audio_ring.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>

static float queue[8], history[16];
static struct hs_audio_ring ring = {.data=queue,.capacity=8,.history=history,.history_capacity=16};

static void *produce(void *unused)
{
    (void)unused;
    for (int i=1; i<=100000; ++i) {
        float v=(float)i;
        while (hs_audio_ring_write(&ring,&v,1)!=1)
            sched_yield();
    }
    return NULL;
}

int main(void)
{
    float out[16], input[]={1,2,3,4,5,6,7,8,9,10,11,12};
    assert(hs_audio_ring_write(&ring,input,12)==8);
    assert(hs_audio_ring_write(&ring,input+8,4)==0);
    assert(hs_audio_ring_latest(&ring,out,8)==8);
    for(int i=0;i<8;i++) assert(out[i]==input[i]);
    assert(hs_audio_ring_latest(&ring,out,8)==0);
    assert(hs_audio_ring_write(&ring,input+8,4)==4);
    assert(hs_audio_ring_latest(&ring,out,8)==8);
    for(int i=0;i<8;i++) assert(out[i]==input[i+4]);
    assert(hs_audio_ring_write(&ring,input,8)==8);
    assert(hs_audio_ring_latest(&ring,out,16)==16);
    for(int i=0;i<8;i++) assert(out[i]==input[i+4]);
    for(int i=0;i<8;i++) assert(out[i+8]==input[i]);
    /* Consumer can drop queued stale samples when capture stops. */
    hs_audio_ring_write(&ring,input,4);
    hs_audio_ring_request_reset(&ring);
    /* A fresh capture may arrive before the consumer observes reset. */
    assert(hs_audio_ring_write(&ring,input+4,4)==4);
    assert(hs_audio_ring_latest(&ring,out,8)==4);
    for(int i=0;i<4;i++) assert(out[i]==input[i+4]);
    hs_audio_ring_discard(&ring);
    assert(hs_audio_ring_latest(&ring,out,8)==0);
    assert(hs_audio_ring_write(&ring,input+4,4)==4);
    assert(hs_audio_ring_latest(&ring,out,8)==4);
    for(int i=0;i<4;i++) assert(out[i]==input[i+4]);
    hs_audio_ring_discard(&ring);
    /* Consume every element in order with a competing producer; no overwrite. */
    pthread_t thread;
    assert(!pthread_create(&thread,NULL,produce,NULL));
    int expected=1;
    while(expected<=100000) {
        size_t n=hs_audio_ring_read(&ring,out,8);
        if(!n) sched_yield();
        for(size_t i=0;i<n;i++) assert(out[i]==(float)expected++);
    }
    assert(!pthread_join(thread,NULL));
    /* Monotonic unsigned sequence wrap remains valid. */
    atomic_store(&ring.write_pos,SIZE_MAX-3);
    atomic_store(&ring.read_pos,SIZE_MAX-3);
    assert(hs_audio_ring_write(&ring,input,8)==8);
    assert(hs_audio_ring_read(&ring,out,8)==8);
    for(int i=0;i<8;i++) assert(out[i]==input[i]);
    return 0;
}
