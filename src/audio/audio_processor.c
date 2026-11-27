#include "audio_processor.h"
#include "../audio_thread.h"
#include "../source-selection.h"
#include <util/bmem.h>
#include <util/threading.h>
#include <util/platform.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ============================================================================
 * LOCK-FREE RING BUFFER
 * Eliminates mutex contention between OBS audio callback and worker thread
 * ============================================================================ */

/* Initialize lock-free ring buffer (capacity must be power of 2) */
static bool audio_lockfree_ring_init(struct hypershader_data *s, size_t capacity)
{
	/* Ensure capacity is power of 2 for fast modulo */
	size_t actual_capacity = 1;
	while (actual_capacity < capacity)
		actual_capacity <<= 1;

	s->audio_lockfree_ring.data = bzalloc(actual_capacity * sizeof(float));
	if (!s->audio_lockfree_ring.data)
		return false;

	s->audio_lockfree_ring.capacity = actual_capacity;
	atomic_store(&s->audio_lockfree_ring.write_pos, 0);
	atomic_store(&s->audio_lockfree_ring.read_pos, 0);
	return true;
}

/* Lock-free write from OBS audio callback (producer) */
static void audio_lockfree_ring_write(struct hypershader_data *s, const float *samples, size_t count)
{
	size_t capacity = s->audio_lockfree_ring.capacity;
	size_t mask = capacity - 1;
	size_t write_pos = atomic_load(&s->audio_lockfree_ring.write_pos);
	size_t read_pos = atomic_load(&s->audio_lockfree_ring.read_pos);

	/* Check available space (leave 1 slot to distinguish full from empty) */
	size_t used = (write_pos - read_pos) & mask;
	size_t available = capacity - 1 - used;

	if (count > available) {
		/* Buffer overflow - drop oldest samples by advancing read pointer */
		size_t overflow = count - available;
		atomic_store(&s->audio_lockfree_ring.read_pos, (read_pos + overflow) & mask);
	}

	/* Write samples */
	for (size_t i = 0; i < count; i++) {
		s->audio_lockfree_ring.data[(write_pos + i) & mask] = samples[i];
	}

	atomic_store(&s->audio_lockfree_ring.write_pos, (write_pos + count) & mask);
}

/* Lock-free read from worker thread (consumer) */
static size_t audio_lockfree_ring_read(struct hypershader_data *s, float *out, size_t count)
{
	size_t capacity = s->audio_lockfree_ring.capacity;
	size_t mask = capacity - 1;
	size_t write_pos = atomic_load(&s->audio_lockfree_ring.write_pos);
	size_t read_pos = atomic_load(&s->audio_lockfree_ring.read_pos);

	size_t available = (write_pos - read_pos) & mask;
	if (count > available)
		count = available;

	for (size_t i = 0; i < count; i++) {
		out[i] = s->audio_lockfree_ring.data[(read_pos + i) & mask];
	}

	atomic_store(&s->audio_lockfree_ring.read_pos, (read_pos + count) & mask);
	return count;
}

/* Peek at recent samples without consuming (for FFT window) */
static size_t audio_lockfree_ring_peek(struct hypershader_data *s, float *out, size_t count)
{
	size_t capacity = s->audio_lockfree_ring.capacity;
	size_t mask = capacity - 1;
	size_t write_pos = atomic_load(&s->audio_lockfree_ring.write_pos);
	size_t read_pos = atomic_load(&s->audio_lockfree_ring.read_pos);

	size_t available = (write_pos - read_pos) & mask;
	if (count > available)
		count = available;

	/* Read from end of buffer (most recent samples) */
	size_t start = (write_pos - count) & mask;
	for (size_t i = 0; i < count; i++) {
		out[i] = s->audio_lockfree_ring.data[(start + i) & mask];
	}

	return count;
}

/* Free lock-free ring buffer */
static void audio_lockfree_ring_free(struct hypershader_data *s)
{
	bfree(s->audio_lockfree_ring.data);
	s->audio_lockfree_ring.data = NULL;
	s->audio_lockfree_ring.capacity = 0;
}

/* Probe callback used during mix enumeration */
static void probe_audio_callback(void *param, size_t mix_idx, struct audio_data *data)
{
	(void)param;
	(void)mix_idx;
	(void)data;
}

