#include "mp_wrapper.h"
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <cmath>
#include <cstring>
#include <mutex>
#include <memory>
#include <algorithm>
#include <string>
#include <thread>
#include <atomic>
#include <obs-module.h>

/**
 * MediaPipe Wrapper Implementation - Full ONNX Integration
 * 
 * INTEGRATION STATUS: REAL BACKEND REQUIRED
 * =========================================
 * 
 * This implementation requires a real inference backend.
 * 
 * ONNX PIPELINE OVERVIEW:
 * -----------------------
 * 1. Face Detector Model (BlazeFace-based):
 *    - File: face_detector.onnx
 *    - Input:  [1, 3, 256, 256] RGB normalized to [0,1]
 *    - Outputs: box_coords_1, box_coords_2, box_scores_1, box_scores_2
 *
 * 2. Face Landmark Model (FaceMesh-based):
 *    - File: face_landmark_detector.onnx
 *    - Input:  [1, 3, 192, 192] cropped face RGB normalized to [0,1]
 *    - Output: landmarks
 *
 * BUILD:
 *   cmake -DUSE_ONNX_RUNTIME=ON ..
 *   cmake --build .
 */

// Configuration
// Configuration
#ifdef USE_ONNX_MEDIAPIPE
#include <onnxruntime_cxx_api.h>
#define ONNX_AVAILABLE_DEF true
#else
#define ONNX_AVAILABLE_DEF false
#endif

#ifdef USE_LITERT_MEDIAPIPE
#include <litert/cc/litert_compiled_model.h>
#define LITERT_AVAILABLE_DEF true
#else
#define LITERT_AVAILABLE_DEF false
#endif

#if defined(USE_ONNX_MEDIAPIPE)
#define MP_BACKEND_NAME "ONNX"
#elif defined(USE_LITERT_MEDIAPIPE)
#define MP_BACKEND_NAME "LiteRT"
#else
#define MP_BACKEND_NAME "Unavailable"
#endif

// Global availability flags
static std::atomic<bool> g_onnx_available{false};
static std::atomic<bool> g_litert_available{false};

// Face mesh constants
constexpr int FACE_MESH_LANDMARK_COUNT = 468;

// Model input sizes for BlazeFace and FaceMesh
constexpr int FACE_DETECTOR_INPUT_SIZE = 256;
constexpr int FACE_LANDMARK_INPUT_SIZE = 192;

#ifdef USE_LITERT_MEDIAPIPE
constexpr int CANNY_INPUT_SIZE = 512;
#endif

#if defined(USE_ONNX_MEDIAPIPE) || defined(USE_LITERT_MEDIAPIPE)
// Face detection settings are only needed when an inference backend is built.
constexpr float FACE_DETECTION_THRESHOLD = 0.5f;
constexpr int NUM_ANCHORS = 896;
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Smooth step function for mask edge falloff
static inline float smoothstep(float edge0, float edge1, float x)
{
	float t = fmaxf(0.0f, fminf(1.0f, (x - edge0) / (edge1 - edge0)));
	return t * t * (3.0f - 2.0f * t);
}

// Sigmoid activation
static inline float sigmoid(float x)
{
	return 1.0f / (1.0f + expf(-x));
}

// Clamp value to range
static inline float clamp01(float x)
{
	return fmaxf(0.0f, fminf(1.0f, x));
}

#ifdef USE_ONNX_MEDIAPIPE
static int ort_media_pipe_intra_threads(bool use_gpu)
{
	if (use_gpu)
		return 1;

	unsigned int hw_threads = std::thread::hardware_concurrency();
	if (hw_threads == 0)
		return 2;
	if (hw_threads <= 4)
		return 1;
	if (hw_threads <= 8)
		return 2;
	return 4;
}

static void configure_onnx_session_options(Ort::SessionOptions &session_options, bool use_gpu)
{
	session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
	session_options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
	session_options.SetIntraOpNumThreads(ort_media_pipe_intra_threads(use_gpu));
	session_options.SetInterOpNumThreads(1);
	session_options.EnableCpuMemArena();
	session_options.EnableMemPattern();
	session_options.AddConfigEntry("session.disable_prepacking", "0");
	if (use_gpu)
		session_options.AddConfigEntry("session.intra_op.allow_spinning", "0");
}

#ifdef USE_CUDA
static void configure_cuda_provider_options(OrtCUDAProviderOptions &cuda_opts)
{
	memset(&cuda_opts, 0, sizeof(cuda_opts));
	cuda_opts.device_id = 0;
	cuda_opts.do_copy_in_default_stream = 1;
	cuda_opts.gpu_mem_limit = SIZE_MAX;
	/* Keep arena behavior at ORT defaults; the session-level settings already
	 * minimize startup latency and prevent excess thread contention. */
}
#endif
#endif

/**
 * Face detection bounding box
 */
struct FaceBox {
	float x_center; // Normalized [0, 1]
	float y_center; // Normalized [0, 1]
	float width;    // Normalized [0, 1]
	float height;   // Normalized [0, 1]
	float score;    // Confidence

	// Get pixel coordinates for a given image size
	void to_pixel_coords(int img_width, int img_height, int *x1, int *y1, int *x2, int *y2) const
	{
		float hw = width / 2.0f;
		float hh = height / 2.0f;
		*x1 = (int)((x_center - hw) * img_width);
		*y1 = (int)((y_center - hh) * img_height);
		*x2 = (int)((x_center + hw) * img_width);
		*y2 = (int)((y_center + hh) * img_height);
	}
};

// Box Intersection over Union
static float box_iou(const FaceBox &a, const FaceBox &b)
{
	float x_left = fmaxf(a.x_center - a.width / 2, b.x_center - b.width / 2);
	float y_top = fmaxf(a.y_center - a.height / 2, b.y_center - b.height / 2);
	float x_right = fminf(a.x_center + a.width / 2, b.x_center + b.width / 2);
	float y_bottom = fminf(a.y_center + a.height / 2, b.y_center + b.height / 2);

	if (x_right < x_left || y_bottom < y_top)
		return 0.0f;

	float intersection = (x_right - x_left) * (y_bottom - y_top);
	float area_a = a.width * a.height;
	float area_b = b.width * b.height;

	return intersection / (area_a + area_b - intersection);
}

/**
 * Anchor box for face detection decoding
 */
struct Anchor {
	float x_center;
	float y_center;
};

#if defined(USE_ONNX_MEDIAPIPE)
/**
 * Generate anchors for BlazeFace Short-Range model
 * Based on MediaPipe's anchor generation algorithm
 */
static std::vector<Anchor> generate_anchors()
{
	std::vector<Anchor> anchors;
	anchors.reserve(NUM_ANCHORS);

	// Anchor specification for Qualcomm MediaPipe face detector ONNX export:
	// 256x256 input, outputs 512 anchors at stride 16 and 384 anchors at stride 32.
	const int strides[] = {16, 32};
	const int stride_count = 2;
	const int anchors_per_stride[] = {2, 6}; // 2 anchors at stride 8, 6 at stride 16

	for (int s = 0; s < stride_count; s++) {
		int stride = strides[s];
		int grid_size = FACE_DETECTOR_INPUT_SIZE / stride;
		int num_anchors_at_stride = anchors_per_stride[s];

		for (int y = 0; y < grid_size; y++) {
			for (int x = 0; x < grid_size; x++) {
				for (int a = 0; a < num_anchors_at_stride; a++) {
					Anchor anchor;
					anchor.x_center = (x + 0.5f) * stride / (float)FACE_DETECTOR_INPUT_SIZE;
					anchor.y_center = (y + 0.5f) * stride / (float)FACE_DETECTOR_INPUT_SIZE;
					anchors.push_back(anchor);
				}
			}
		}
	}

	return anchors;
}

