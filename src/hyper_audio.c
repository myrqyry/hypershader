#include "hyper_audio.h"
#include "audio_thread.h"
#include <math.h>
#include <util/threading.h>
#include <util/platform.h>
#include <obs-module.h>
#include "audio/audio_processor.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

bool hyper_audio_init(struct hypershader_data *s)
{
	if (!s)
		return false;

	// Default settings
	s->audio_sample_rate = 48000;
	s->audio_window_size = 1024;

	// Initialize atomic locks for audio
	pthread_mutex_init(&s->audio_lock, NULL);
	pthread_mutex_init(&s->audio_results_lock, NULL);
	pthread_cond_init(&s->audio_cv, NULL);

	// Initialize audio thread state machine
	if (!audio_thread_manager_init(&s->audio_thread_mgr)) {
		blog(LOG_ERROR, "[HyperShader] Failed to init audio thread manager");
		goto fail;
	}

	// Initialize FFT settings
	s->fft_n = 1;
	while (s->fft_n < s->audio_window_size)
		s->fft_n <<= 1;

	s->fft_cfg = kiss_fftr_alloc(s->fft_n, 0, NULL, NULL);
	s->fft_in = bzalloc(s->fft_n * sizeof(kiss_fft_scalar));
	s->fft_out = bzalloc((s->fft_n / 2 + 1) * sizeof(kiss_fft_cpx));
	s->fft_window = bzalloc(s->audio_window_size * sizeof(float));

	// Generate Window Function (Hann default)
	for (int i = 0; i < s->audio_window_size; i++) {
		s->fft_window[i] =
			0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)(s->audio_window_size - 1)));
	}

	s->fft_texture_history = bzalloc(AUDIO_TEX_WIDTH * sizeof(float));

	/* Initialize lock-free ring buffer (4x window size, power of 2) */
	size_t lockfree_capacity = s->audio_window_size * 4;
	size_t actual_capacity = 1;
	while (actual_capacity < lockfree_capacity)
		actual_capacity <<= 1;

	s->audio_lockfree_ring.data = bzalloc(actual_capacity * sizeof(float));
	s->audio_lockfree_ring.capacity = actual_capacity;
	atomic_store(&s->audio_lockfree_ring.write_pos, 0);
	atomic_store(&s->audio_lockfree_ring.read_pos, 0);

	/* Initialize timestamp and latency monitoring */
	s->last_audio_ts = 0;
	s->audio_latency_monitor.avg_process_time_ns = 0;
	s->audio_latency_monitor.consecutive_overruns = 0;

	if (!s->fft_cfg || !s->fft_in || !s->fft_out || !s->fft_window) {
		goto fail;
	}

	/* Check lock-free ring allocation */
	if (!s->audio_lockfree_ring.data) {
		blog(LOG_ERROR, "[HyperShader] Failed to allocate lock-free audio ring buffer");
		goto fail;
	}

	s->audio_system_healthy = true;
	return true;

fail:
	hyper_audio_destroy(s);
	return false;
}

void hyper_audio_destroy(struct hypershader_data *s)
{
	if (!s)
		return;

	if (s->fft_cfg) {
		kiss_fftr_free(s->fft_cfg);
		s->fft_cfg = NULL;
	}
	bfree(s->fft_in);
	s->fft_in = NULL;
	bfree(s->fft_out);
	s->fft_out = NULL;
	bfree(s->fft_window);
	s->fft_window = NULL;
	bfree(s->audio_status_message);
	s->audio_status_message = NULL;
	bfree(s->fft_texture_history);
	s->fft_texture_history = NULL;

	/* Free pre-allocated scratch buffer */
	bfree(s->audio_mono_scratch);
	s->audio_mono_scratch = NULL;
	s->audio_mono_scratch_size = 0;

	/* Free lock-free ring buffer */
	bfree(s->audio_lockfree_ring.data);
	s->audio_lockfree_ring.data = NULL;
	s->audio_lockfree_ring.capacity = 0;

	/* Free pre-allocated audio worker buffer */
	bfree(s->audio_localbuf);
	s->audio_localbuf = NULL;
	s->audio_localbuf_size = 0;

	/* Destroy thread manager */
	audio_thread_manager_destroy(&s->audio_thread_mgr);

	/* Destroy pthread primitives */
	pthread_mutex_destroy(&s->audio_lock);
	pthread_mutex_destroy(&s->audio_results_lock);
	pthread_cond_destroy(&s->audio_cv);
}

void hyper_audio_update_settings(struct hypershader_data *s, obs_data_t *settings)
{
	if (!s || !settings)
		return;

	int new_fft = (int)obs_data_get_int(settings, "audio_fft_size");
	int new_window = (int)obs_data_get_int(settings, "audio_window_type");
	if (new_fft <= 0)
		new_fft = 1024;

	/* The settings callback can run many times before the worker wakes up.
	 * Compare both active and pending configurations under the same lock so
	 * a single 0->1024 request doesn't get queued/logged every frame. */
	pthread_mutex_lock(&s->audio_lock);
	bool active_matches = new_fft == s->audio_fft_size && new_window == s->audio_window_type;
	bool pending_matches = atomic_load_explicit(&s->audio_reconfig_pending, memory_order_seq_cst) &&
		new_fft == s->next_audio_fft_size && new_window == s->next_audio_window_type;
	if (!active_matches && !pending_matches) {
		blog(LOG_INFO, "[HyperShader] Scheduling FFT reconfig: %d->%d", s->audio_fft_size, new_fft);
		s->next_audio_fft_size = new_fft;
		s->next_audio_window_type = new_window;
		atomic_store_explicit(&s->audio_reconfig_pending, true, memory_order_seq_cst);
		pthread_cond_signal(&s->audio_cv);
	}
	pthread_mutex_unlock(&s->audio_lock);
}

void hyper_audio_snapshot(struct hypershader_data *s, float *out_level, float *out_peak, float *out_bands,
			  float *out_beat, float *out_envelope, float *out_music, uint8_t *out_texture)
{
	if (!s)
		return;

	pthread_mutex_lock(&s->audio_results_lock);

	if (out_level)
		*out_level = s->audio_level;

	if (out_peak)
		*out_peak = s->audio_peak;

	// out_bands should be size 8
	if (out_bands) {
		// Assuming user passes correct size or we iterate 8
		// hypershader_data defines audio_bands as float[8]
		memcpy(out_bands, s->audio_bands, sizeof(float) * 8);
	}

	if (out_beat)
		*out_beat = s->audio_beat;

	if (out_envelope)
		*out_envelope = s->audio_envelope;
	if (out_music)
        memcpy(out_music,s->audio_music,sizeof(s->audio_music));

	// out_texture should be AUDIO_TEX_WIDTH * AUDIO_TEX_HEIGHT bytes (512*2 typically)
	if (out_texture) {
		memcpy(out_texture, s->audio_texture_data, sizeof(uint8_t) * AUDIO_TEX_WIDTH * AUDIO_TEX_HEIGHT);
	}

	pthread_mutex_unlock(&s->audio_results_lock);
}
