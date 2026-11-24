#ifndef KISS_FFTR_H
#define KISS_FFTR_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef float kiss_fft_scalar;

typedef struct {
    float r;
    float i;
} kiss_fft_cpx;

/* Opaque config type */
typedef struct kiss_fftr_state *kiss_fftr_cfg;

#define kiss_fftr_alloc hyper_kiss_fftr_alloc
#define kiss_fftr       hyper_kiss_fftr
#define kiss_fftr_free  hyper_kiss_fftr_free

/* Allocate and free real-fft config */
kiss_fftr_cfg kiss_fftr_alloc(int nfft, int inverse_fft, void *mem, size_t *lenmem);
void kiss_fftr(kiss_fftr_cfg cfg, const kiss_fft_scalar *timedata, kiss_fft_cpx *freqdata);
void kiss_fftr_free(kiss_fftr_cfg cfg);

#ifdef __cplusplus
}
#endif

#endif /* KISS_FFTR_H */