/**
 * Decode face detections from BlazeFace output
 */
static std::vector<FaceBox>
decode_face_detections(const float *raw_boxes,  // [num_anchors, 16] - box regressions (4 coords + 12 keypoints)
		       const float *raw_scores, // [num_anchors, 1] - classification scores
		       const std::vector<Anchor> &anchors, int num_anchors, float threshold)
{
	std::vector<FaceBox> detections;

	for (int i = 0; i < num_anchors; i++) {
		float score = sigmoid(raw_scores[i]);
		if (score < threshold)
			continue;

		// Decode box (first 4 values: x_center, y_center, width, height)
		const float *box = &raw_boxes[i * 16];

		FaceBox face;
		face.x_center = box[0] / FACE_DETECTOR_INPUT_SIZE + anchors[i].x_center;
		face.y_center = box[1] / FACE_DETECTOR_INPUT_SIZE + anchors[i].y_center;
		face.width = box[2] / FACE_DETECTOR_INPUT_SIZE;
		face.height = box[3] / FACE_DETECTOR_INPUT_SIZE;
		face.score = score;

		detections.push_back(face);
	}

	// Non-Maximum Suppression
	std::sort(detections.begin(), detections.end(),
		  [](const FaceBox &a, const FaceBox &b) { return a.score > b.score; });

	std::vector<FaceBox> final_faces;
	for (const auto &det : detections) {
		bool keep = true;
		for (const auto &kept : final_faces) {
			if (box_iou(det, kept) > 0.3f) {
				keep = false;
				break;
			}
		}
		if (keep)
			final_faces.push_back(det);
	}

	return final_faces;
}
#endif

struct mp_runner {
	mp_graph_type_t current_graph;
	std::mutex data_lock;

	// Mask Data
	std::vector<uint8_t> mask_buffer;
	int mask_width;
	int mask_height;

	// Landmark Data
	std::vector<mp_landmark_t> landmarks;

	// Hand Tracking Data
	std::vector<mp_hand_result_t> hand_results;

	// Depth Estimation Data
	std::vector<float> depth_buffer;
	int depth_width;
	int depth_height;
	float depth_min;
	float depth_max;

	// Last detected face box (for landmark cropping)
	FaceBox last_face_box;
	std::vector<FaceBox> all_faces; // Store all detected faces
	bool has_face;

	bool use_gpu;

	// Processing buffers
	std::vector<float> detector_input;
	std::vector<float> landmark_input;

	// Pre-computed anchors
	std::vector<Anchor> anchors;

#ifdef USE_LITERT_MEDIAPIPE
	// LiteRT 2.x Resources
	std::unique_ptr<litert::CompiledModel> compiled_model_det;
	std::unique_ptr<litert::CompiledModel> compiled_model_land;
	std::unique_ptr<litert::CompiledModel> compiled_model_canny;
	std::unique_ptr<litert::CompiledModel> compiled_model_hand_det;
	std::unique_ptr<litert::CompiledModel> compiled_model_hand_land;

	std::vector<litert::TensorBuffer> det_inputs;
	std::vector<litert::TensorBuffer> det_outputs;
	std::vector<litert::TensorBuffer> land_inputs;
	std::vector<litert::TensorBuffer> land_outputs;
	std::vector<litert::TensorBuffer> canny_inputs;
	std::vector<litert::TensorBuffer> canny_outputs;
	std::vector<litert::TensorBuffer> hand_det_inputs;
	std::vector<litert::TensorBuffer> hand_det_outputs;
	std::vector<litert::TensorBuffer> hand_land_inputs;
	std::vector<litert::TensorBuffer> hand_land_outputs;

	litert::Environment env;
#endif

#ifdef USE_ONNX_MEDIAPIPE
	// ONNX Runtime Sessions
	std::unique_ptr<Ort::Env> ort_env;
	std::unique_ptr<Ort::Session> face_detector_session;
	std::unique_ptr<Ort::Session> face_landmark_session;
	std::unique_ptr<Ort::Session> hand_detector_session;
	std::unique_ptr<Ort::Session> hand_landmark_session;
	std::unique_ptr<Ort::Session> depth_estimation_session;
	std::unique_ptr<Ort::AllocatorWithDefaultOptions> allocator;
	Ort::MemoryInfo memory_info;

	// Model I/O names
	std::vector<const char *> detector_input_names;
	std::vector<const char *> detector_output_names;
	std::vector<const char *> landmark_input_names;
	std::vector<const char *> landmark_output_names;
	std::vector<const char *> hand_detector_input_names;
	std::vector<const char *> hand_detector_output_names;
	std::vector<const char *> hand_landmark_input_names;
	std::vector<const char *> hand_landmark_output_names;
	std::vector<const char *> depth_input_names;
	std::vector<const char *> depth_output_names;

	// OBS Resources (Issue 2)
	struct gs_texture *mask_tex = nullptr;
	struct gs_texture *landmarks_tex = nullptr;
	struct gs_texture *ref_landmarks_tex = nullptr;
	struct gs_stagesurf *staging_tex = nullptr;
	struct gs_texrender *scaler = nullptr;
	uint8_t *cpu_buffer = nullptr;
	bool enabled = false;

	mp_runner()
#ifdef USE_ONNX_MEDIAPIPE
		: memory_info(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault))
#endif
	{
#if defined(USE_ONNX_MEDIAPIPE)
		anchors = generate_anchors();
#endif
	}
#endif
};

/**
 * Resize and normalize helper for detector input
 * Resizes source image to target size and normalizes to [0,1] in NHWC format
 */
static void resize_and_normalize(const uint8_t *src, int src_width, int src_height, int src_stride, float *dst,
				 int dst_width, int dst_height)
{
	float x_ratio = (float)src_width / dst_width;
	float y_ratio = (float)src_height / dst_height;

	for (int y = 0; y < dst_height; y++) {
		for (int x = 0; x < dst_width; x++) {
			// Source coordinates
			float src_x = x * x_ratio;
			float src_y = y * y_ratio;
			int x0 = (int)src_x;
			int y0 = (int)src_y;
			int x1 = std::min(x0 + 1, src_width - 1);
			int y1 = std::min(y0 + 1, src_height - 1);

			float xf = src_x - x0;
			float yf = src_y - y0;

			// Bilinear interpolation for each channel
			for (int c = 0; c < 3; c++) {
				float v00 = src[y0 * src_stride + x0 * 4 + c];
				float v01 = src[y1 * src_stride + x0 * 4 + c];
				float v10 = src[y0 * src_stride + x1 * 4 + c];
				float v11 = src[y1 * src_stride + x1 * 4 + c];

				float v0 = v00 + xf * (v10 - v00);
				float v1 = v01 + xf * (v11 - v01);
				float v = v0 + yf * (v1 - v0);

				// Normalize to [0, 1] and store in NHWC format (model expects NHWC)
				dst[(y * dst_width + x) * 3 + c] = v / 255.0f;
			}
		}
	}
}

