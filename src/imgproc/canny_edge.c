/**
 * Canny Edge Detector - Real Implementation
 * 
 * Full implementation of the classic Canny algorithm:
 * 1. Gaussian blur for noise reduction
 * 2. Sobel gradient computation (Gx, Gy)
 * 3. Non-maximum suppression to thin edges
 * 4. Double thresholding (strong/weak classification)
 * 5. Hysteresis edge tracking (connect weak to strong)
 */

#include "canny_edge.h"
#include <obs-module.h> /* Required for bmalloc/bfree */
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Internal state for the Canny detector */
struct canny_detector {
	/* Parameters */
	float low_threshold;
	float high_threshold;
	float sigma;

	/* Gaussian kernel */
	float *gauss_kernel;
	int kernel_size;

	/* Working buffers (allocated on first use or resize) */
	int buf_width;
	int buf_height;
	float *grayscale;    /* Grayscale input (float) */
	float *blurred;      /* After Gaussian blur */
	float *gradient_mag; /* Gradient magnitude */
	float *gradient_dir; /* Gradient direction */
	float *nms;          /* After non-maximum suppression */
	uint8_t *edge_map;   /* Final edge classification */
};

/* ============================================================
 * Helper Functions
 * ============================================================ */

static inline float clampf(float x, float lo, float hi)
{
	return x < lo ? lo : (x > hi ? hi : x);
}

static inline int clampi(int x, int lo, int hi)
{
	return x < lo ? lo : (x > hi ? hi : x);
}

/**
 * Generate a 1D Gaussian kernel.
 */
static void generate_gaussian_kernel(float sigma, float **kernel, int *size)
{
	/* Kernel size = 6*sigma, rounded to nearest odd number */
	int k = (int)(sigma * 3.0f);
	if (k < 1)
		k = 1;
	*size = 2 * k + 1;

	*kernel = (float *)bmalloc(*size * sizeof(float));
	if (!*kernel)
		return;

	float sum = 0.0f;
	float sigma2 = sigma * sigma;

	for (int i = 0; i < *size; i++) {
		int x = i - k;
		(*kernel)[i] = expf(-(float)(x * x) / (2.0f * sigma2));
		sum += (*kernel)[i];
	}

	/* Normalize */
	for (int i = 0; i < *size; i++) {
		(*kernel)[i] /= sum;
	}
}

/**
 * Allocate or reallocate working buffers.
 */
static bool ensure_buffers(canny_detector_t *c, int width, int height)
{
	if (c->buf_width == width && c->buf_height == height) {
		return true; /* Already allocated */
	}

	size_t pixels = (size_t)width * height;

	/* Free old buffers */
	bfree(c->grayscale);
	bfree(c->blurred);
	bfree(c->gradient_mag);
	bfree(c->gradient_dir);
	bfree(c->nms);
	bfree(c->edge_map);

	/* Allocate new buffers */
	c->grayscale = (float *)bmalloc(pixels * sizeof(float));
	c->blurred = (float *)bmalloc(pixels * sizeof(float));
	c->gradient_mag = (float *)bmalloc(pixels * sizeof(float));
	c->gradient_dir = (float *)bmalloc(pixels * sizeof(float));
	c->nms = (float *)bmalloc(pixels * sizeof(float));
	c->edge_map = (uint8_t *)bmalloc(pixels);

	if (!c->grayscale || !c->blurred || !c->gradient_mag || !c->gradient_dir || !c->nms || !c->edge_map) {
		c->buf_width = 0;
		c->buf_height = 0;
		return false;
	}

	c->buf_width = width;
	c->buf_height = height;
	return true;
}

/* ============================================================
 * Canny Algorithm Steps
 * ============================================================ */

/**
 * Step 1: Apply Gaussian blur (separable convolution)
 */
static void gaussian_blur(const float *input, float *output, int width, int height, const float *kernel, int ksize)
{
	int k = ksize / 2;

	/* Buffer for horizontal pass */
	float *temp = (float *)bmalloc(width * height * sizeof(float));
	if (!temp) {
		memcpy(output, input, width * height * sizeof(float));
		return;
	}

	/* Horizontal pass */
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			float sum = 0.0f;
			for (int i = -k; i <= k; i++) {
				int xi = clampi(x + i, 0, width - 1);
				sum += input[y * width + xi] * kernel[i + k];
			}
			temp[y * width + x] = sum;
		}
	}

	/* Vertical pass */
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			float sum = 0.0f;
			for (int i = -k; i <= k; i++) {
				int yi = clampi(y + i, 0, height - 1);
				sum += temp[yi * width + x] * kernel[i + k];
			}
			output[y * width + x] = sum;
		}
	}

	bfree(temp);
}

/**
 * Step 2: Compute gradients using Sobel operators
 */