/* Thread that probes mix indices and logs results */
void *probe_audio_mixes_thread(void *param)
{
	struct hypershader_data *s = param;
	audio_t *a = obs_get_audio();
	if (!a) {
		blog(LOG_WARNING, "[hypershader] No audio output handler available to probe mixes");
		return NULL;
	}

	const struct audio_output_info *info = audio_output_get_info(a);
	if (info) {
		blog(LOG_INFO, "[hypershader] Audio output info: name='%s' sr=%u format=%d speakers=%d",
		     info->name ? info->name : "(unnamed)", info->samples_per_sec, (int)info->format,
		     (int)info->speakers);
	}

	for (size_t i = 0; i < MAX_AUDIO_MIXES; ++i) {
		struct audio_convert_info conv;
		memset(&conv, 0, sizeof(conv));
		conv.samples_per_sec = s ? s->audio_sample_rate : 48000;
		conv.format = AUDIO_FORMAT_FLOAT;
		conv.speakers = SPEAKERS_STEREO;

		bool ok = audio_output_connect(a, i, &conv, probe_audio_callback, NULL);
		if (ok) {
			blog(LOG_INFO, "[hypershader] Mix %zu: connect OK", i);
			audio_output_disconnect(a, i, probe_audio_callback, NULL);
		} else {
			blog(LOG_INFO, "[hypershader] Mix %zu: connect FAILED", i);
		}
	}

	if (s)
		s->probe_running = false;
	return NULL;
}

/* ============================================================================
 * MINIAUDIO INTEGRATION FOR CROSS-PLATFORM AUDIO PREVIEW
 * ============================================================================ */

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include "miniaudio.h"

/* Preview state for miniaudio callback */
struct preview_state {
	float frequency;
	float phase;
	int sample_rate;
	int samples_remaining;
	volatile bool stop_requested;
};

/* Miniaudio callback for generating sine wave */
static void preview_data_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
	struct preview_state *state = (struct preview_state *)pDevice->pUserData;
	float *out = (float *)pOutput;

	(void)pInput; /* Unused */

	if (state->stop_requested || state->samples_remaining <= 0) {
		memset(pOutput, 0, frameCount * sizeof(float) * 2); /* Stereo silence */
		return;
	}

	ma_uint32 frames_to_generate = frameCount;
	if ((int)frames_to_generate > state->samples_remaining) {
		frames_to_generate = (ma_uint32)state->samples_remaining;
	}

	float angular_freq = 2.0f * (float)M_PI * state->frequency / (float)state->sample_rate;

	for (ma_uint32 i = 0; i < frames_to_generate; i++) {
		float sample = 0.5f * sinf(state->phase);
		out[i * 2 + 0] = sample; /* Left */
		out[i * 2 + 1] = sample; /* Right */
		state->phase += angular_freq;
		if (state->phase > 2.0f * (float)M_PI) {
			state->phase -= 2.0f * (float)M_PI;
		}
	}

	/* Silence remaining frames if we ran out */
	for (ma_uint32 i = frames_to_generate; i < frameCount; i++) {
		out[i * 2 + 0] = 0.0f;
		out[i * 2 + 1] = 0.0f;
	}

	state->samples_remaining -= (int)frames_to_generate;
}

void *sound_preview_thread_fn(void *param)
{
	struct hypershader_data *s = param;
	if (!s)
		return NULL;
	s->sound_preview_running = true;

	int sr = s->audio_sample_rate > 0 ? s->audio_sample_rate : 48000;
	float duration = 3.0f;

	/* Setup preview state */
	struct preview_state state;
	state.frequency = 440.0f;
	state.phase = 0.0f;
	state.sample_rate = sr;
	state.samples_remaining = (int)(duration * sr);
	state.stop_requested = false;

	/* Configure miniaudio device */
	ma_device_config config = ma_device_config_init(ma_device_type_playback);
	config.playback.format = ma_format_f32;
	config.playback.channels = 2;
	config.sampleRate = (ma_uint32)sr;
	config.dataCallback = preview_data_callback;
	config.pUserData = &state;

	ma_device device;
	if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
		blog(LOG_ERROR, "[hypershader] Failed to initialize miniaudio device for preview");
		s->sound_preview_running = false;
		return NULL;
	}

	if (ma_device_start(&device) != MA_SUCCESS) {
		blog(LOG_ERROR, "[hypershader] Failed to start miniaudio device for preview");
		ma_device_uninit(&device);
		s->sound_preview_running = false;
		return NULL;
	}

	blog(LOG_INFO, "[hypershader] Audio preview started (miniaudio, sr=%d, duration=%.1fs)", sr, duration);

	/* Wait for playback to complete or thread cancellation */
	while (state.samples_remaining > 0 && !state.stop_requested) {
		os_sleep_ms(100);
		/* Check if filter is being destroyed */
		if (!s->sound_preview_running) {
			state.stop_requested = true;
			break;
		}
	}

	ma_device_stop(&device);
	ma_device_uninit(&device);

	blog(LOG_INFO, "[hypershader] Audio preview finished");
	s->sound_preview_running = false;
	return NULL;
}

