#include "kiss_fftr.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <util/bmem.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Minimal in-file FFT implementation to provide a lightweight kiss_fftr-like API.
 * This is not full kissfft but sufficient for our band computations and allows
 * pre-allocation and reuse of buffers.
 * 
 * NOTE: Updated to use OBS bmalloc/bfree to match plugin allocator and prevent conflicts.
 */

struct kiss_fftr_state {
	int nfft;
	double *re;
	double *im;
};

static void fft_inplace_double(double *re, double *im, int n)
{
	int j = 0;
	for (int i = 1; i < n; i++) {
		int bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j) {
			double tr = re[i];
			re[i] = re[j];
			re[j] = tr;
			double ti = im[i];
			im[i] = im[j];
			im[j] = ti;
		}
	}

	for (int len = 2; len <= n; len <<= 1) {
		double ang = -2.0 * M_PI / len;
		double wlen_re = cos(ang);
		double wlen_im = sin(ang);
		for (int i = 0; i < n; i += len) {
			double wr = 1.0, wi = 0.0;
			for (int k = 0; k < (len >> 1); k++) {
				int u = i + k;
				int v = i + k + (len >> 1);
				double ur = re[u];
				double ui = im[u];
				double vr = re[v] * wr - im[v] * wi;
				double vi = re[v] * wi + im[v] * wr;
				re[u] = ur + vr;
				im[u] = ui + vi;
				re[v] = ur - vr;
				im[v] = ui - vi;
				double tw = wr * wlen_re - wi * wlen_im;
				wi = wr * wlen_im + wi * wlen_re;
				wr = tw;
			}
		}
	}
}

kiss_fftr_cfg kiss_fftr_alloc(int nfft, int inverse_fft, void *mem, size_t *lenmem)
{
	(void)inverse_fft;
	(void)mem;
	(void)lenmem;
	if (nfft <= 0)
		return NULL;
	struct kiss_fftr_state *st = (struct kiss_fftr_state *)bmalloc(sizeof(struct kiss_fftr_state));
	if (!st)
		return NULL;
	st->nfft = nfft;
	st->re = (double *)bmalloc(sizeof(double) * nfft);
	st->im = (double *)bmalloc(sizeof(double) * nfft);
	if (!st->re || !st->im) {
		if (st->re)
			bfree(st->re);
		if (st->im)
			bfree(st->im);
		bfree(st);
		return NULL;
	}
	/* Debug: Log allocation to track address lifecycle in logs */
	// blog(LOG_DEBUG, "[DEBUG_FFT] kiss_fftr_alloc: st=%p re=%p im=%p", st, st->re, st->im);
	return (kiss_fftr_cfg)st;
}

void kiss_fftr(kiss_fftr_cfg cfg, const kiss_fft_scalar *timedata, kiss_fft_cpx *freqdata)
{
	if (!cfg || !timedata || !freqdata)
		return;
	struct kiss_fftr_state *st = (struct kiss_fftr_state *)cfg;
	int N = st->nfft;
	for (int i = 0; i < N; i++) {
		st->re[i] = (double)timedata[i];
		st->im[i] = 0.0;
	}

	fft_inplace_double(st->re, st->im, N);

	int nbins = N / 2 + 1;
	for (int k = 0; k < nbins; k++) {
		freqdata[k].r = (float)st->re[k];
		freqdata[k].i = (float)st->im[k];
	}
}

void kiss_fftr_free(kiss_fftr_cfg cfg)
{
	if (!cfg)
		return;
	struct kiss_fftr_state *st = (struct kiss_fftr_state *)cfg;
	if (st->re) {
		// blog(LOG_DEBUG, "[DEBUG_FFT] kiss_fftr_free: freeing re %p", st->re);
		bfree(st->re);
	}
	if (st->im) {
		// blog(LOG_DEBUG, "[DEBUG_FFT] kiss_fftr_free: freeing im %p", st->im);
		bfree(st->im);
	}
	// blog(LOG_DEBUG, "[DEBUG_FFT] kiss_fftr_free: freeing st %p", st);
	bfree(st);
}
