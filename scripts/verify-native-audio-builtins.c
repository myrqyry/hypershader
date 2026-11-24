#include "hypershader_data.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    static const char *const injected[] = {
        "audio_level", "audio_peak", "audio_beat", "audio_envelope",
        "audio_kick", "audio_snare", "audio_flux", "audio_bpm",
        "audio_beat_phase", "audio_bar_phase", "audio_beat_confidence", "audio_reaction_state"
    };
    for (size_t i = 0; i < sizeof(injected) / sizeof(injected[0]); ++i)
        assert(is_builtin_uniform(injected[i]));
    for (unsigned int i = 0; i < 8; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "audio_band%u", i);
        assert(is_builtin_uniform(name));
    }
    static const char *const user_owned[] = {
        "audio_gain", "audio_attack", "audio_band8", "audio_band",
        "audio_band1_extra", "audio_level_override", "spectrum_band0",
        "iAudioKick", "iAudioBands0", ""
    };
    for (size_t i = 0; i < sizeof(user_owned) / sizeof(user_owned[0]); ++i)
        assert(!is_builtin_uniform(user_owned[i]));
    assert(!is_builtin_uniform(NULL));
    puts("NATIVE_AUDIO_BUILTIN_OWNERSHIP_PASS=1");
}