/* Both OBS mix capture and per-source capture deliver planar float audio. */
static void receive_audio_samples(struct hypershader_data *s, const struct audio_data *data, bool muted)
{
	if (!s || !data || data->frames == 0)
		return;
	/* audio_output_connect requests planar stereo float samples. Each data
	 * pointer contains an independent channel, not interleaved L/R pairs. */
	float *pl0 = (float *)data->data[0];
	float *pl1 = NULL;
	if (data->data[1])
		pl1 = (float *)data->data[1];
	uint32_t frames = data->frames;

	/* Lock-free ring buffer (always initialized after successful init) */
	if (!s->audio_lockfree_ring.data) {
		return;
	}

	atomic_store_explicit(&s->last_audio_capture_ns, os_gettime_ns(), memory_order_release);

	/* Convert stereo to mono using pre-allocated scratch buffer (avoid RT allocation) */
	float *mono_buf = s->audio_mono_scratch;
	size_t use_frames = frames;

	/* Bounds check against scratch buffer size */
	if (!mono_buf || frames > s->audio_mono_scratch_size) {
		/* Fallback: write directly to ring without stereo-to-mono conversion */
		if (pl0 && !muted)
			audio_lockfree_ring_write(s, pl0, frames);
		else if (muted) {
			/* Silence rather than retaining another source's last FFT window. */
			static const float zeros[256] = {0};
			for (size_t offset = 0; offset < frames; offset += 256) {
				size_t n = frames - offset < 256 ? frames - offset : 256;
				audio_lockfree_ring_write(s, zeros, n);
			}
		}
		return;
	}

	for (uint32_t i = 0; i < use_frames; ++i) {
		float v = muted ? 0.0f : (pl0 ? pl0[i] : 0.0f);
		if (pl1 && !muted)
			v = (v + pl1[i]) * 0.5f;
		mono_buf[i] = v;
	}
	audio_lockfree_ring_write(s, mono_buf, use_frames);
}

static void obs_audio_callback(void *param, size_t mix_idx, struct audio_data *data)
{
	(void)mix_idx;
	receive_audio_samples(param, data, false);
}

static void source_audio_callback(void *param, obs_source_t *source, const struct audio_data *data, bool muted)
{
	(void)source;
	receive_audio_samples(param, data, muted);
}

bool start_source_audio_connection(struct hypershader_data *s)
{
	if (!s || !s->audio_source || !s->audio_source[0])
		return false;
	if (s->source_audio_connected)
		return true;
	obs_source_t *source = hs_source_selection_resolve(s->audio_source, NULL);
	if (!source)
		return false;
	if (obs_source_removed(source) || !(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) ||
	    source == s->context) {
		obs_source_release(source);
		return false;
	}
	s->selected_audio_source = source;
	obs_source_add_audio_capture_callback(source, source_audio_callback, s);
	s->source_audio_connected = true;
	blog(LOG_INFO, "[hypershader] Capturing audio from source %s", obs_source_get_name(source));
	return true;
}

/* The beat envelope is driven only while audio flows. Clearing it on
 * disconnect keeps a shader from holding a permanent pulse after its source
 * goes away (the FFT level is intentionally left as-is: pre-existing behavior). */
static void reset_beat_state(struct hypershader_data *s)
{
	if (!s)
		return;

	pthread_mutex_lock(&s->audio_results_lock);
	s->audio_beat = 0.0f;
	s->audio_envelope = 0.0f;
	s->audio_beat_snapshot = 0.0f;
	s->audio_envelope_snapshot = 0.0f;
	s->beat_average = 0.0f;
	s->beat_cooldown = 0.0f;
    hs_music_reset(&s->music_timing);
    memset(s->audio_music,0,sizeof(s->audio_music));
	pthread_mutex_unlock(&s->audio_results_lock);
}

void stop_source_audio_connection(struct hypershader_data *s)
{
	if (!s || !s->selected_audio_source)
		return;

	reset_beat_state(s);

	obs_source_remove_audio_capture_callback(s->selected_audio_source, source_audio_callback, s);
	atomic_store_explicit(&s->last_audio_capture_ns, 0, memory_order_release);
	blog(LOG_INFO, "[hypershader] Disconnected from OBS audio source %s",
	     obs_source_get_name(s->selected_audio_source));
	obs_source_release(s->selected_audio_source);
	s->selected_audio_source = NULL;
	s->source_audio_connected = false;
}