static void resize_and_normalize_chw(const uint8_t *src, int src_width, int src_height, int src_stride, float *dst,
				     int dst_width, int dst_height)
{
	const int plane = dst_width * dst_height;
	for (int y = 0; y < dst_height; y++) {
		for (int x = 0; x < dst_width; x++) {
			int sx = x * src_width / dst_width;
			int sy = y * src_height / dst_height;
			const uint8_t *pixel = src + sy * src_stride + sx * 4;
			int idx = y * dst_width + x;
			dst[idx] = pixel[0] / 255.0f;
			dst[plane + idx] = pixel[1] / 255.0f;
			dst[2 * plane + idx] = pixel[2] / 255.0f;
		}
	}
}

/**
 * Initialize runner with default values
 */
static void init_mp_runner(mp_runner *runner)
{
	runner->current_graph = MP_GRAPH_NONE;
	runner->mask_width = 256;
	runner->mask_height = 256;
	runner->mask_buffer.resize(runner->mask_width * runner->mask_height);
	runner->has_face = false;

	// Init with standard face mesh landmark count
	runner->landmarks.resize(FACE_MESH_LANDMARK_COUNT);

	// Initialize hand tracking data
	runner->hand_results.clear();

	// Initialize depth estimation data
	runner->depth_width = 256;
	runner->depth_height = 256;
	runner->depth_buffer.resize(runner->depth_width * runner->depth_height);
	runner->depth_min = 0.0f;
	runner->depth_max = 1.0f;

	// Allocate processing buffers
	runner->detector_input.resize(FACE_DETECTOR_INPUT_SIZE * FACE_DETECTOR_INPUT_SIZE * 3);
	runner->landmark_input.resize(FACE_LANDMARK_INPUT_SIZE * FACE_LANDMARK_INPUT_SIZE * 3);
}

/**
 * Crop and resize helper for landmark input
 * Crops face region from source and resizes to landmark input size
 */
static void crop_resize_and_normalize(const uint8_t *src, int src_width, int src_height, int src_stride,
				      const FaceBox &box, float *dst, int dst_size)
{
	// Expand box by 25% for better landmark extraction (standard MediaPipe practice)
	float expand = 0.25f;
	float cx = box.x_center;
	float cy = box.y_center;
	float w = box.width * (1.0f + expand);
	float h = box.height * (1.0f + expand);

	// Make square (use larger dimension)
	float size = fmaxf(w, h);

	// Calculate crop bounds in pixels
	float x_min = (cx - size / 2.0f) * src_width;
	float y_min = (cy - size / 2.0f) * src_height;
	float x_max = (cx + size / 2.0f) * src_width;
	float y_max = (cy + size / 2.0f) * src_height;

	float crop_width = x_max - x_min;
	float crop_height = y_max - y_min;

	for (int y = 0; y < dst_size; y++) {
		for (int x = 0; x < dst_size; x++) {
			// Map to source coordinates
			float src_x = x_min + (x / (float)dst_size) * crop_width;
			float src_y = y_min + (y / (float)dst_size) * crop_height;

			// Clamp to image bounds
			int sx = std::max(0, std::min((int)src_x, src_width - 1));
			int sy = std::max(0, std::min((int)src_y, src_height - 1));

			// Copy and normalize
			for (int c = 0; c < 3; c++) {
				float v = src[sy * src_stride + sx * 4 + c];
				dst[(y * dst_size + x) * 3 + c] = v / 255.0f;
			}
		}
	}
}

static void crop_resize_and_normalize_chw(const uint8_t *src, int src_width, int src_height, int src_stride,
					  const FaceBox &box, float *dst, int dst_size)
{
	float expand = 0.25f;
	float box_size = fmaxf(box.width, box.height) * (1.0f + expand);
	float x_min = (box.x_center - box_size / 2.0f) * src_width;
	float y_min = (box.y_center - box_size / 2.0f) * src_height;
	float crop_width = box_size * src_width;
	float crop_height = box_size * src_height;
	const int plane = dst_size * dst_size;

	for (int y = 0; y < dst_size; y++) {
		for (int x = 0; x < dst_size; x++) {
			float src_x = x_min + (x / (float)dst_size) * crop_width;
			float src_y = y_min + (y / (float)dst_size) * crop_height;
			int sx = std::max(0, std::min((int)src_x, src_width - 1));
			int sy = std::max(0, std::min((int)src_y, src_height - 1));
			const uint8_t *pixel = src + sy * src_stride + sx * 4;
			int idx = y * dst_size + x;
			dst[idx] = pixel[0] / 255.0f;
			dst[plane + idx] = pixel[1] / 255.0f;
			dst[2 * plane + idx] = pixel[2] / 255.0f;
		}
	}
}

