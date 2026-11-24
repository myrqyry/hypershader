#ifndef HS_MUSIC_TIMING_H
#define HS_MUSIC_TIMING_H
#include <stdbool.h>
#include <stddef.h>
#define HS_MUSIC_MAX_BINS 8192
#define HS_MUSIC_FIELDS 7
enum {HS_MUSIC_KICK,HS_MUSIC_SNARE,HS_MUSIC_FLUX,HS_MUSIC_BPM,HS_MUSIC_BEAT_PHASE,HS_MUSIC_BAR_PHASE,HS_MUSIC_CONFIDENCE};
typedef struct hs_music_timing {
    float previous[HS_MUSIC_MAX_BINS];
    unsigned previous_count, sample_rate;
    double seconds,last_kick,last_snare,period,bar_beats,silence_since;
    float bpm,confidence,kick,snare,flux,beat_phase,bar_phase;
    unsigned reliable_intervals;
    bool have_kick,have_snare;
} hs_music_timing;
void hs_music_reset(hs_music_timing*);
void hs_music_step(hs_music_timing*,const float* magnitude,unsigned bins,
                   unsigned sample_rate,unsigned fft_size,float dt);
#endif