static void compute_gradients(const float *input, float *magnitude, float *direction, int width, int height)
{
	/* Sobel kernels:
     * Gx = [-1 0 1]    Gy = [-1 -2 -1]
     *      [-2 0 2]         [ 0  0  0]
     *      [-1 0 1]         [ 1  2  1]
     */

	for (int y = 1; y < height - 1; y++) {
		for (int x = 1; x < width - 1; x++) {
			/* Sample 3x3 neighborhood */
			float p00 = input[(y - 1) * width + (x - 1)];
			float p01 = input[(y - 1) * width + x];
			float p02 = input[(y - 1) * width + (x + 1)];
			float p10 = input[y * width + (x - 1)];
			/* float p11 = input[y * width + x]; */ /* Center - not used */
			float p12 = input[y * width + (x + 1)];
			float p20 = input[(y + 1) * width + (x - 1)];
			float p21 = input[(y + 1) * width + x];
			float p22 = input[(y + 1) * width + (x + 1)];

			/* Sobel gradients */
			float gx = -p00 + p02 - 2.0f * p10 + 2.0f * p12 - p20 + p22;
			float gy = -p00 - 2.0f * p01 - p02 + p20 + 2.0f * p21 + p22;

			int idx = y * width + x;
			magnitude[idx] = sqrtf(gx * gx + gy * gy);
			direction[idx] = atan2f(gy, gx);
		}
	}

	/* Handle borders (set to zero) */
	for (int x = 0; x < width; x++) {
		magnitude[x] = 0;
		magnitude[(height - 1) * width + x] = 0;
		direction[x] = 0;
		direction[(height - 1) * width + x] = 0;
	}
	for (int y = 0; y < height; y++) {
		magnitude[y * width] = 0;
		magnitude[y * width + width - 1] = 0;
		direction[y * width] = 0;
		direction[y * width + width - 1] = 0;
	}
}

/**
 * Step 3: Non-maximum suppression
 * Keep only local maxima along gradient direction.
 */
static void non_maximum_suppression(const float *magnitude, const float *direction, float *output, int width,
				    int height)
{
	memset(output, 0, width * height * sizeof(float));

	for (int y = 1; y < height - 1; y++) {
		for (int x = 1; x < width - 1; x++) {
			int idx = y * width + x;
			float mag = magnitude[idx];
			float dir = direction[idx];

			/* Quantize direction to 0, 45, 90, or 135 degrees */
			/* Direction is in [-PI, PI], normalize to [0, PI] for comparison */
			if (dir < 0)
				dir += (float)M_PI;

			float m1, m2;

			if (dir < M_PI / 8.0f || dir >= 7.0f * M_PI / 8.0f) {
				/* 0 degrees: compare with left and right */
				m1 = magnitude[y * width + (x - 1)];
				m2 = magnitude[y * width + (x + 1)];
			} else if (dir < 3.0f * M_PI / 8.0f) {
				/* 45 degrees: compare with top-right and bottom-left */
				m1 = magnitude[(y - 1) * width + (x + 1)];
				m2 = magnitude[(y + 1) * width + (x - 1)];
			} else if (dir < 5.0f * M_PI / 8.0f) {
				/* 90 degrees: compare with top and bottom */
				m1 = magnitude[(y - 1) * width + x];
				m2 = magnitude[(y + 1) * width + x];
			} else {
				/* 135 degrees: compare with top-left and bottom-right */
				m1 = magnitude[(y - 1) * width + (x - 1)];
				m2 = magnitude[(y + 1) * width + (x + 1)];
			}

			/* Keep if local maximum */
			if (mag >= m1 && mag >= m2) {
				output[idx] = mag;
			}
		}
	}
}

/**
 * Step 4 & 5: Double thresholding and hysteresis edge tracking
 */
static void hysteresis_thresholding(const float *nms, uint8_t *output, int width, int height, float low_thresh,
				    float high_thresh)
{
	/* Classification constants */
	const uint8_t EDGE_NONE = 0;
	const uint8_t EDGE_WEAK = 128;
	const uint8_t EDGE_STRONG = 255;

	/* Step 4: Double threshold classification */
	for (int i = 0; i < width * height; i++) {
		if (nms[i] >= high_thresh) {
			output[i] = EDGE_STRONG;
		} else if (nms[i] >= low_thresh) {
			output[i] = EDGE_WEAK;
		} else {
			output[i] = EDGE_NONE;
		}
	}

	/* Step 5: Hysteresis - promote weak edges connected to strong edges */
	/* Use iterative approach (simpler than recursive, no stack overflow) */
	bool changed = true;
	int iterations = 0;
	const int max_iterations = 100; /* Prevent infinite loops */

	while (changed && iterations < max_iterations) {
		changed = false;
		iterations++;

		for (int y = 1; y < height - 1; y++) {
			for (int x = 1; x < width - 1; x++) {
				int idx = y * width + x;

				if (output[idx] != EDGE_WEAK)
					continue;

				/* Check 8-connected neighbors for strong edge */
				bool has_strong = output[(y - 1) * width + (x - 1)] == EDGE_STRONG ||
						  output[(y - 1) * width + x] == EDGE_STRONG ||
						  output[(y - 1) * width + (x + 1)] == EDGE_STRONG ||
						  output[y * width + (x - 1)] == EDGE_STRONG ||
						  output[y * width + (x + 1)] == EDGE_STRONG ||
						  output[(y + 1) * width + (x - 1)] == EDGE_STRONG ||
						  output[(y + 1) * width + x] == EDGE_STRONG ||
						  output[(y + 1) * width + (x + 1)] == EDGE_STRONG;

				if (has_strong) {
					output[idx] = EDGE_STRONG;
					changed = true;
				}
			}
		}
	}

	/* Final pass: convert weak edges without strong connections to no-edge */
	for (int i = 0; i < width * height; i++) {
		if (output[i] == EDGE_WEAK) {
			output[i] = EDGE_NONE;
		}
	}
}