extern "C" {

mp_runner_t *mp_create(bool use_gpu, const char *model_path)
{
	mp_runner *runner = new mp_runner();
	runner->use_gpu = use_gpu;
	blog(LOG_INFO, "[MediaPipe] Initializing backend (gpu=%d, models=%s)", use_gpu ? 1 : 0,
	     model_path && model_path[0] ? model_path : "(none)");

	// Initialize all data structures
	init_mp_runner(runner);

	std::string base_path = model_path ? std::string(model_path) : "";
	if (!base_path.empty() && base_path.back() != '/' && base_path.back() != '\\') {
		base_path += "/";
	}

	// 1. Try LiteRT (LiteRT 2.x CompiledModel API)
#ifdef USE_LITERT_MEDIAPIPE
	try {
		bool litert_found = false;
		std::string det_path, land_path, canny_path;

		if (!base_path.empty()) {
			det_path = base_path + "face_detection_short_range.tflite";
			land_path = base_path + "face_landmark.tflite";
			canny_path = base_path + "canny_edge_plugin.tflite";

			FILE *f1 = fopen(det_path.c_str(), "rb");
			FILE *f2 = fopen(land_path.c_str(), "rb");
			if (f1 && f2) {
				fclose(f1);
				fclose(f2);
				litert_found = true;
				blog(LOG_INFO, "[MediaPipe] Found LiteRT models at: %s", base_path.c_str());
			} else {
				if (f1)
					fclose(f1);
				if (f2)
					fclose(f2);
			}
		}

		if (litert_found) {
			litert_hw_accelerator_t accelerator = use_gpu ? kLiteRtHwAcceleratorGpu
								      : kLiteRtHwAcceleratorCpu;

			auto det_res = litert::CompiledModel::Create(runner->env, det_path, accelerator);
			auto land_res = litert::CompiledModel::Create(runner->env, land_path, accelerator);

			if (det_res.ok() && land_res.ok()) {
				runner->compiled_model_det = std::move(*det_res);
				runner->compiled_model_land = std::move(*land_res);

				// Preallocate buffers for signature 0
				auto det_in = runner->compiled_model_det->CreateInputBuffers(0);
				auto det_out = runner->compiled_model_det->CreateOutputBuffers(0);
				auto land_in = runner->compiled_model_land->CreateInputBuffers(0);
				auto land_out = runner->compiled_model_land->CreateOutputBuffers(0);

				if (det_in.ok() && det_out.ok() && land_in.ok() && land_out.ok()) {
					runner->det_inputs = std::move(*det_in);
					runner->det_outputs = std::move(*det_out);
					runner->land_inputs = std::move(*land_in);
					runner->land_outputs = std::move(*land_out);
					g_litert_available = true;
					blog(LOG_INFO, "[MediaPipe] LiteRT 2.x Face models initialized successfully!");
				}
			}

			// Try to load hand tracking models (LiteRT 2.x)
			std::string hand_det_path = base_path + "palm_detection.tflite";
			std::string hand_land_path = base_path + "hand_landmark.tflite";
			FILE *f_h1 = fopen(hand_det_path.c_str(), "rb");
			FILE *f_h2 = fopen(hand_land_path.c_str(), "rb");
			if (f_h1 && f_h2) {
				fclose(f_h1);
				fclose(f_h2);
				auto h_det_res = litert::CompiledModel::Create(runner->env, hand_det_path, accelerator);
				auto h_land_res =
					litert::CompiledModel::Create(runner->env, hand_land_path, accelerator);

				if (h_det_res.ok() && h_land_res.ok()) {
					runner->compiled_model_hand_det = std::move(*h_det_res);
					runner->compiled_model_hand_land = std::move(*h_land_res);

					auto hd_in = runner->compiled_model_hand_det->CreateInputBuffers(0);
					auto hd_out = runner->compiled_model_hand_det->CreateOutputBuffers(0);
					auto hl_in = runner->compiled_model_hand_land->CreateInputBuffers(0);
					auto hl_out = runner->compiled_model_hand_land->CreateOutputBuffers(0);

					if (hd_in.ok() && hd_out.ok() && hl_in.ok() && hl_out.ok()) {
						runner->hand_det_inputs = std::move(*hd_in);
						runner->hand_det_outputs = std::move(*hd_out);
						runner->hand_land_inputs = std::move(*hl_in);
						runner->hand_land_outputs = std::move(*hl_out);
						blog(LOG_INFO,
						     "[MediaPipe] LiteRT 2.x Hand models initialized successfully!");
					}
				}
			} else {
				if (f_h1)
					fclose(f_h1);
				if (f_h2)
					fclose(f_h2);
			}

			FILE *f_canny = fopen(canny_path.c_str(), "rb");
			if (f_canny) {
				fclose(f_canny);
				auto canny_res = litert::CompiledModel::Create(runner->env, canny_path, accelerator);
				if (canny_res.ok()) {
					runner->compiled_model_canny = std::move(*canny_res);
					auto c_in = runner->compiled_model_canny->CreateInputBuffers(0);
					auto c_out = runner->compiled_model_canny->CreateOutputBuffers(0);
					if (c_in.ok() && c_out.ok()) {
						runner->canny_inputs = std::move(*c_in);
						runner->canny_outputs = std::move(*c_out);
						blog(LOG_INFO, "[MediaPipe] LiteRT 2.x Canny model initialized!");
					}
				}
			}
		} else {
			blog(LOG_WARNING, "[MediaPipe] LiteRT models not found in: %s", base_path.c_str());
		}
	} catch (const std::exception &e) {
		blog(LOG_ERROR, "[MediaPipe] LiteRT 2.x init exception: %s", e.what());
	}
#endif

	// 2. Try ONNX (if LiteRT not loaded)
#ifdef USE_ONNX_MEDIAPIPE
	if (!g_litert_available) {
		try {
			runner->ort_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "MediaPipe_ONNX");
			runner->allocator = std::make_unique<Ort::AllocatorWithDefaultOptions>();

			Ort::SessionOptions session_options;
			configure_onnx_session_options(session_options, use_gpu);

			if (use_gpu) {
#ifdef USE_CUDA
				OrtCUDAProviderOptions cuda_opts;
				configure_cuda_provider_options(cuda_opts);
				session_options.AppendExecutionProvider_CUDA(cuda_opts);
#endif
			}

			bool onnx_found = false;
			std::string det_path, land_path;

			if (!base_path.empty()) {
				std::string d = base_path + "face_detector.onnx";
				std::string l = base_path + "face_landmark_detector.onnx";
				FILE *f1 = fopen(d.c_str(), "rb");
				FILE *f2 = fopen(l.c_str(), "rb");
				if (f1 && f2) {
					fclose(f1);
					fclose(f2);
					det_path = d;
					land_path = l;
					onnx_found = true;
					blog(LOG_INFO, "[MediaPipe] Found ONNX models at: %s", base_path.c_str());
				} else {
					if (f1)
						fclose(f1);
					if (f2)
						fclose(f2);
				}
			}

			if (onnx_found) {
				runner->face_detector_session = std::make_unique<Ort::Session>(
					*runner->ort_env, det_path.c_str(), session_options);
				runner->face_landmark_session = std::make_unique<Ort::Session>(
					*runner->ort_env, land_path.c_str(), session_options);

				// Set I/O names for Qualcomm MediaPipe ONNX export.
				runner->detector_input_names.push_back("image");
				runner->detector_output_names.push_back("box_coords_1");
				runner->detector_output_names.push_back("box_coords_2");
				runner->detector_output_names.push_back("box_scores_1");
				runner->detector_output_names.push_back("box_scores_2");

				runner->landmark_input_names.push_back("image");
				runner->landmark_output_names.push_back("landmarks");

				// Try to load hand tracking models
				std::string hand_det_path = base_path + "palm_detection.onnx";
				std::string hand_land_path = base_path + "hand_landmark.onnx";
				FILE *f_hand_det = fopen(hand_det_path.c_str(), "rb");
				FILE *f_hand_land = fopen(hand_land_path.c_str(), "rb");

				if (f_hand_det && f_hand_land) {
					fclose(f_hand_det);
					fclose(f_hand_land);
					runner->hand_detector_session = std::make_unique<Ort::Session>(
						*runner->ort_env, hand_det_path.c_str(), session_options);
					runner->hand_landmark_session = std::make_unique<Ort::Session>(
						*runner->ort_env, hand_land_path.c_str(), session_options);

					runner->hand_detector_input_names.push_back("input");
					runner->hand_detector_output_names.push_back("output");

					runner->hand_landmark_input_names.push_back("input");
					runner->hand_landmark_output_names.push_back("output");

					blog(LOG_INFO, "[MediaPipe] Hand tracking models loaded successfully!");
				} else {
					if (f_hand_det)
						fclose(f_hand_det);
					if (f_hand_land)
						fclose(f_hand_land);
					blog(LOG_WARNING, "[MediaPipe] Hand tracking models not found");
				}

				// Try to load depth estimation model
				std::string depth_path = base_path + "depth_anything_v2.onnx";
				FILE *f_depth = fopen(depth_path.c_str(), "rb");

				if (f_depth) {
					fclose(f_depth);
					runner->depth_estimation_session = std::make_unique<Ort::Session>(
						*runner->ort_env, depth_path.c_str(), session_options);

					runner->depth_input_names.push_back("input");
					runner->depth_output_names.push_back("output");

					blog(LOG_INFO, "[MediaPipe] Depth estimation model loaded successfully!");
				} else {
					blog(LOG_WARNING, "[MediaPipe] Depth estimation model not found");
				}

				g_onnx_available = true;
				blog(LOG_INFO, "[MediaPipe] ONNX Runtime initialized successfully!");
			} else {
				blog(LOG_WARNING, "[MediaPipe] Models not found in: %s", base_path.c_str());
			}
		} catch (const std::exception &e) {
			blog(LOG_ERROR, "[MediaPipe] ONNX init failed: %s", e.what());
		}
	}
#endif

	// Backend availability is per runner, not global. A previous runner's
	// successful initialization must not authorize one with missing models.
	bool runner_ready = false;
#ifdef USE_LITERT_MEDIAPIPE
	runner_ready = runner->compiled_model_det && runner->compiled_model_land;
#endif
#ifdef USE_ONNX_MEDIAPIPE
	runner_ready = runner_ready || (runner->face_detector_session && runner->face_landmark_session);
#endif
	if (!runner_ready) {
		blog(LOG_ERROR,
		     "[MediaPipe] No real backend available. Configure USE_ONNX_RUNTIME or USE_LITERT with real models.");
		delete runner;
		return nullptr;
	}

	blog(LOG_INFO, "[MediaPipe] Backend ready: %s", mp_get_backend_name());

	return (mp_runner_t *)runner;
}