bool start_obs_audio_connection(struct hypershader_data *s)
{
	if (!s)
		return false;
	if (s->audio_connected)
		return true;

	audio_t *a = obs_get_audio();
	if (!a)
		return false;

	struct audio_convert_info conv;
	memset(&conv, 0, sizeof(conv));
	conv.samples_per_sec = s->audio_sample_rate;
	conv.format = AUDIO_FORMAT_FLOAT_PLANAR;
	conv.speakers = SPEAKERS_STEREO;

	size_t mix_idx = s->audio_mix_idx; /* use user-selected mix */
	if (!audio_output_connect(a, mix_idx, &conv, obs_audio_callback, s)) {
		blog(LOG_ERROR, "[hypershader] Failed to connect to audio output mix %zu", mix_idx);
		return false;
	}

	s->obs_audio = a;
	s->audio_connected = true;
	s->audio_mix_idx = mix_idx;
	s->audio_sample_rate = audio_output_get_sample_rate(a);
	blog(LOG_INFO, "[hypershader] Connected to OBS audio mix %zu (sr=%d)", mix_idx, s->audio_sample_rate);
	return true;
}

void stop_obs_audio_connection(struct hypershader_data *s)
{
	if (!s || !s->audio_connected)
		return;

	/* audio_output_disconnect takes (audio_t*, mix_idx, callback, param) in current libobs */
	audio_output_disconnect(s->obs_audio, s->audio_mix_idx, obs_audio_callback, s);
	atomic_store_explicit(&s->last_audio_capture_ns, 0, memory_order_release);
	reset_beat_state(s);
	s->audio_connected = false;
	blog(LOG_INFO, "[hypershader] Disconnected from OBS audio mix %zu", s->audio_mix_idx);
}

/* Internal helpers for FFT locking */
static inline void fft_cfg_lock(struct hypershader_data *s)
{
	(void)s; /* no-op or specific lock if adding one */
}
static inline void fft_cfg_unlock(struct hypershader_data *s)
{
	(void)s;
}