/* ============================================================
 * Public API
 * ============================================================ */

canny_detector_t *canny_create(void)
{
	canny_detector_t *c = (canny_detector_t *)bzalloc(sizeof(canny_detector_t));
	if (!c)
		return NULL;

	/* Default parameters */
	c->low_threshold = 50.0f;
	c->high_threshold = 150.0f;
	c->sigma = 1.4f;

	/* Generate initial Gaussian kernel */
	generate_gaussian_kernel(c->sigma, &c->gauss_kernel, &c->kernel_size);

	return c;
}

void canny_destroy(canny_detector_t *c)
{
	if (!c)
		return;

	bfree(c->gauss_kernel);
	bfree(c->grayscale);
	bfree(c->blurred);
	bfree(c->gradient_mag);
	bfree(c->gradient_dir);
	bfree(c->nms);
	bfree(c->edge_map);
	bfree(c);
}

void canny_set_thresholds(canny_detector_t *c, float low_threshold, float high_threshold)
{
	if (!c)
		return;
	c->low_threshold = low_threshold;
	c->high_threshold = high_threshold;
}

void canny_set_sigma(canny_detector_t *c, float sigma)
{
	if (!c)
		return;
	if (sigma != c->sigma) {
		c->sigma = sigma;
		bfree(c->gauss_kernel);
		generate_gaussian_kernel(c->sigma, &c->gauss_kernel, &c->kernel_size);
	}
}

bool canny_process(canny_detector_t *c, const uint8_t *input, int width, int height, uint8_t *output)
{
	if (!c || !input || !output || width <= 2 || height <= 2) {
		return false;
	}

	/* Ensure buffers are allocated */
	if (!ensure_buffers(c, width, height)) {
		return false;
	}

	/* Convert uint8 to float [0, 255] */
	size_t pixels = (size_t)width * height;
	for (size_t i = 0; i < pixels; i++) {
		c->grayscale[i] = (float)input[i];
	}

	/* Step 1: Gaussian blur */
	gaussian_blur(c->grayscale, c->blurred, width, height, c->gauss_kernel, c->kernel_size);

	/* Step 2: Compute gradients */
	compute_gradients(c->blurred, c->gradient_mag, c->gradient_dir, width, height);

	/* Step 3: Non-maximum suppression */
	non_maximum_suppression(c->gradient_mag, c->gradient_dir, c->nms, width, height);

	/* Steps 4 & 5: Double threshold + Hysteresis */
	hysteresis_thresholding(c->nms, c->edge_map, width, height, c->low_threshold, c->high_threshold);

	/* Copy result to output */
	memcpy(output, c->edge_map, pixels);

	return true;
}

bool canny_process_rgba(canny_detector_t *c, const uint8_t *input, int width, int height, int stride, uint8_t *output)
{
	if (!c || !input || !output || width <= 2 || height <= 2) {
		return false;
	}

	/* Ensure buffers are allocated */
	if (!ensure_buffers(c, width, height)) {
		return false;
	}

	/* Convert RGBA to grayscale using luminance formula:
     * Y = 0.299*R + 0.587*G + 0.114*B */
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			const uint8_t *pixel = &input[y * stride + x * 4];
			float r = pixel[0];
			float g = pixel[1];
			float b = pixel[2];
			c->grayscale[y * width + x] = 0.299f * r + 0.587f * g + 0.114f * b;
		}
	}

	/* Step 1: Gaussian blur */
	gaussian_blur(c->grayscale, c->blurred, width, height, c->gauss_kernel, c->kernel_size);

	/* Step 2: Compute gradients */
	compute_gradients(c->blurred, c->gradient_mag, c->gradient_dir, width, height);

	/* Step 3: Non-maximum suppression */
	non_maximum_suppression(c->gradient_mag, c->gradient_dir, c->nms, width, height);

	/* Steps 4 & 5: Double threshold + Hysteresis */
	hysteresis_thresholding(c->nms, c->edge_map, width, height, c->low_threshold, c->high_threshold);

	/* Copy result to output */
	memcpy(output, c->edge_map, (size_t)width * height);

	return true;
}

const float *canny_get_gradient_magnitude(canny_detector_t *c)
{
	return c ? c->gradient_mag : NULL;
}

const float *canny_get_gradient_direction(canny_detector_t *c)
{
	return c ? c->gradient_dir : NULL;
}