void mp_destroy(mp_runner_t *ptr)
{
	if (!ptr)
		return;
	mp_runner *runner = (mp_runner *)ptr;

#ifdef USE_LITERT_MEDIAPIPE
	// std::unique_ptr handles cleanup for compiled models
#endif

	delete runner;
}

bool mp_start_graph(mp_runner_t *ptr, mp_graph_type_t type)
{
	if (!ptr)
		return false;
	mp_runner *runner = (mp_runner *)ptr;
	// Only accept modes with an implemented output path and available models.
	bool face_available = false;
#ifdef USE_ONNX_MEDIAPIPE
	face_available = runner->face_detector_session && runner->face_landmark_session;
#endif
#ifdef USE_LITERT_MEDIAPIPE
	face_available = face_available || (runner->compiled_model_det && runner->compiled_model_land);
#endif
	const bool supported = type == MP_GRAPH_NONE ||
		(face_available && (type == MP_GRAPH_FACE_MESH ||
		                    type == MP_GRAPH_SELFIE_SEGMENTATION ||
		                    type == MP_GRAPH_PRIVACY));
	if (!supported) {
		// Do not keep a previous graph active after rejecting a selection.
		std::lock_guard<std::mutex> lock(runner->data_lock);
		runner->current_graph = MP_GRAPH_NONE;
		runner->has_face = false;
		runner->all_faces.clear();
		std::fill(runner->mask_buffer.begin(), runner->mask_buffer.end(), 0);
		blog(LOG_WARNING, "[MediaPipe] Graph %d unavailable: missing verified implementation or models",
		     (int)type);
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(runner->data_lock);
		runner->current_graph = type;
		runner->has_face = false;
		runner->all_faces.clear();
		std::fill(runner->mask_buffer.begin(), runner->mask_buffer.end(), 0);
	}

	const char *graph_name = "Unknown";
	switch (type) {
	case MP_GRAPH_SELFIE_SEGMENTATION:
		graph_name = "Selfie Segmentation";
		break;
	case MP_GRAPH_FACE_MESH:
		graph_name = "Face Mesh";
		break;
	case MP_GRAPH_HAND_TRACKING:
		graph_name = "Hand Tracking";
		break;
	case MP_GRAPH_POSE_ESTIMATION:
		graph_name = "Pose Estimation";
		break;
	case MP_GRAPH_DEPTH_ESTIMATION:
		graph_name = "Depth Estimation";
		break;
	case MP_GRAPH_CANNY:
		graph_name = "Canny Edge (ControlNet)";
		break;
	case MP_GRAPH_PRIVACY:
		graph_name = "Privacy Filter (Blur Bystanders)";
		break;
	default:
		break;
	}

	blog(LOG_INFO, "[MediaPipe] Started graph: %s (%s)", graph_name, mp_get_backend_name());
	return true;
}

extern "C" mp_graph_type_t mp_get_current_graph(mp_runner_t *ptr)
{
	if (!ptr)
		return MP_GRAPH_NONE;
	return ((mp_runner *)ptr)->current_graph;
}

/**
 * Process hand tracking using ONNX models
 */
static void process_hand_tracking_onnx(mp_runner *runner, const uint8_t *data, int width, int height, int stride)
{
	(void)runner;
	(void)data;
	(void)width;
	(void)height;
	(void)stride;
#ifdef USE_ONNX_MEDIAPIPE
	try {
		std::lock_guard<std::mutex> lock(runner->data_lock);
		runner->hand_results.clear();
		blog(LOG_ERROR,
		     "[MediaPipe] Hand tracking ONNX decoder is not implemented; refusing simulated output.");

	} catch (const std::exception &e) {
		blog(LOG_ERROR, "[MediaPipe] Hand tracking error: %s", e.what());
	}
#endif
}

/**
 * Process depth estimation using ONNX model
 */
static void process_depth_estimation_onnx(mp_runner *runner, const uint8_t *data, int width, int height, int stride)
{
	(void)runner;
	(void)data;
	(void)width;
	(void)height;
	(void)stride;
#ifdef USE_ONNX_MEDIAPIPE
	try {
		// Depth estimation typically uses 256x256 or 512x512 input
		const int depth_input_size = 256;
		std::vector<float> depth_input(depth_input_size * depth_input_size * 3);
		resize_and_normalize(data, width, height, stride, depth_input.data(), depth_input_size,
				     depth_input_size);

		std::array<int64_t, 4> depth_input_shape = {1, depth_input_size, depth_input_size, 3};
		Ort::Value depth_input_tensor =
			Ort::Value::CreateTensor<float>(runner->memory_info, depth_input.data(), depth_input.size(),
							depth_input_shape.data(), depth_input_shape.size());

		// Run depth estimation
		auto depth_outputs = runner->depth_estimation_session->Run(
			Ort::RunOptions{nullptr}, runner->depth_input_names.data(), &depth_input_tensor, 1,
			runner->depth_output_names.data(), runner->depth_output_names.size());

		// Process depth output
		auto *raw_depth = depth_outputs[0].GetTensorMutableData<float>();
		auto depth_shape = depth_outputs[0].GetTensorTypeAndShapeInfo().GetShape();

		std::lock_guard<std::mutex> lock(runner->data_lock);

		// Resize depth to output buffer size
		int out_width = runner->depth_width;
		int out_height = runner->depth_height;

		for (int y = 0; y < out_height; y++) {
			for (int x = 0; x < out_width; x++) {
				// Bilinear sampling from depth model output
				float src_x = (float)x / out_width * depth_input_size;
				float src_y = (float)y / out_height * depth_input_size;

				int x0 = (int)src_x;
				int y0 = (int)src_y;
				int x1 = std::min(x0 + 1, depth_input_size - 1);
				int y1 = std::min(y0 + 1, depth_input_size - 1);

				float fx = src_x - x0;
				float fy = src_y - y0;

				// Sample depth values (assuming single channel output)
				float d00 = raw_depth[y0 * depth_input_size + x0];
				float d01 = raw_depth[y1 * depth_input_size + x0];
				float d10 = raw_depth[y0 * depth_input_size + x1];
				float d11 = raw_depth[y1 * depth_input_size + x1];

				float depth = d00 * (1 - fx) * (1 - fy) + d10 * fx * (1 - fy) + d01 * (1 - fx) * fy +
					      d11 * fx * fy;

				runner->depth_buffer[y * out_width + x] = clamp01(depth);
			}
		}

		// Update depth range
		auto [min_it, max_it] = std::minmax_element(runner->depth_buffer.begin(), runner->depth_buffer.end());
		runner->depth_min = *min_it;
		runner->depth_max = *max_it;

	} catch (const std::exception &e) {
		blog(LOG_ERROR, "[MediaPipe] Depth estimation error: %s", e.what());
	}
#endif
}