void compute_audio_features(struct hypershader_data *s, float *window, int n)
{
	if (!s || !window || n <= 0)
		return;

	/* Windowing (Apply Hanning/Hamming) */
	/* Assumes n == s->audio_window_size */
	if (n != s->audio_window_size) {
		/* Mismatch */
		return;
	}

	for (int i = 0; i < n; i++) {
		s->fft_in[i] = window[i] * s->fft_window[i];
	}

	/* Perform FFT */
	if (s->fft_cfg) {
		kiss_fftr(s->fft_cfg, s->fft_in, s->fft_out);
	}

	/* Analyze spectrum (first half) */
	float rms = 0.0f;
	float peak = 0.0f;
	int bins = n / 2;
	if (bins > AUDIO_TEX_WIDTH)
		bins = AUDIO_TEX_WIDTH; /* cap */

	/* Group bins by actual Hz, not by eight equal FFT index ranges.
	 * Equal bin ranges put everything below 3 kHz into the 'bass' band
	 * for a 1024-point / 48 kHz FFT, leaving the mid/treble controls inert.
	 * These crossovers yield useful low/mid/high groups for the built-in
	 * audio_band0..7 controls, with Nyquist handled automatically. */
	static const float upper_hz[7] = {125.0f, 250.0f, 500.0f, 1000.0f,
				       2000.0f, 4000.0f, 8000.0f};
	float band_energy[8] = {0};
	float band_vals[8] = {0};
	const float bin_hz = (float)s->audio_sample_rate / (float)n;

	for (int i = 0; i < bins; i++) {
		float mag = sqrtf(s->fft_out[i].r * s->fft_out[i].r + s->fft_out[i].i * s->fft_out[i].i);
		/* normalize roughly */
		mag /= (float)n;

		rms += mag * mag;
		if (mag > peak)
			peak = mag;

		int band = 0;
		float frequency_hz = (float)i * bin_hz;
		while (band < 7 && frequency_hz >= upper_hz[band])
			band++;
		band_energy[band] += mag * mag;
	}

	rms = sqrtf(rms / (float)bins);
	/* Scale responses */
	peak *= 4.0f; /* empirical boost */
	rms *= 10.0f;

	/* Energy integration preserves narrow tones without diluting them by
	 * the hundreds of near-silent FFT bins in upper frequency bands. */
	for (int b = 0; b < 8; b++)
		band_vals[b] = fminf(1.0f, 2.0f * sqrtf(band_energy[b]));

	/* Update State (Smoothing) */
	pthread_mutex_lock(&s->audio_results_lock);
	float alpha = s->audio_smoothing;
	s->audio_level = s->audio_level * alpha + rms * (1.0f - alpha);
	s->audio_peak = s->audio_peak * alpha + peak * (1.0f - alpha);
	for (int b = 0; b < 8; b++) {
		s->audio_bands[b] = s->audio_bands[b] * alpha + band_vals[b] * (1.0f - alpha);
	}

	/* Onset / beat detection on the smoothed broadband level.
	 * An onset is a level jump above a fast moving average; each onset kicks an
	 * envelope that decays with a release constant, and a cooldown blocks
	 * re-triggering. No FFT work is added: this reuses the level that was just
	 * computed, so audio-reactive shaders gain beat timing without any
	 * per-frame analysis of their own.
	 *
	 * The threshold is relative to the running average, not an absolute level:
	 * this pipeline's audio_level is an empirical RMS scale (a loud tone sits
	 * near 0.1), so a fixed 0..1 threshold would never fire. */
	{
		uint64_t now_ns = os_gettime_ns();
		float beat_dt = 0.0f;
		if (s->last_audio_ts > 0 && now_ns > s->last_audio_ts)
			beat_dt = (float)((double)(now_ns - s->last_audio_ts) / 1.0e9);
		if (beat_dt < 0.001f)
			beat_dt = 0.001f;
		else if (beat_dt > 0.2f)
			beat_dt = 0.2f;

		float drive = fmaxf(0.0f, s->audio_level);
		if (drive > 1.0f)
			drive = 1.0f;

		s->beat_cooldown -= beat_dt;
		if (s->beat_cooldown < 0.0f)
			s->beat_cooldown = 0.0f;

		/* Fast attack / slower release so the average tracks the current pulse
		 * and leaves headroom for the next jump. */
		float beat_tau = (drive > s->beat_average) ? 0.08f : 0.25f;
		s->beat_average += (drive - s->beat_average) * (1.0f - expf(-beat_dt / beat_tau));

		const float jump = drive - s->beat_average;
		const float threshold = fmaxf(0.02f, s->beat_average * 0.30f);
		const bool onset = drive > 0.03f && jump > threshold;

		if (onset && s->beat_cooldown <= 0.0f) {
			s->audio_envelope = fmaxf(s->audio_envelope, 0.4f + 0.6f * drive);
			s->audio_beat = 1.0f;
			s->beat_cooldown = 0.18f;
		} else {
			s->audio_envelope += (0.0f - s->audio_envelope) * (1.0f - expf(-beat_dt / 0.22f));
		}

		/* The beat impulse is much shorter than the envelope. */
		s->audio_beat += (0.0f - s->audio_beat) * (1.0f - expf(-beat_dt / 0.06f));

        // Reuse the existing FFT output without another audio capture or FFT.
        float magnitudes[HS_MUSIC_MAX_BINS];
        unsigned count=s->fft_n>0?(unsigned)s->fft_n/2:0u;
        if(s->fft_out&&count>0&&count<=HS_MUSIC_MAX_BINS){
            for(unsigned bin=0;bin<count;++bin){
                const float re=s->fft_out[bin].r,im=s->fft_out[bin].i;
                magnitudes[bin]=sqrtf(re*re+im*im)/(float)s->fft_n;
            }
            hs_music_step(&s->music_timing,magnitudes,count,
                          (unsigned)s->audio_sample_rate,(unsigned)s->fft_n,beat_dt);
        }else hs_music_reset(&s->music_timing);
        s->audio_music[HS_MUSIC_KICK]=s->music_timing.kick;
        s->audio_music[HS_MUSIC_SNARE]=s->music_timing.snare;
        s->audio_music[HS_MUSIC_FLUX]=s->music_timing.flux;
        s->audio_music[HS_MUSIC_BPM]=s->music_timing.bpm;
        s->audio_music[HS_MUSIC_BEAT_PHASE]=s->music_timing.beat_phase;
        s->audio_music[HS_MUSIC_BAR_PHASE]=s->music_timing.bar_phase;
        s->audio_music[HS_MUSIC_CONFIDENCE]=s->music_timing.confidence;
		s->audio_beat_snapshot = s->audio_beat;
		s->audio_envelope_snapshot = s->audio_envelope;
	}

	/* Fill Audio Texture Row (FFT Spectrum) */
	/* Row 0: FFT frequency data (normalized) */
	/* hypershader audio texture: 512 pixels wide; data[0].x = frequency magnitude */

	int nbins = s->audio_window_size / 2;
	float smooth_factor = 0.5f; // Texture smoothing

	if (s->fft_texture_history) {
		for (int i = 0; i < AUDIO_TEX_WIDTH; i++) {
			/* Map pixel X to frequency bin with improved interpolation for small FFT sizes */
			float findex = (float)i * (float)nbins / (float)AUDIO_TEX_WIDTH;
			int i0 = (int)findex;
			int i1 = (i0 + 1 < nbins) ? i0 + 1 : i0;
			float t = findex - (float)i0;

			/* Handle edge cases for small FFT sizes */
			if (nbins <= 1) {
				/* Not enough bins for interpolation, use single bin */
				i0 = 0;
				i1 = 0;
				t = 0.0f;
			} else if (i0 >= nbins) {
				/* Clamp to last bin */
				i0 = nbins - 1;
				i1 = nbins - 1;
				t = 0.0f;
			}

			float mag0 = sqrtf(s->fft_out[i0].r * s->fft_out[i0].r + s->fft_out[i0].i * s->fft_out[i0].i);
			float mag1 = sqrtf(s->fft_out[i1].r * s->fft_out[i1].r + s->fft_out[i1].i * s->fft_out[i1].i);
			float mag = (mag0 * (1.0f - t) + mag1 * t);

			/* Normalize and scale */
			mag /= (float)s->audio_window_size;
			mag *= 0.15f;

			/* Apply temporal smoothing */
			s->fft_texture_history[i] =
				s->fft_texture_history[i] * smooth_factor + mag * (1.0f - smooth_factor);
			mag = s->fft_texture_history[i];

			int spectrum = (int)(mag * 255.0f);
			if (spectrum < 0)
				spectrum = 0;
			if (spectrum > 255)
				spectrum = 255;
			s->audio_texture_data[i] = (uint8_t)spectrum;
		}
	} else {
		/* Fallback if no history buffer (should not happen) */
		for (int i = 0; i < AUDIO_TEX_WIDTH; i++) {
			float findex = (float)i * (float)nbins / (float)AUDIO_TEX_WIDTH;
			int i0 = (int)findex;
			int i1 = (i0 + 1 < nbins) ? i0 + 1 : i0;
			float t = findex - (float)i0;

			/* Handle edge cases for small FFT sizes */
			if (nbins <= 1) {
				/* Not enough bins for interpolation, use single bin */
				i0 = 0;
				i1 = 0;
				t = 0.0f;
			} else if (i0 >= nbins) {
				/* Clamp to last bin */
				i0 = nbins - 1;
				i1 = nbins - 1;
				t = 0.0f;
			}

			float mag0 = sqrtf(s->fft_out[i0].r * s->fft_out[i0].r + s->fft_out[i0].i * s->fft_out[i0].i);
			float mag1 = sqrtf(s->fft_out[i1].r * s->fft_out[i1].r + s->fft_out[i1].i * s->fft_out[i1].i);
			float mag = (mag0 * (1.0f - t) + mag1 * t) * 0.15f;

			int spectrum = (int)(mag * 255.0f);
			s->audio_texture_data[i] = (uint8_t)(spectrum > 255 ? 255 : (spectrum < 0 ? 0 : spectrum));
		}
	}

	/* The second row is the actual latest PCM waveform. A zero sample is
	 * encoded as 128; each of the 512 texels covers one point in the
	 * current audio window. It shares the worker's results lock and GPU
	 * texture upload, without allocations or callbacks on the render thread. */
	for (int x = 0; x < AUDIO_TEX_WIDTH; x++) {
		int index = (int)((size_t)x * (size_t)n / AUDIO_TEX_WIDTH);
		if (index >= n)
			index = n - 1;
		float pcm = window[index];
		if (!isfinite(pcm))
			pcm = 0.0f;
		pcm = fmaxf(-1.0f, fminf(1.0f, pcm));
		s->audio_texture_data[AUDIO_TEX_WIDTH + x] =
			(uint8_t)lroundf((pcm + 1.0f) * 127.5f);
	}

	pthread_mutex_unlock(&s->audio_results_lock);
}

