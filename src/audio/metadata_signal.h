#ifndef HS_METADATA_SIGNAL_H
#define HS_METADATA_SIGNAL_H
#include "music_timing.h"

enum hs_metadata_signal {
    HS_META_NONE, HS_META_LEVEL, HS_META_PEAK, HS_META_BEAT, HS_META_ENVELOPE,
    HS_META_BAND0, HS_META_MUSIC0 = HS_META_BAND0 + 8
};

static inline int hs_meta_ascii_equal(const char *a, const char *b)
{
    while (*a && *b) {
        char c = *a++;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != *b++) return 0;
    }
    return *a == *b;
}

static inline enum hs_metadata_signal hs_metadata_signal_resolve(const char *tag)
{
    if (!tag) return HS_META_NONE;
    static const char *const bands[] = {
        "band0", "band1", "band2", "band3", "band4", "band5", "band6", "band7"
    };
    static const char *const music[HS_MUSIC_FIELDS] = {
        [HS_MUSIC_KICK] = "kick", [HS_MUSIC_SNARE] = "snare",
        [HS_MUSIC_FLUX] = "flux", [HS_MUSIC_BPM] = "bpm",
        [HS_MUSIC_BEAT_PHASE] = "beat_phase", [HS_MUSIC_BAR_PHASE] = "bar_phase",
        [HS_MUSIC_CONFIDENCE] = "beat_confidence"
    };
    if (hs_meta_ascii_equal(tag, "level")) return HS_META_LEVEL;
    if (hs_meta_ascii_equal(tag, "peak")) return HS_META_PEAK;
    if (hs_meta_ascii_equal(tag, "beat")) return HS_META_BEAT;
    if (hs_meta_ascii_equal(tag, "envelope")) return HS_META_ENVELOPE;
    if (hs_meta_ascii_equal(tag, "bass")) return HS_META_BAND0;
    if (hs_meta_ascii_equal(tag, "mid")) return HS_META_BAND0 + 3;
    if (hs_meta_ascii_equal(tag, "treble")) return HS_META_BAND0 + 6;
    for (int i = 0; i < 8; ++i)
        if (hs_meta_ascii_equal(tag, bands[i])) return HS_META_BAND0 + i;
    for (int i = 0; i < HS_MUSIC_FIELDS; ++i)
        if (hs_meta_ascii_equal(tag, music[i])) return HS_META_MUSIC0 + i;
    return HS_META_NONE;
}

static inline float hs_metadata_signal_value(enum hs_metadata_signal signal,
    float level, float peak, float beat, float envelope,
    const float bands[8], const float music[HS_MUSIC_FIELDS])
{
    switch (signal) {
    case HS_META_LEVEL: return level;
    case HS_META_PEAK: return peak;
    case HS_META_BEAT: return beat;
    case HS_META_ENVELOPE: return envelope;
    default:
        if (signal >= HS_META_BAND0 && signal < HS_META_MUSIC0)
            return bands[signal - HS_META_BAND0];
        if (signal >= HS_META_MUSIC0 && signal < HS_META_MUSIC0 + HS_MUSIC_FIELDS)
            return music[signal - HS_META_MUSIC0];
        return 0.f;
    }
}
#endif