/**
 * Process hand tracking using LiteRT 2.x models
 */
static void process_hand_tracking_litert(mp_runner *runner, const uint8_t *data, int width, int height, int stride)
{
#ifdef USE_LITERT_MEDIAPIPE
	if (!runner->compiled_model_hand_det || !runner->compiled_model_hand_land)
		return;

	try {
		// 1. Palm Detection
		// (Assume 224x224 input based on standard MediaPipe palm detector)
		const int palm_size = 224;
		std::vector<float> palm_input(palm_size * palm_size * 3);
		resize_and_normalize(data, width, height, stride, palm_input.data(), palm_size, palm_size);

		runner->hand_det_inputs[0].Write(palm_input.data());
		if (runner->compiled_model_hand_det->Run(0, runner->hand_det_inputs, runner->hand_det_outputs).ok()) {
			std::lock_guard<std::mutex> lock(runner->data_lock);
			runner->hand_results.clear();
			blog(LOG_ERROR,
			     "[MediaPipe] LiteRT hand tracking decoder is not implemented; refusing simulated output.");
		}
	} catch (const std::exception &e) {
		blog(LOG_ERROR, "[MediaPipe] LiteRT 2.x Hand tracking failed: %s", e.what());
	}
#else
	(void)runner;
	(void)data;
	(void)width;
	(void)height;
	(void)stride;
#endif
}

/**
 * Process a video frame for landmarks/segmentation.
 */
