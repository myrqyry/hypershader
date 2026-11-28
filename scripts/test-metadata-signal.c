#include "audio/metadata_signal.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    const float bands[8] = {10,11,12,13,14,15,16,17};
    const float music[HS_MUSIC_FIELDS] = {20,21,22,23,24,25,26};
    const struct { const char *tag; float value; } cases[] = {
        {"level",1},{"PEAK",2},{"beat",3},{"envelope",4},
        {"bass",10},{"mid",13},{"treble",16},
        {"band0",10},{"band1",11},{"band2",12},{"band3",13},
        {"band4",14},{"band5",15},{"band6",16},{"band7",17},
        {"kick",20},{"snare",21},{"flux",22},{"bpm",23},
        {"beat_phase",24},{"bar_phase",25},{"beat_confidence",26},
        {"band8",0},{"levelx",0},{"",0},{NULL,0}
    };
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i)
        assert(hs_metadata_signal_value(hs_metadata_signal_resolve(cases[i].tag),
            1,2,3,4,bands,music)==cases[i].value);
    puts("METADATA_SIGNAL_PASS=1");
}
