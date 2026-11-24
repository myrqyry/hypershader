#include "music_timing.h"
#include <math.h>
#include <string.h>

static float clamp01(float x)
{
    if (!isfinite(x) || x <= 0.f) return 0.f;
    if (x >= 1.f) return 1.f;
    return x;
}

void hs_music_reset(hs_music_timing *s)
{
    if (s) memset(s, 0, sizeof(*s));
}

void hs_music_step(hs_music_timing *s, const float *magnitude, unsigned bins,
                   unsigned sample_rate, unsigned fft_size, float dt)
{
    if (!s) return;
    s->kick = s->snare = s->flux = 0.f;
    if (!magnitude || bins < 8 || bins > HS_MUSIC_MAX_BINS || !sample_rate ||
        fft_size < bins * 2 || fft_size > 16384 || !isfinite(dt) || dt <= 0.f ||
        dt > .25f) {
        hs_music_reset(s);
        return;
    }
    if (s->previous_count != bins || s->sample_rate != sample_rate) {
        hs_music_reset(s);
        s->previous_count = bins;
        s->sample_rate = sample_rate;
    }

    s->seconds += dt;
    double energy[3] = {0}, increase[3] = {0};
    for (unsigned i = 1; i < bins; ++i) {
        float value = magnitude[i];
        if (!isfinite(value) || value < 0) value = 0.f;
        if (value > 1000.f) value = 1000.f;
        const float difference = fmaxf(0.f, value - s->previous[i]);
        s->previous[i] = value;
        const double hz = (double)i * sample_rate / fft_size;
        energy[0] += value;
        increase[0] += difference;
        if (hz >= 55. && hz < 170.) {
            energy[1] += value;
            increase[1] += difference;
        } else if (hz >= 350. && hz < 4000.) {
            energy[2] += value;
            increase[2] += difference;
        }
    }
    s->flux = energy[0] >= .015 ? clamp01((float)(increase[0] / energy[0])) : 0.f;
    const float kick_flux = energy[1] >= .015 ? clamp01((float)(increase[1] / energy[1])) : 0.f;
    const float snare_flux = energy[2] >= .015 ? clamp01((float)(increase[2] / energy[2])) : 0.f;

    // These are spectrally selective transient impulses, not calibrated
    // drum classifiers. A bass synth may trigger kick, and vocal consonants
    // may trigger snare.
    if (kick_flux >= .4f &&
        (!s->have_kick || s->seconds - s->last_kick >= .15)) {
        const double interval = s->seconds - s->last_kick;
        if (s->have_kick && interval >= .25 && interval <= 2.) {
            if (s->period > 0 &&
                fabs(interval - s->period) <= s->period * .18) {
                if (s->reliable_intervals < 16) ++s->reliable_intervals;
                s->period = s->period * .80 + interval * .20;
            } else {
                s->period = interval;
                s->reliable_intervals = 1;
            }
            s->bpm = (float)(60. / s->period);
            s->confidence = clamp01((float)s->reliable_intervals / 3.f);
            s->bar_beats += 1.;
        } else if (s->have_kick) {
            s->period = 0.;
            s->bpm = s->confidence = 0.f;
            s->reliable_intervals = 0;
            s->bar_beats = 0.;
        }
        s->have_kick = true;
        s->last_kick = s->seconds;
        s->kick = kick_flux;
    }
    if (snare_flux >= .4f &&
        (!s->have_snare || s->seconds - s->last_snare >= .15)) {
        s->have_snare = true;
        s->last_snare = s->seconds;
        s->snare = snare_flux;
    }
    if (s->have_kick && s->seconds - s->last_kick > 2.) {
        s->have_kick = false;
        s->period = 0;
        s->bpm = s->confidence = s->beat_phase = s->bar_phase = 0.f;
        s->reliable_intervals = 0;
        s->bar_beats = 0;
    }
    if (s->period > 0 && s->have_kick) {
        const double elapsed = s->seconds - s->last_kick;
        s->beat_phase = (float)fmod(elapsed / s->period, 1.);
        s->bar_phase = (float)(fmod(s->bar_beats + elapsed / s->period, 4.) / 4.);
    }
}
