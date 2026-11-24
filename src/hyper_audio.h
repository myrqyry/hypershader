#pragma once
#include <obs-module.h>
#include "hypershader_data.h"

// Initialize audio resources (FFT buffers, etc.)
bool hyper_audio_init(struct hypershader_data *s);

// Clean up audio resources
void hyper_audio_destroy(struct hypershader_data *s);

// Update FFT settings (reallocate if size changed)
void hyper_audio_update_settings(struct hypershader_data *s, obs_data_t *settings);

// Snapshot audio data for the render thread (Thread-Safe)
void hyper_audio_snapshot(struct hypershader_data *s, float *out_level, float *out_peak, float *out_bands,
			  float *out_beat, float *out_envelope, float *out_music, uint8_t *out_texture);
