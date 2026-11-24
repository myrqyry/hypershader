#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

#include "../hypershader_data.h"
#include <obs.h>

/* Initializes audio connection based on settings */
bool start_obs_audio_connection(struct hypershader_data *s);

/* Stops audio connection */
void stop_obs_audio_connection(struct hypershader_data *s);
bool start_source_audio_connection(struct hypershader_data *s);
void stop_source_audio_connection(struct hypershader_data *s);

/* Main audio processing thread function */
void *audio_worker(void *p);

/* Audio mix probing thread */
void *probe_audio_mixes_thread(void *param);

/* Sound preview thread (generates sine wave) */
void *sound_preview_thread_fn(void *param);

/* Computes FFT and updates audio textures/features (Internal helper, exposed if needed) */
void compute_audio_features(struct hypershader_data *s, float *window, int n);

#endif /* AUDIO_PROCESSOR_H */
