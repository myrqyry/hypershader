/**
 * @file fft_config.h
 * @brief FFT configuration with transactional updates
 * 
 * Provides safe FFT reconfiguration with rollback on failure.
 * This ensures the audio system remains functional even if
 * parameter changes fail to apply.
 */

#ifndef FFT_CONFIG_H
#define FFT_CONFIG_H

#include <obs-module.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../safe_alloc.h"

/* Forward declarations */
typedef struct kiss_fftr_state *kiss_fftr_cfg;
typedef float kiss_fft_scalar;
typedef struct {
	float r;
	float i;
} kiss_fft_cpx;

/* Window types */
#define FFT_WINDOW_NONE 0
#define FFT_WINDOW_HANN 1
#define FFT_WINDOW_HAMMING 2

/**
 * FFT configuration structure
 * Contains all resources needed for FFT processing
 */
typedef struct fft_config {
	int fft_size;    /* Requested FFT size */
	int fft_n;       /* Actual power-of-2 size */
	int window_type; /* Window function type */
	int window_size; /* Window size in samples */

	kiss_fftr_cfg cfg;      /* Kiss FFT configuration */
	kiss_fft_scalar *input; /* Input buffer (size fft_n) */
	kiss_fft_cpx *output;   /* Output bins (size fft_n/2+1) */
	float *window;          /* Window coefficients */
	float *history;         /* Smoothing history buffer */

	bool valid; /* True if all resources allocated */
} fft_config_t;

/**
 * @brief Compute next power of 2 greater than or equal to n
 */
static inline int fft_next_power_of_2(int n)
{
	int p = 1;
	while (p < n)
		p <<= 1;
	return p;
}

/**
 * @brief Compute window function coefficients
 */
static inline void fft_compute_window(float *window, int size, int type)
{
	if (!window || size <= 0)
		return;

	for (int i = 0; i < size; i++) {
		float t = (float)i / (float)(size - 1);
		switch (type) {
		case FFT_WINDOW_HANN:
			window[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * t));
			break;
		case FFT_WINDOW_HAMMING:
			window[i] = 0.54f - 0.46f * cosf(2.0f * (float)M_PI * t);
			break;
		default:
			window[i] = 1.0f;
			break;
		}
	}
}

/**
 * @brief Destroy FFT configuration and free all resources
 */
static inline void fft_config_destroy(fft_config_t *config)
{
	if (!config)
		return;

	/* Note: kiss_fftr_free needs to be called from the actual implementation
       since we don't include the kissfft header here */
	/* if (config->cfg) kiss_fftr_free(config->cfg); */

	safe_bfree(config->input);
	safe_bfree(config->output);
	safe_bfree(config->window);
	safe_bfree(config->history);

	memset(config, 0, sizeof(*config));
}

/**
 * @brief Validate FFT configuration parameters
 */
static inline bool fft_config_validate_params(int fft_size, int window_type)
{
	if (fft_size <= 0 || fft_size > 16384) {
		blog(LOG_ERROR, "[FFT] Invalid size: %d (must be 1-16384)", fft_size);
		return false;
	}

	if (window_type < FFT_WINDOW_NONE || window_type > FFT_WINDOW_HAMMING) {
		blog(LOG_WARNING, "[FFT] Unknown window type: %d, using Hann", window_type);
	}

	return true;
}

/*
 * NOTE: Full transactional create/swap implementation will be added
 * in the audio_processor.c file to avoid circular dependencies with kissfft.
 * 
 * The pattern:
 * 1. Create new config fully (fft_config_create)
 * 2. If successful, swap with old config atomically
 * 3. Destroy old config after swap
 * 
 * If step 1 fails, old config remains untouched.
 */

#endif /* FFT_CONFIG_H */