bool mp_process_frame(mp_runner_t *ptr, const uint8_t *data, int width, int height, int stride)
{
#if !defined(USE_ONNX_MEDIAPIPE) && !defined(USE_LITERT_MEDIAPIPE)
	(void)width;
	(void)height;
	(void)stride;
#endif
	if (!ptr)
		return false;
	mp_runner *runner = (mp_runner *)ptr;
	if (!data)
		return false;

#ifdef USE_LITERT_MEDIAPIPE
	if (g_litert_available) {

		// === Canny Edge Plugin (LiteRT 2.x) ===
		if (runner->current_graph == MP_GRAPH_CANNY && runner->compiled_model_canny) {
			resize_and_normalize(data, width, height, stride, runner->detector_input.data(),
					     CANNY_INPUT_SIZE, CANNY_INPUT_SIZE);

			runner->canny_inputs[0].Write(runner->detector_input.data());
			if (runner->compiled_model_canny->Run(0, runner->canny_inputs, runner->canny_outputs).ok()) {
				// Success
			}
			return true;
		}

		if (runner->compiled_model_det && runner->compiled_model_land) {
			// === STAGE 1: Face Detection (LiteRT 2.x) ===
			resize_and_normalize(data, width, height, stride, runner->detector_input.data(),
					     FACE_DETECTOR_INPUT_SIZE, FACE_DETECTOR_INPUT_SIZE);

			runner->det_inputs[0].Write(runner->detector_input.data());

			if (runner->compiled_model_det->Run(0, runner->det_inputs, runner->det_outputs).ok()) {
				std::vector<float> raw_boxes(runner->det_outputs[0].GetSize() / sizeof(float));
				std::vector<float> raw_scores(runner->det_outputs[1].GetSize() / sizeof(float));

				runner->det_outputs[0].Read(raw_boxes.data());
				runner->det_outputs[1].Read(raw_scores.data());

				std::vector<FaceBox> faces = decode_face_detections(raw_boxes.data(), raw_scores.data(),
										    runner->anchors, NUM_ANCHORS,
										    FACE_DETECTION_THRESHOLD);

				std::lock_guard<std::mutex> lock(runner->data_lock);
				runner->all_faces = faces;

				if (!faces.empty()) {
					runner->has_face = true;
					runner->last_face_box = faces[0];

					// === STAGE 2: Landmarks (LiteRT 2.x) ===
					crop_resize_and_normalize(data, width, height, stride, runner->last_face_box,
								  runner->landmark_input.data(),
								  FACE_LANDMARK_INPUT_SIZE);

					runner->land_inputs[0].Write(runner->landmark_input.data());

					if (runner->compiled_model_land
						    ->Run(0, runner->land_inputs, runner->land_outputs)
						    .ok()) {
						std::vector<float> raw_landmarks(runner->land_outputs[0].GetSize() /
										 sizeof(float));
						runner->land_outputs[0].Read(raw_landmarks.data());

						// Transform landmarks
						float expand = 0.25f;
						float box_size = fmaxf(runner->last_face_box.width,
								       runner->last_face_box.height) *
								 (1.0f + expand);
						float box_x = runner->last_face_box.x_center - box_size / 2.0f;
						float box_y = runner->last_face_box.y_center - box_size / 2.0f;

						for (int i = 0; i < FACE_MESH_LANDMARK_COUNT; i++) {
							float lx = raw_landmarks[i * 3 + 0] / FACE_LANDMARK_INPUT_SIZE;
							float ly = raw_landmarks[i * 3 + 1] / FACE_LANDMARK_INPUT_SIZE;
							float lz = raw_landmarks[i * 3 + 2] / FACE_LANDMARK_INPUT_SIZE;

							runner->landmarks[i].x = box_x + lx * box_size;
							runner->landmarks[i].y = box_y + ly * box_size;
							runner->landmarks[i].z = lz;
							runner->landmarks[i].visibility = 1.0f;
							runner->landmarks[i].presence = 1.0f;
						}

						// Generate mask (reused logic)
						if (runner->current_graph == MP_GRAPH_SELFIE_SEGMENTATION ||
						    runner->current_graph == MP_GRAPH_FACE_MESH) {
							int mw = runner->mask_width;
							int mh = runner->mask_height;
							for (int y = 0; y < mh; y++) {
								for (int x = 0; x < mw; x++) {
									float u = (float)x / mw;
									float v = (float)y / mh;
									float dx =
										(u - runner->last_face_box.x_center) /
										(runner->last_face_box.width * 0.6f);
									float dy =
										(v - runner->last_face_box.y_center) /
										(runner->last_face_box.height * 0.8f);
									float dist = sqrtf(dx * dx + dy * dy);
									float alpha =
										1.0f - smoothstep(0.8f, 1.0f, dist);
									runner->mask_buffer[y * mw + x] =
										(uint8_t)(clamp01(alpha) * 255.0f);
								}
							}
						}
					}
				} else {
					runner->has_face = false;
					runner->all_faces.clear();
					memset(runner->mask_buffer.data(), 0, runner->mask_buffer.size());
				}
			}
		}

		// Process Hand Tracking if active
		if (runner->current_graph == MP_GRAPH_HAND_TRACKING) {
			process_hand_tracking_litert(runner, data, width, height, stride);
		}

		return true;
	}
#endif
#ifdef USE_ONNX_MEDIAPIPE
	if (g_onnx_available && runner->face_detector_session && runner->face_landmark_session &&
	    (runner->current_graph == MP_GRAPH_FACE_MESH ||
	     runner->current_graph == MP_GRAPH_SELFIE_SEGMENTATION ||
	     runner->current_graph == MP_GRAPH_PRIVACY)) {
		try {
			// === STAGE 1: Face Detection ===
			// Qualcomm MediaPipe ONNX expects NCHW [1,3,256,256], normalized to [0,1].
			resize_and_normalize_chw(data, width, height, stride, runner->detector_input.data(),
						 FACE_DETECTOR_INPUT_SIZE, FACE_DETECTOR_INPUT_SIZE);

			// Create input tensor
			std::array<int64_t, 4> detector_input_shape = {1, 3, FACE_DETECTOR_INPUT_SIZE,
								       FACE_DETECTOR_INPUT_SIZE};
			Ort::Value detector_input_tensor = Ort::Value::CreateTensor<float>(
				runner->memory_info, runner->detector_input.data(), runner->detector_input.size(),
				detector_input_shape.data(), detector_input_shape.size());

			// Run face detection
			auto detector_outputs = runner->face_detector_session->Run(
				Ort::RunOptions{nullptr}, runner->detector_input_names.data(), &detector_input_tensor,
				1, runner->detector_output_names.data(), runner->detector_output_names.size());

			// Decode detections: boxes/scores are split as 512 + 384 anchors.
			std::vector<float> boxes(NUM_ANCHORS * 16);
			std::vector<float> scores(NUM_ANCHORS);
			float *boxes1 = detector_outputs[0].GetTensorMutableData<float>();
			float *boxes2 = detector_outputs[1].GetTensorMutableData<float>();
			float *scores1 = detector_outputs[2].GetTensorMutableData<float>();
			float *scores2 = detector_outputs[3].GetTensorMutableData<float>();
			memcpy(boxes.data(), boxes1, 512 * 16 * sizeof(float));
			memcpy(boxes.data() + 512 * 16, boxes2, 384 * 16 * sizeof(float));
			memcpy(scores.data(), scores1, 512 * sizeof(float));
			memcpy(scores.data() + 512, scores2, 384 * sizeof(float));

			std::vector<FaceBox> faces = decode_face_detections(
				boxes.data(), scores.data(), runner->anchors, NUM_ANCHORS, FACE_DETECTION_THRESHOLD);

			std::lock_guard<std::mutex> lock(runner->data_lock);
			runner->all_faces = faces;

			if (!faces.empty()) {
				runner->has_face = true;
				runner->last_face_box = faces[0];

				// === STAGE 2: Face Landmark Detection ===
				// Crop face region and resize to 192x192
				crop_resize_and_normalize_chw(data, width, height, stride, runner->last_face_box,
							      runner->landmark_input.data(), FACE_LANDMARK_INPUT_SIZE);

				// Create landmark input tensor
				std::array<int64_t, 4> landmark_input_shape = {1, 3, FACE_LANDMARK_INPUT_SIZE,
									       FACE_LANDMARK_INPUT_SIZE};
				Ort::Value landmark_input_tensor = Ort::Value::CreateTensor<float>(
					runner->memory_info, runner->landmark_input.data(),
					runner->landmark_input.size(), landmark_input_shape.data(),
					landmark_input_shape.size());

				// Run landmark detection
				auto landmark_outputs = runner->face_landmark_session->Run(
					Ort::RunOptions{nullptr}, runner->landmark_input_names.data(),
					&landmark_input_tensor, 1, runner->landmark_output_names.data(),
					runner->landmark_output_names.size());

				// Extract landmarks [1, 468, 3] (x, y, z)
				auto *raw_landmarks = landmark_outputs[0].GetTensorMutableData<float>();

				// Transform landmarks back to original image coordinates
				float expand = 0.25f;
				float box_size = fmaxf(runner->last_face_box.width, runner->last_face_box.height) *
						 (1.0f + expand);
				float box_x = runner->last_face_box.x_center - box_size / 2.0f;
				float box_y = runner->last_face_box.y_center - box_size / 2.0f;

				for (int i = 0; i < FACE_MESH_LANDMARK_COUNT; i++) {
					// Qualcomm's ONNX Face Landmark returns normalized crop
					// coordinates. Dividing by 192 collapsed the face mesh.
					float lx = raw_landmarks[i * 3 + 0];
					float ly = raw_landmarks[i * 3 + 1];
					float lz = raw_landmarks[i * 3 + 2];

					// Transform to original image coordinates
					runner->landmarks[i].x = box_x + lx * box_size;
					runner->landmarks[i].y = box_y + ly * box_size;
					runner->landmarks[i].z = lz; // Z is relative depth
					runner->landmarks[i].visibility = 1.0f;
					runner->landmarks[i].presence = 1.0f;
				}

				// Generate segmentation mask based on face region
				if (runner->current_graph == MP_GRAPH_SELFIE_SEGMENTATION ||
				    runner->current_graph == MP_GRAPH_FACE_MESH) {

					int mw = runner->mask_width;
					int mh = runner->mask_height;

					for (int y = 0; y < mh; y++) {
						for (int x = 0; x < mw; x++) {
							float u = (float)x / mw;
							float v = (float)y / mh;

							// Ellipse mask based on face bounds
							float dx = (u - runner->last_face_box.x_center) /
								   (runner->last_face_box.width * 0.6f);
							float dy = (v - runner->last_face_box.y_center) /
								   (runner->last_face_box.height * 0.8f);
							float dist = sqrtf(dx * dx + dy * dy);

							float alpha = 1.0f - smoothstep(0.8f, 1.0f, dist);
							runner->mask_buffer[y * mw + x] =
								(uint8_t)(clamp01(alpha) * 255.0f);
						}
					}
				} else if (runner->current_graph == MP_GRAPH_PRIVACY) {
					// Privacy Mode: Blur Bystanders
					// 1. Identify Primary Face (closest to center)
					int primary_idx = -1;
					float min_dist = 1000.0f;
					for (size_t i = 0; i < runner->all_faces.size(); i++) {
						float dx = runner->all_faces[i].x_center - 0.5f;
						float dy = runner->all_faces[i].y_center - 0.5f;
						float dist = dx * dx + dy * dy;
						if (dist < min_dist) {
							min_dist = dist;
							primary_idx = (int)i;
						}
					}

					// 2. Clear mask (White = Keep)
					memset(runner->mask_buffer.data(), 255, runner->mask_buffer.size());

					// 3. Draw Black Ellipses for Bystanders
					int mw = runner->mask_width;
					int mh = runner->mask_height;

					for (size_t i = 0; i < runner->all_faces.size(); i++) {
						if ((int)i == primary_idx)
							continue; // Skip streamer

						const FaceBox &face = runner->all_faces[i];

						// Expand blur area slightly
						float blur_w = face.width * 1.2f;
						float blur_h = face.height * 1.5f;

						// Bounding box of ellipse to optimize loop
						int x0 = (int)((face.x_center - blur_w / 2) * mw);
						int y0 = (int)((face.y_center - blur_h / 2) * mh);
						int x1 = (int)((face.x_center + blur_w / 2) * mw);
						int y1 = (int)((face.y_center + blur_h / 2) * mh);

						x0 = std::max(0, x0);
						y0 = std::max(0, y0);
						x1 = std::min(mw, x1);
						y1 = std::min(mh, y1);

						for (int y = y0; y < y1; y++) {
							for (int x = x0; x < x1; x++) {
								float u = (float)x / mw;
								float v = (float)y / mh;

								float dx = (u - face.x_center) / (blur_w * 0.5f);
								float dy = (v - face.y_center) / (blur_h * 0.5f);
								float dist = dx * dx + dy * dy;

								if (dist < 1.0f) {
									// Gradient edge
									float alpha = smoothstep(0.8f, 1.0f, dist);
									// Multiply existing mask (so overlapping blur zones merge)
									uint8_t current =
										runner->mask_buffer[y * mw + x];
									runner->mask_buffer[y * mw + x] =
										(uint8_t)(std::min((float)current,
												   alpha * 255.0f));
								}
							}
						}
					}
				}
			} else {
				runner->has_face = false;
				runner->all_faces.clear();
				// In privacy mode, no bystanders means keep the whole frame.
				memset(runner->mask_buffer.data(),
				       runner->current_graph == MP_GRAPH_PRIVACY ? 255 : 0,
				       runner->mask_buffer.size());
			}

			return true;
		} catch (const Ort::Exception &e) {
			blog(LOG_ERROR, "[MediaPipe] ONNX error: %s", e.what());
			return false;
		}
	}
#endif

#ifdef USE_ONNX_MEDIAPIPE
	// Process hand tracking if enabled
	if (runner->current_graph == MP_GRAPH_HAND_TRACKING && runner->hand_detector_session &&
	    runner->hand_landmark_session) {
		process_hand_tracking_onnx(runner, data, width, height, stride);
		return true;
	}

	// Process depth estimation if enabled
	if (runner->current_graph == MP_GRAPH_DEPTH_ESTIMATION && runner->depth_estimation_session) {
		process_depth_estimation_onnx(runner, data, width, height, stride);
		return true;
	}

	if (g_onnx_available) {
		blog(LOG_WARNING, "[MediaPipe] No ONNX model path implemented for graph: %d", runner->current_graph);
		return false;
	}
#endif

#ifdef USE_LITERT_MEDIAPIPE
	if (g_litert_available) {
		blog(LOG_WARNING, "[MediaPipe] No LiteRT model path implemented for graph: %d", runner->current_graph);
		return false;
	}
#endif

	blog(LOG_WARNING,
	     "[MediaPipe] No implemented backend path for active graph %d; refusing synthetic output",
	     runner->current_graph);
	return false;
}

