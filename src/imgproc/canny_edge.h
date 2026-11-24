/**
 * Canny Edge Detector - Real Implementation
 * 
 * A proper implementation of the Canny edge detection algorithm.
 * No neural network required - this is classical computer vision.
 * 
 * Algorithm Steps:
 * 1. Gaussian blur (noise reduction)
 * 2. Gradient computation (Sobel operators)
 * 3. Non-maximum suppression (thin edges)
 * 4. Double thresholding (strong/weak edges)
 * 5. Hysteresis edge tracking (connect weak to strong)
 * 
 * Usage:
 *   canny_detector_t* canny = canny_create();
 *   canny_set_thresholds(canny, 50, 150);
 *   canny_process(canny, input_gray, width, height, output);
 *   canny_destroy(canny);
 */

#ifndef CANNY_EDGE_H
#define CANNY_EDGE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque pointer to Canny detector state */
typedef struct canny_detector canny_detector_t;

/**
 * Create a Canny edge detector instance.
 * Default thresholds: low=50, high=150
 * Default Gaussian sigma: 1.4
 */
canny_detector_t* canny_create(void);

/**
 * Destroy the detector and free resources.
 */
void canny_destroy(canny_detector_t* canny);

/**
 * Set edge detection thresholds.
 * @param low_threshold  Edges below this are discarded (default: 50)
 * @param high_threshold Edges above this are strong edges (default: 150)
 */
void canny_set_thresholds(canny_detector_t* canny, float low_threshold, float high_threshold);

/**
 * Set Gaussian blur sigma for noise reduction.
 * @param sigma Standard deviation for Gaussian kernel (default: 1.4)
 */
void canny_set_sigma(canny_detector_t* canny, float sigma);

/**
 * Process a grayscale image and detect edges.
 * 
 * @param canny   The detector instance
 * @param input   Input grayscale image (8-bit, single channel)
 * @param width   Image width
 * @param height  Image height
 * @param output  Output edge map (8-bit, 0=no edge, 255=edge)
 * @return true on success
 */
bool canny_process(canny_detector_t* canny, 
                   const uint8_t* input, int width, int height,
                   uint8_t* output);

/**
 * Process an RGBA image and detect edges.
 * Converts to grayscale internally using luminance formula.
 * 
 * @param canny   The detector instance
 * @param input   Input RGBA image (8-bit, 4 channels)
 * @param width   Image width
 * @param height  Image height
 * @param stride  Input row stride in bytes
 * @param output  Output edge map (8-bit, 0=no edge, 255=edge)
 * @return true on success
 */
bool canny_process_rgba(canny_detector_t* canny,
                        const uint8_t* input, int width, int height, int stride,
                        uint8_t* output);

/**
 * Get the gradient magnitude image from last processing.
 * Useful for visualization or further processing.
 * Returns NULL if no image has been processed yet.
 */
const float* canny_get_gradient_magnitude(canny_detector_t* canny);

/**
 * Get the gradient direction image from last processing.
 * Values are in radians [-PI, PI].
 */
const float* canny_get_gradient_direction(canny_detector_t* canny);

#ifdef __cplusplus
}
#endif

#endif /* CANNY_EDGE_H */