void *audio_worker(void *p)
{
	struct hypershader_data *s = p;
	uint64_t last_process_ts = 0;

	/* Signal that worker thread is fully initialized */
	audio_thread_set_running(&s->audio_thread_mgr);

	while (!audio_thread_should_stop(&s->audio_thread_mgr)) {
		/* Use 21ms tick matching libobs audio tick rate (1024 samples @ 48kHz) */
		int sleep_ms = 21;

		/* Use condition variable for efficient sleep that can be interrupted */
		pthread_mutex_lock(&s->audio_lock);
		if (!audio_thread_should_stop(&s->audio_thread_mgr)) {
			struct timespec ts;
			timespec_get(&ts, TIME_UTC);
			long ns = ts.tv_nsec + (sleep_ms * 1000000L);
			ts.tv_sec += ns / 1000000000L;
			ts.tv_nsec = ns % 1000000000L;
			pthread_cond_timedwait(&s->audio_cv, &s->audio_lock, &ts);
		}
		pthread_mutex_unlock(&s->audio_lock);

		/* Timestamp validation - prevent over-sampling */
		uint64_t current_ts = os_gettime_ns();
		uint64_t expected_interval = 21333333ULL; /* ~21.33ms in nanoseconds */

		if (last_process_ts > 0 && (current_ts - last_process_ts) < expected_interval / 2) {
			/* Woke up too early, skip this iteration */
			continue;
		}

		/* Validate monotonic timestamps */
		if (s->last_audio_ts > 0 && current_ts < s->last_audio_ts) {
			blog(LOG_WARNING, "[hypershader] Audio timestamp went backward, resetting");
			s->last_audio_ts = 0;
		}

		/* Handle deferred reconfiguration if pending */
		if (atomic_load_explicit(&s->audio_reconfig_pending, memory_order_seq_cst)) {
			pthread_mutex_lock(&s->audio_lock);
			int next_fft = s->next_audio_fft_size;
			int next_window = s->next_audio_window_type;

			blog(LOG_INFO, "[hypershader] Worker: Reconfiguring FFT to %d (window %d)", next_fft,
			     next_window);

			/* Calculate new FFT size */
			int new_fft_n = 1;
			while (new_fft_n < next_fft)
				new_fft_n <<= 1;

			/* Allocate new resources */
			kiss_fftr_cfg new_cfg = kiss_fftr_alloc(new_fft_n, 0, NULL, NULL);
			kiss_fft_scalar *new_in = (kiss_fft_scalar *)bzalloc(new_fft_n * sizeof(kiss_fft_scalar));
			kiss_fft_cpx *new_out = (kiss_fft_cpx *)bzalloc((new_fft_n / 2 + 1) * sizeof(kiss_fft_cpx));
			float *new_window_buf = (float *)bzalloc(next_fft * sizeof(float));

			if (new_cfg && new_in && new_out && new_window_buf) {
				/* Swap and free old */
				if (s->fft_cfg)
					kiss_fftr_free(s->fft_cfg);
				bfree(s->fft_in);
				bfree(s->fft_out);
				bfree(s->fft_window);

				s->fft_cfg = new_cfg;
				s->fft_in = new_in;
				s->fft_out = new_out;
				s->fft_window = new_window_buf;
				s->fft_n = new_fft_n;
				s->audio_fft_size = next_fft;
				s->audio_window_type = next_window;
				s->audio_window_size = next_fft;

				/* Generate window */
				if (s->audio_window_type == 1 /* AUDIO_WIN_HANN */) {
					for (int i = 0; i < s->audio_window_size; i++)
						s->fft_window[i] =
							0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i /
									    (float)(s->audio_window_size - 1)));
				} else if (s->audio_window_type == 2 /* AUDIO_WIN_HAMMING */) {
					for (int i = 0; i < s->audio_window_size; i++)
						s->fft_window[i] =
							0.54f - 0.46f * cosf(2.0f * (float)M_PI * (float)i /
									     (float)(s->audio_window_size - 1));
				} else {
					for (int i = 0; i < s->audio_window_size; i++)
						s->fft_window[i] = 1.0f;
				}
				s->audio_system_healthy = true;
			} else {
				blog(LOG_ERROR, "[hypershader] Worker: FFT Reconfig FAILED");
				if (new_cfg)
					kiss_fftr_free(new_cfg);
				bfree(new_in);
				bfree(new_out);
				bfree(new_window_buf);
				s->audio_system_healthy = false;
			}

			atomic_store_explicit(&s->audio_reconfig_pending, false, memory_order_seq_cst);
			pthread_mutex_unlock(&s->audio_lock);
		}

		/* make local copy of latest window from lock-free ring (use pre-allocated buffer) */
		float *localbuf = s->audio_localbuf;
		int n = 0;

		if (s->audio_localbuf && s->audio_localbuf_size >= (size_t)s->audio_window_size) {
			if (s->audio_lockfree_ring.data) {
				n = (int)audio_lockfree_ring_peek(s, localbuf, s->audio_window_size);
			}

			if (n > 0) {
				uint64_t process_start = os_gettime_ns();

				compute_audio_features(s, localbuf, n);

				uint64_t process_duration = os_gettime_ns() - process_start;
				if (s->audio_latency_monitor.avg_process_time_ns == 0) {
					s->audio_latency_monitor.avg_process_time_ns = process_duration;
				} else {
					s->audio_latency_monitor.avg_process_time_ns =
						(s->audio_latency_monitor.avg_process_time_ns * 9 + process_duration) /
						10;
				}

				uint64_t tick_budget = 21333333ULL / 2;
				if (process_duration > tick_budget) {
					s->audio_latency_monitor.consecutive_overruns++;
					if (s->audio_latency_monitor.consecutive_overruns > 3) {
						blog(LOG_WARNING,
						     "[hypershader] Audio processing overruns detected "
						     "(avg=%.2fms), consider reducing FFT size",
						     (float)s->audio_latency_monitor.avg_process_time_ns / 1000000.0f);
						s->audio_latency_monitor.consecutive_overruns = 0;
					}
				} else {
					s->audio_latency_monitor.consecutive_overruns = 0;
				}

				last_process_ts = current_ts;
				s->last_audio_ts = current_ts;

				if (audio_thread_should_stop(&s->audio_thread_mgr))
					break;
			} else {
				bool gen = false;
				pthread_mutex_lock(&s->audio_lock);
				gen = s->audio_test_tone;
				float phase = s->audio_phase;
				pthread_mutex_unlock(&s->audio_lock);

				if (gen) {
					float sr = (float)s->audio_sample_rate;
					for (int i = 0; i < s->audio_window_size; i++) {
						localbuf[i] = 0.05f * sinf(phase);
						phase += 2.0f * (float)M_PI * 440.0f / sr;
						if (phase > 2.0f * (float)M_PI)
							phase -= 2.0f * (float)M_PI;
					}
					pthread_mutex_lock(&s->audio_lock);
					s->audio_phase = phase;
					pthread_mutex_unlock(&s->audio_lock);
					compute_audio_features(s, localbuf, s->audio_window_size);
				atomic_store_explicit(&s->last_audio_capture_ns, os_gettime_ns(), memory_order_release);
				}
			}

			if (s->audio_debug) {
				s->audio_debug_tick++;
				if (s->audio_debug_tick >= 47) {
					s->audio_debug_tick = 0;
					bool significant_change =
						(fabsf(s->audio_level - s->audio_debug_last_level) >
						 0.01f * fmaxf(fabsf(s->audio_debug_last_level), 0.01f)) ||
						(fabsf(s->audio_peak - s->audio_debug_last_peak) >
						 0.01f * fmaxf(fabsf(s->audio_debug_last_peak), 0.01f));
					if (!significant_change) {
						for (int i = 0; i < 8; i++) {
							if (fabsf(s->audio_bands[i] - s->audio_debug_last_bands[i]) >
							    0.01f * fmaxf(fabsf(s->audio_debug_last_bands[i]), 0.01f)) {
								significant_change = true;
								break;
							}
						}
					}
					if (significant_change || s->audio_debug_last_level < 0.0f) {
						blog(LOG_INFO,
						     "[hypershader] Audio debug: level=%.6f peak=%.6f bands=[%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f]",
						     s->audio_level, s->audio_peak, s->audio_bands[0],
						     s->audio_bands[1], s->audio_bands[2], s->audio_bands[3],
						     s->audio_bands[4], s->audio_bands[5], s->audio_bands[6],
						     s->audio_bands[7]);
						s->audio_debug_last_level = s->audio_level;
						s->audio_debug_last_peak = s->audio_peak;
						memcpy(s->audio_debug_last_bands, s->audio_bands,
						       sizeof(s->audio_debug_last_bands));
					}
				}
			}
		} else {
			/* Fallback: allocate per-iteration if pre-allocated buffer unavailable */
			localbuf = (float *)bzalloc(sizeof(float) * (size_t)s->audio_window_size);
			if (localbuf && s->audio_lockfree_ring.data) {
				n = (int)audio_lockfree_ring_peek(s, localbuf, s->audio_window_size);
				if (n > 0)
					compute_audio_features(s, localbuf, n);
			}
			bfree(localbuf);
		}
	}
	audio_thread_set_stopped(&s->audio_thread_mgr);
	return NULL;
}