bool mp_process_image(mp_runner_t *ptr, const uint8_t *data, int width, int height, int stride,
		      mp_landmark_list_t *out_landmarks)
{
	if (!ptr || !out_landmarks)
		return false;

#ifdef USE_ONNX_MEDIAPIPE
	mp_runner *runner = (mp_runner *)ptr;
	if (g_onnx_available && runner->face_landmark_session) {
		if (mp_process_frame(ptr, data, width, height, stride)) {
			std::lock_guard<std::mutex> lock(runner->data_lock);
			if (runner->has_face) {
				int count = (int)runner->landmarks.size();
				if (count > 0) {
					out_landmarks->landmarks =
						(mp_landmark_t *)bmalloc(count * sizeof(mp_landmark_t));
					memcpy(out_landmarks->landmarks, runner->landmarks.data(),
					       count * sizeof(mp_landmark_t));
					out_landmarks->count = count;
					return true;
				}
			}
		}
		return false;
	}
#else
	(void)data;
	(void)width;
	(void)height;
	(void)stride;
#endif
	return false;
}

bool mp_get_segmentation_mask(mp_runner_t *ptr, mp_mask_data_t *out_mask)
{
	if (!ptr || !out_mask)
		return false;
	mp_runner *runner = (mp_runner *)ptr;

	std::lock_guard<std::mutex> lock(runner->data_lock);

	size_t size = runner->mask_buffer.size();
	if (size == 0)
		return false;

	out_mask->buffer = (uint8_t *)bmalloc(size);
	memcpy(out_mask->buffer, runner->mask_buffer.data(), size);
	out_mask->width = runner->mask_width;
	out_mask->height = runner->mask_height;
	out_mask->channels = 1;
	return true;
}

bool mp_get_landmarks(mp_runner_t *ptr, mp_landmark_list_t *out_landmarks)
{
	if (!ptr || !out_landmarks)
		return false;
	mp_runner *runner = (mp_runner *)ptr;

	std::lock_guard<std::mutex> lock(runner->data_lock);

	int count = (int)runner->landmarks.size();
	if (!runner->has_face || count == 0)
		return false;

	out_landmarks->landmarks = (mp_landmark_t *)bmalloc(count * sizeof(mp_landmark_t));
	memcpy(out_landmarks->landmarks, runner->landmarks.data(), count * sizeof(mp_landmark_t));
	out_landmarks->count = count;
	return true;
}

bool mp_get_hand_results(mp_runner_t *ptr, mp_hand_result_t *out_hands, int *out_count, int max_count)
{
	if (!ptr || !out_hands || !out_count)
		return false;
	mp_runner *runner = (mp_runner *)ptr;

	std::lock_guard<std::mutex> lock(runner->data_lock);

	int count = std::min((int)runner->hand_results.size(), max_count);
	*out_count = count;

	for (int i = 0; i < count; i++) {
		out_hands[i] = runner->hand_results[i];
	}

	return count > 0;
}

bool mp_get_depth_map(mp_runner_t *ptr, mp_depth_data_t *out_depth)
{
	(void)ptr;
	(void)out_depth;
	// A preallocated zero-filled buffer is not a depth estimate.
	return false;
}

// Query backend information
const char *mp_get_backend_name(void)
{
#ifdef USE_LITERT_MEDIAPIPE
	if (g_litert_available)
		return "LiteRT";
#endif
#ifdef USE_ONNX_MEDIAPIPE
	if (g_onnx_available)
		return "ONNX";
#endif
	return MP_BACKEND_NAME;
}

bool mp_is_onnx_available(void)
{
#ifdef USE_ONNX_MEDIAPIPE
	return g_onnx_available;
#else
	return false;
#endif
}

} // extern "C"
