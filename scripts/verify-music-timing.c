#include "../src/audio/music_timing.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static int close_to(float a,float b,float e){return fabsf(a-b)<e;}
int main(void){
    enum{N=4096,BINS=N/2};
    float spectrum[BINS]={0};
    hs_music_timing music;
    hs_music_reset(&music);
    const unsigned kick=(unsigned)lround(94.0*N/48000.0);
    const unsigned snare=(unsigned)lround(1100.0*N/48000.0);
    int classified=1,steady=1,bpm=1,phase=1;
    for(unsigned i=0;i<80;++i){
        memset(spectrum,0,sizeof(spectrum));
        const int pulse=(i%10)==0;
        if(pulse)spectrum[kick]=.15f;
        hs_music_step(&music,spectrum,BINS,48000,N,.05f);
        if(pulse&&music.kick<.45f)classified=0;
        if(pulse&&music.snare>.02f)classified=0;
        if(!pulse&&music.kick>.05f)steady=0;
        if(i==40){
            bpm=music.confidence>.5f&&close_to(music.bpm,120.f,3.f);
            phase=music.beat_phase<.1f&&music.bar_phase>=0&&music.bar_phase<1.f;
        }
        if(i==45)phase=phase&&music.beat_phase>.4f&&music.beat_phase<.6f;
    }
    memset(spectrum,0,sizeof(spectrum));
    for(unsigned i=0;i<55;++i)hs_music_step(&music,spectrum,BINS,48000,N,.05f);
    int expired=music.bpm==0.f&&music.confidence==0.f&&music.beat_phase==0.f;
    hs_music_step(&music,spectrum,BINS,44100,N,.05f);
    int sample_rate_reset=music.sample_rate==44100&&music.bpm==0.f;
    hs_music_reset(&music);
    hs_music_step(&music,spectrum,BINS,48000,N,.05f);
    spectrum[snare]=.1f;
    hs_music_step(&music,spectrum,BINS,48000,N,.05f);
    int independent=music.snare>.45f&&music.kick<.05f;
    for(unsigned i=0;i<8;++i)hs_music_step(&music,spectrum,BINS,48000,N,.05f);
    int held=music.snare<.05f&&!music.kick;
    spectrum[snare]=NAN;
    hs_music_step(&music,spectrum,BINS,48000,N,.05f);
    int finite=isfinite(music.snare)&&isfinite(music.flux);
    hs_music_reset(&music);
    int reset=music.bpm==0.f&&music.confidence==0.f&&music.kick==0.f;
    printf("MUSIC_TIMING kick=%d quiet=%d snare=%d held=%d bpm=%d phase=%d finite=%d reset=%d expired=%d reconfigured=%d\n",
        classified,steady,independent,held,bpm,phase,finite,reset,expired,sample_rate_reset);
    return !(classified&&steady&&independent&&held&&bpm&&phase&&finite&&reset&&expired&&sample_rate_reset);
}
