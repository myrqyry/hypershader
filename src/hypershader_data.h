#ifndef HYPERSHADER_DATA_H
#define HYPERSHADER_DATA_H

#include <stdatomic.h>

#include <obs-module.h>
#include "audio_thread.h"
#include "audio/music_timing.h"
#include "audio/reaction_dynamics.h"
#include "sprite_atlas.h"
#include <graphics/graphics.h>
#include "channel_image_compat.h"
#include <pthread.h>
#include <util/darray.h>
#include <string.h>

/* Vendored lightweight kissfft wrapper */
/* Assumes kiss_fftr.h is in the include path */
#include "kiss_fftr.h"

#include <graphics/matrix4.h>
#include <graphics/vec2.h>
#include <graphics/vec3.h>
#include <graphics/vec4.h>

#ifdef __linux__
#include <X11/Xlib.h>
#endif

/* Include MediaPipe Wrapper */
#include "mediapipe/mp_wrapper.h"
#include "mediapipe/face_surface.h"
#include "interop/sight_touch.h"

/* Input mode choices */
#define INPUT_MODE_SOURCE 0
#define INPUT_MODE_GLOBAL_CURSOR 1
#define INPUT_MODE_AUDIO 2
#define CHANNEL_COUNT 4

/* Instance kind: filter, source, or transition */
enum hypershader_instance_kind {
	HYPER_KIND_FILTER = 0,
	HYPER_KIND_SOURCE,
	HYPER_KIND_TRANSITION,
};

/* Source dimension bounds */
#define HYPER_SOURCE_WIDTH_MIN 64
#define HYPER_SOURCE_WIDTH_MAX 7680
#define HYPER_SOURCE_HEIGHT_MIN 64
#define HYPER_SOURCE_HEIGHT_MAX 4320
#define HYPER_SOURCE_WIDTH_DEFAULT 1920
#define HYPER_SOURCE_HEIGHT_DEFAULT 1080

/* Window types for FFT */
#define AUDIO_WIN_NONE 0
#define AUDIO_WIN_HANN 1
#define AUDIO_WIN_HAMMING 2

/* Import Modes */
#define IMPORT_MODE_URL 0
#define IMPORT_MODE_FILE 1
#define IMPORT_MODE_JSON_FILE 2
#define IMPORT_MODE_JSON_PASTE 3

/* Audio analysis feature IDs for mapping */
#define AUDIO_FEAT_RMS 0
#define AUDIO_FEAT_PEAK 1
#define AUDIO_FEAT_BAND0 2

#define AUDIO_SMOOTHING 0.85f /* exponential smoothing factor */
#define AUDIO_TEX_WIDTH 512
#define AUDIO_TEX_HEIGHT 2
#define AUDIO_MAP_SLOTS 4

/* Dynamics animation types */
#define DYNAMICS_NONE        0
#define DYNAMICS_SPRING      1      /* Second-order spring-damper */
#define DYNAMICS_SMOOTH_DAMP 2 /* Unity-style SmoothDamp */
#define DYNAMICS_EXPONENTIAL 3 /* Simple exponential ease */

struct shader_param_meta {
	char *name;
	float min;
	float max;
	float step;
	char *label;

	/* Explicit flags for whether values were set (robustness fix) */
	bool has_min;
	bool has_max;
	bool has_step;

	/* Audio Reactivity */
	char *audio_tag; /* e.g. "bass", "level" */
	float audio_gain;
	float audio_smooth;

	/* Dropdown support */
	char *list_options; /* "Label1,Label2,Label3" */

	/* Grouping support */
	char *group; /* "Deformation", "Lighting", etc. */
	char *visible_if; /* e.g. show_vectors or mode >= 2 */

	/* Cached effect parameter for runtime binding */
	gs_eparam_t *cached_param;

	/* Default value support */
	float default_val;
	char *default_val_str; /* For non-scalar defaults (e.g. "0.5,0.5,0.5") */
	bool has_default;

	/* Text Input Support (@text tag in shader metadata) */
	char *text_value;           /* The user-entered text string */
	char *text_font;            /* Font name/path (optional, uses default if NULL) */
	float text_size;            /* Font size in pixels (0 = auto) */
	bool is_text;               /* True if this param uses text-to-texture */
	gs_texture_t *text_texture; /* Runtime: rendered text texture */
	int text_tex_width;         /* Width of generated text texture */
	int text_tex_height;        /* Height of generated text texture */

	/* Dynamics (Spring-Damper) Animation System */
	int dynamics_type;     /* 0=none, 1=spring, 2=smooth_damp, 3=exponential */
	float dynamics_freq;   /* Natural frequency (Hz) - how fast it responds */
	float dynamics_damp;   /* Damping ratio: <1 bouncy, 1=critical, >1 overdamped */
	float dynamics_pos;    /* Current animated position (runtime state) */
	float dynamics_vel;    /* Current velocity (runtime state) */
	float dynamics_target; /* Target value to animate towards */
};

typedef struct {
	double avg_render_time_ms;
	double max_render_time_ms;
	uint32_t frame_count;
	uint64_t last_reset_time;
} performance_stats_t;

/* Cached view of a shader effect parameter for UI construction.
   Populated during shader load while in graphics context, then used
   by properties_add_shader_params without entering graphics context. */
struct shader_param_view {
	char *name;                     /* Param name (owned, bmalloc'd) */
	enum gs_shader_param_type type; /* GS_SHADER_PARAM_* */
};

typedef DARRAY(struct shader_param_meta) shader_meta_t;

struct shader_capabilities {
	bool audio_required;
	bool mediapipe_enabled;
	bool mediapipe_has_modes;
};

/* Reference-counted metadata wrapper for thread-safe property refresh.
   Property widgets may hold references to metadata strings during async
   refresh - this prevents use-after-free while avoiding memory leaks. */
struct shader_meta_rc {
	shader_meta_t meta;
	struct shader_capabilities capabilities;
	atomic_int refcount;
};

void metadata_rc_retain(struct shader_meta_rc *rc);
void metadata_rc_release(struct shader_meta_rc **rc_ptr);

/* Include guard tracking for shader preprocessor (prevents circular includes) */
struct include_guard {
	char **paths;    /* Array of already-included file paths */
	size_t count;    /* Number of paths in array */
	size_t capacity; /* Allocated capacity */
};

struct hypershader_data {
	/* Instance kind: filter, source, or transition */
	enum hypershader_instance_kind kind;

	/* Configurable source dimensions (only used for HYPER_KIND_SOURCE) */
	int source_width;
	int source_height;

	/* Internal */
	obs_source_t *context;
	obs_hotkey_pair_id effect_hotkeys;
	gs_effect_t *effect;
	float time_scale;
	float time_accumulator;
    float shader_frame_count; /* iFrame scales with global speed; physical frame_count does not */
    float compose_base_opacity;
    float compose_effect_opacity;
    int compose_blend_mode;
    gs_effect_t *output_compositor;
    gs_texrender_t *output_composite_fx;
    gs_texrender_t *output_composite_base;
    gs_texture_t *output_transparent;
    enum gs_color_format output_composite_format;
	float last_tick_delta;
	int frame_count;
	/* Sound shader support */
	bool sound_enabled;             /* UI toggle: enable sound output / preview */
	int sound_mix;                  /* mix index to use for audio input/output (preview uses this) */
	pthread_t sound_preview_thread; /* thread for preview generation */
	_Atomic bool sound_preview_running;

	pthread_t probe_thread; /* thread for mix probing */
	_Atomic bool probe_running;

	pthread_mutex_t mutex;                 /* General lock for effect/metadata sync */
	_Atomic bool refreshing_properties;    /* Re-entrancy guard for property callbacks */
	_Atomic bool needs_properties_refresh; /* Flag set when shader changes, cleared after refresh */
	unsigned long properties_generation;   /* Generation counter for callback invalidation */
	_Atomic bool properties_building;      /* Guard against re-entrant property callbacks */

	void *watcher;           /* struct shader_watcher* (dev mode) */

	char *shader_path;
	char *shader_error; /* Last compilation error message */
	struct vec2 mouse_pos;
	struct vec2 mouse_click;

	/* Input mode and optional audio source name */
	int input_mode;     /* INPUT_MODE_* */
	char *audio_source; /* optional; reserved for Audio mode */

	/* Channel Inputs */
	obs_source_t *channel_sources[4]; /* The active source object for each channel */
	char *channel_source_names[4];    /* The name of the selected source (saved setting) */
	char *channel_source_uuids[4];    /* UUID of the selected source (more reliable than name) */
	char *channel_files[4];           /* The path to the selected image file (saved setting) */
	int channel_types[4];             /* 0=None, 1=Image, 2=Source */
	bool channel_required[4];         /* Whether the shader code actually uses this channel */

	hs_channel_image_t channel_images[4]; /* Runtime-compatible OBS image state */
	gs_texture_t *channel_textures[4]; /* The final texture to bind (either from image or source) */

	/* Cached effect params (NULL if not present in the effect) */
	gs_eparam_t *ep_iTime;
	gs_eparam_t *ep_iScale; /* RESTORED */
	gs_eparam_t *ep_iResolution;
	gs_eparam_t *ep_screen_size; /* Helper for some shaders */
	gs_eparam_t *ep_iMouse;
	/* gs_eparam_t *ep_iMouseClick; REMOVED */
	gs_eparam_t *ep_iDate;
	gs_eparam_t *ep_iChannel[4];
	gs_eparam_t *ep_iChannelResolution
		[4]; /* ChRes is often an array in GLSL but individual in standard OBS effects? Using array of params to be safe. */
	gs_eparam_t *ep_iChannelTime[4];
	gs_eparam_t *ep_iTimeDelta;  /* optional */
	gs_eparam_t *ep_iFrame;      /* optional */
	gs_eparam_t *ep_iFrameRate;  /* optional */
	gs_eparam_t *ep_iSampleRate; /* optional */

	/* Audio Inputs */
	gs_eparam_t *ep_audio_reaction_state; /* opt-in three gates + spring position */
	struct hs_reaction_state reaction_state;
	uint64_t reaction_frame_ns;
	uint64_t reaction_input_key;
	gs_eparam_t *ep_iChannelAtlas[4];
	struct hs_atlas_clock atlas_clock[4];
	struct vec4 atlas_value[4];
	gs_eparam_t *ep_audio_level;
	gs_eparam_t *ep_audio_peak;
	gs_eparam_t *ep_audio_beat;
	gs_eparam_t *ep_audio_envelope;
    gs_eparam_t *ep_audio_music[HS_MUSIC_FIELDS];
	gs_eparam_t *ep_audio_band[8];   /* Cached handles for audio_band0-7 */
	gs_eparam_t *ep_audio_band_array; /* Optional float array uniform audio_band[8] */

	/* Scene / Camera Data */
	bool use_scene_data;
	gs_eparam_t *ep_view_mat;
	gs_eparam_t *ep_proj_mat;
	gs_eparam_t *ep_camera_pos;
	gs_eparam_t *ep_camera_dir;
	struct matrix4 view_mat;
	struct matrix4 proj_mat;
	struct vec3 camera_pos;
	struct vec3 camera_dir;

	/* Native Persistence: capture unmodified upstream frame, then ping-pong
	 * the actual effect output. All resources belong to the graphics thread. */
	gs_texrender_t *history[2];
	gs_texrender_t *feedback_input;
	int history_idx;
	uint32_t feedback_width, feedback_height;
	bool feedback_first_render_logged;
	gs_eparam_t *ep_iPreviousFrame;
	/* Opt-in two-buffer simulation graph: A -> B -> Draw.
	 * Each buffer has a previous-frame read texture and current write texture.
	 * Only the graphics thread touches these objects. */
	gs_texrender_t *graph_history[2][2]; /* [buffer A/B][ping/pong] */
	uint32_t graph_width, graph_height;
	enum gs_color_format graph_format;
	unsigned graph_write_idx;
	uint32_t graph_frame; /* reset on reload/resize, independent of OBS source age */
	uint64_t graph_flow_sequence; /* last successfully rendered Sight flow snapshot */
	bool graph_flow_announced;
	bool graph_valid;
	bool graph_enabled;
	gs_eparam_t *ep_iBufferA, *ep_iBufferB;
	gs_eparam_t *ep_iBufferAPrevious, *ep_iBufferBPrevious;
	gs_eparam_t *ep_iGraphFrame;
	/* Note: Removed duplicate 'inputs' array - channels ARE the shader inputs (iChannel0-3) */

	/* Channel Source Rendering */
	gs_texrender_t *channel_renders[4]; /* Render targets for OBS source inputs */

	/* Avoid repeated warnings for missing params */
	bool warned_missing_iTime;
	bool warned_missing_iResolution;
	bool warned_missing_iMouse;
	/* bool warned_missing_iMouseClick; // Removed */

	/* Audio preprocessing */
	int audio_sample_rate;      /* e.g., 48000 */
	int audio_window_size;      /* samples used for FFT / RMS */
	pthread_mutex_t audio_lock; /* protect audio worker state */

	/* Worker thread for feature computation (state machine managed) */
	audio_thread_manager_t audio_thread_mgr;
	pthread_cond_t audio_cv; /* for efficient sleeping/waking */

	/* Audio features (updated by worker) */
	float audio_level;        /* smoothed RMS */
	float audio_peak;         /* peak value */
	float audio_bands[8];     /* band energies (0..1) */
	float audio_beat;         /* short onset impulse, ~60ms decay */
	float audio_envelope;     /* sustained beat envelope (0..1) */
    hs_music_timing music_timing;
    float audio_music[HS_MUSIC_FIELDS];
    float render_audio_music[HS_MUSIC_FIELDS];
	float render_audio_level; /* render-thread snapshot shared by graph passes */
	float render_audio_peak;
	float render_audio_bands[8];
	float render_audio_beat;
	float render_audio_envelope;

	/* Thread-safe snapshots for rendering */
	pthread_mutex_t audio_results_lock; /* separate lock for computed features */
	float audio_level_snapshot;
	float audio_peak_snapshot;
	float audio_bands_snapshot[8];
	float audio_beat_snapshot;
	float audio_envelope_snapshot;

	/* Onset detector state (worker thread only) */
	float beat_average;  /* slow moving average of the drive level */
	float beat_cooldown; /* seconds until the next onset may fire */

	/* Error reporting */
	char *audio_status_message; /* user-facing error message */
	bool audio_system_healthy;  /* simple health flag */

	/* Timestamp alignment with libobs tick rate */
	uint64_t last_audio_ts; /* Last processed audio timestamp (ns) */
	atomic_uint_fast64_t last_audio_capture_ns; /* Fresh incoming PCM, not repeated FFT windows */

	/* Lock-free audio ring buffer (replaces mutex-protected version) */
	struct {
		float *data;
		size_t capacity; /* Power of 2 for fast modulo */
		atomic_size_t write_pos;
		atomic_size_t read_pos;
	} audio_lockfree_ring;

	/* Latency monitoring for graceful degradation */
	struct {
		uint64_t avg_process_time_ns;
		size_t consecutive_overruns;
	} audio_latency_monitor;

	/* Pre-allocated audio scratch buffer (avoids allocation in real-time callback) */
	float *audio_mono_scratch;
	size_t audio_mono_scratch_size;

	/* Per-instance debug throttling state (moved from statics) */
	int audio_debug_tick;
	float audio_debug_last_level;
	float audio_debug_last_peak;
	float audio_debug_last_bands[8];

	/* Shader Metadata (refcounted for thread-safe property refresh) */
	struct shader_meta_rc *metadata_rc; /* Current metadata (refcounted) */
	float *meta_audio_values;           /* smoothed values matching metadata index */
	size_t meta_audio_count;

	/* Cached shader param views for UI (populated during load, read by properties UI) */
	struct shader_param_view *param_views; /* Array of cached param info */
	size_t param_view_count;

	float audio_smoothing; /* exponential smoothing factor (0..1) */
	int audio_update_rate; /* updates per second for audio worker */
	bool audio_test_tone;  /* generate a sine test tone when enabled */
	bool audio_debug;      /* enable debug logging for audio (throttled) */
	float audio_phase;     /* phase accumulator for test tone */


	/* User-configurable FFT settings */
	int audio_fft_size;    /* user-chosen FFT/window size */
	int audio_window_type; /* AUDIO_WIN_* */

	/* Guard reconfiguration (prevent nested re-entry) */
	_Atomic bool fft_reconfig_in_progress;
	_Atomic bool audio_reconfig_pending;
	int next_audio_fft_size;
	int next_audio_window_type;

	/* Per-instance metadata cache (avoids static race conditions) */
	char *cached_metadata_path;
	shader_meta_t cached_metadata;
	struct shader_capabilities cached_capabilities;

	/* FFT resources (kissfft wrapper) */
	int fft_n;               /* FFT size (power of two) */
	kiss_fftr_cfg fft_cfg;   /* FFT config */
	kiss_fft_scalar *fft_in; /* input buffer (size fft_n) */
	kiss_fft_cpx *fft_out;   /* output bins (size fft_n/2+1) */
	float *fft_window;       /* window buffer (size audio_window_size) */

	/* OBS audio connection state */
	audio_t *obs_audio;   /* handle returned by obs_get_audio() */
	bool audio_connected; /* whether we connected via audio_output_connect */
	size_t audio_mix_idx; /* mix index we're connected to (default 0) */
	obs_source_t *selected_audio_source; /* held ref for per-source capture */
	bool source_audio_connected;

	/* Mapping slots (map computed feature -> shader uniform) */
	char *audio_map_name[AUDIO_MAP_SLOTS];  /* uniform name */
	int audio_map_feature[AUDIO_MAP_SLOTS]; /* feature id */
	bool audio_map_enabled[AUDIO_MAP_SLOTS];
	gs_eparam_t *audio_map_param[AUDIO_MAP_SLOTS];

	/* Audio texture: row 0 FFT magnitudes, row 1 waveform. */
	gs_texture_t *audio_texture;
	uint8_t audio_texture_data[AUDIO_TEX_WIDTH * AUDIO_TEX_HEIGHT];
	float *fft_texture_history; /* Smoothing buffer for FFT texture (size AUDIO_TEX_WIDTH) */

	/* Audio texture update throttling (reduce GPU load) */
	uint64_t last_audio_tex_update_ns; /* Timestamp of last texture update */
	bool audio_tex_dirty;              /* Flag to force update on next frame */

	/* Audio texture optimization: change detection */
	uint8_t audio_texture_prev[AUDIO_TEX_WIDTH * AUDIO_TEX_HEIGHT];
	float audio_change_threshold;

	/* Hot Reload Error Recovery */
	struct {
		gs_effect_t *backup_effect;   /* Last known good effect */
		char *backup_error;           /* Error message from failed reload */
		uint64_t last_reload_attempt; /* Timestamp of last reload attempt */
		bool reload_pending;          /* True while queued UI reload is in-flight */
		bool recovery_mode;           /* True when using backup effect */
		int consecutive_failures;     /* Count of consecutive reload failures */
	} hot_reload_state;


	/* Plugin Data Path for Security Validation */
	char *data_path;

	/* Performance Metrics */
	performance_stats_t perf_stats;

	/* MediaPipe Integration */
	mp_runner_t *mp_runner;
	bool mp_init_attempted;           /* Attempt a native runner only when requested */
	bool mp_sync_ready;               /* mutex/cond initialized, even without worker */
	bool mp_enabled;                    /* Toggle in UI */
	gs_texture_t *mp_mask_tex;          /* Segmentation mask texture */
	gs_texture_t *mp_landmarks_tex;     /* Landmark data texture (if used) */
	gs_texture_t *mp_ref_landmarks_tex; /* Static Reference Landmarks (for puppeteering) */
	hs_face_surface *mp_face_surface;       /* Per-instance GPU face-surface raster */
	bool mp_analyze_trigger;            /* Trigger to analyze current frame for reference */

	gs_eparam_t *ep_iSightFlow, *ep_iSightFlowPresent, *ep_iSightFlowInterval, *ep_iSightFlowCoverage, *ep_iSightFlowStep;
	gs_texture_t *sight_flow_texture;
	uint64_t sight_flow_sequence;
	float sight_flow_interval, sight_flow_coverage;
	bool sight_flow_present, sight_flow_announced;
	gs_eparam_t *ep_iSightTextPresent, *ep_iSightTextCount;
	gs_eparam_t *ep_iSightTextMask, *ep_iSightTextBounds;
	gs_texture_t *sight_text_mask, *sight_text_bounds;
	uint64_t sight_text_sequence;
	uint32_t sight_text_count;
	bool sight_text_present, sight_text_announced;
	gs_eparam_t *ep_iSightMotionPresent;
	gs_eparam_t *ep_iSightMotionEnergy;
	float sight_motion_energy;
	bool sight_motion_present, sight_motion_announced;
	gs_eparam_t *ep_iSightMattePresent;
	gs_eparam_t *ep_iSightDepth, *ep_iSightDepthPresent;
	gs_texture_t *sight_depth_tex;
	uint64_t sight_depth_sequence;
	bool sight_depth_present, sight_depth_announced;
	gs_eparam_t *ep_iSightSubjectPresent;
	gs_eparam_t *ep_iSightSubjectBounds;
	gs_eparam_t *ep_iSightSubjectCenter;
	gs_eparam_t *ep_iSightSubjectCoverage;
	float sight_subject_bounds[4], sight_subject_center[2], sight_subject_coverage;
	bool sight_subject_present;
	gs_texture_t *sight_matte_tex;
	uint64_t sight_matte_sequence;
	bool sight_matte_present;
	bool sight_matte_announced;
	gs_eparam_t *ep_iSegmentation;       /* Shader uniform: sampler2D iSegmentation */
	gs_eparam_t *ep_iLandmarks;          /* Shader uniform: sampler2D iLandmarks */
	gs_eparam_t *ep_iFacePresent;        /* Shader uniform: valid current face detection */
	gs_eparam_t *ep_iReferenceLandmarks; /* Shader uniform: sampler2D iReferenceLandmarks */
	gs_eparam_t *ep_iFaceSurface;       /* Shader uniform: RGBA32F barycentric coverage */
	gs_eparam_t *ep_iFaceTriangles;     /* Shader uniform: triangle-to-landmark lookup */
	/* Optional Sight Tracking Signal Bus consumer. Sight owns inference; this
	 * instance reads snapshots from a private receiver source via libobs. */
	bool sight_use_tracking;
	char sight_channel[97];
	char sight_render_channel[97];
	bool sight_announced;
	obs_source_t *sight_receiver;
	uint32_t sight_retry_ticks;
	uint64_t sight_sequence;
	uint32_t sight_kind;
	uint32_t sight_point_count;
	uint32_t sight_hand_count;
	float sight_pinch[2];
	int sight_gesture[2];
	gs_texture_t *sight_points_tex;       /* RGBA32F, count x 1 */
	gs_texture_t *sight_hands_tex;        /* RGBA32F, 21 x 2 */
	gs_texture_t *sight_blendshape_tex;   /* RGBA32F, 64 x 1; score in R */
	gs_texture_t *sight_transform_tex;    /* RGBA32F, 4 x 1 raw matrix rows */
	hs_face_surface *sight_face_surface;
	gs_eparam_t *ep_iSightPoints;
	gs_eparam_t *ep_iSightHands;
    struct hs_sight_touch sight_touch;
    gs_eparam_t *ep_iSightTouch[2], *ep_iSightRipple[HS_TOUCH_CAPACITY];
	gs_eparam_t *ep_iSightBlendshapes;
	gs_eparam_t *ep_iSightHeadRows;
	gs_eparam_t *ep_iSightPresent;
	gs_eparam_t *ep_iSightKind;
	gs_eparam_t *ep_iSightPointCount;
	gs_eparam_t *ep_iSightHandCount;
	gs_eparam_t *ep_iSightPinch0;
	gs_eparam_t *ep_iSightPinch1;
	gs_eparam_t *ep_iSightGesture0;
	gs_eparam_t *ep_iSightGesture1;

	/* Staging buffer for GPU->CPU readback */
	gs_stagesurf_t *mp_staging_tex;
	uint8_t *mp_cpu_buffer;
	gs_texrender_t *mp_scaler; /* Resize to 192x192 on GPU before download */

	/* Pre-allocated MediaPipe task (avoids allocation in render callback) */
#define MP_FRAME_SIZE (192 * 192 * 4)       /* Fixed 192x192 RGBA buffer */
	uint8_t *mp_preallocated_frame; /* Pre-allocated frame buffer for worker */
	bool mp_frame_buffer_in_use;    /* Flag: true when worker is processing */

	/* Embedded MediaPipe task storage (avoids bzalloc in video_render) */
	struct mp_process_task_data {
		struct hypershader_data *s;
		uint8_t *frame_data;
		int width;
		int height;
	} mp_pending_task_storage;
	bool mp_pending_task_active; /* Flag: true when data is ready for worker */

	/* Dedicated MediaPipe worker thread */
	pthread_t mp_thread;
	pthread_mutex_t mp_mutex;
	pthread_cond_t mp_cond;
	bool mp_thread_run;

	/* Pre-allocated buffer for audio worker (avoids bzalloc per iteration) */
	float *audio_localbuf;
	size_t audio_localbuf_size;

	/* Dedicated Shader Loading worker thread */
	pthread_t shader_load_thread;
	pthread_mutex_t shader_load_mutex;
	pthread_cond_t shader_load_cond;
	void *shader_load_pending_task; /* struct shader_load_task_data* */
	bool shader_load_thread_run;

	/* EnTT Scene Manager */
	void *scene;                 /* scene_handle_t */
	float *entity_data;          /* Buffer for entity data (x, y, scale, type) */
	int entity_count_max;        /* Buffer capacity */
	gs_eparam_t *ep_entities;    /* uniform float4 entities[100]; */
	gs_eparam_t *ep_entityCount; /* uniform int entityCount; */
    gs_eparam_t *ep_entityTexture; /* typed RGBA32F 100x1 entity data sampler */
    gs_texture_t *entity_texture;

	gs_texture_t *dummy_texture;

#ifdef __linux__
	Display *x11_display; /* Cached X11 display for cursor tracking */
#endif
};

static inline bool is_builtin_uniform(const char *name)
{
	if (!name)
		return false;
	if (strcmp(name, "iTime") == 0)
		return true;
	if (strcmp(name, "iTimeDelta") == 0)
		return true;
	if (strcmp(name, "iFrame") == 0)
		return true;
	if (strcmp(name, "iFrameRate") == 0)
		return true;
	if (strcmp(name, "iDate") == 0)
		return true;
	if (strcmp(name, "iSampleRate") == 0)
		return true;
	if (strcmp(name, "iResolution") == 0)
		return true;
	if (strcmp(name, "iProgress") == 0 || strcmp(name, "iScale") == 0)
		return true; /* Engine-injected transition progress and source scale */
	if (strcmp(name, "iMouse") == 0)
		return true;
	/* if (strcmp(name, "iMouseClick") == 0) return true; // Removed */
	if (strncmp(name, "iChannel", 8) == 0)
		return true; /* iChannelX, iChannelTime, iChannelResolution */
	/* Reserve precisely the native audio signals, never arbitrary audio_*
	 * controls (audio_gain, audio_attack and other shader-owned parameters). */
	static const char *const audio_names[] = {
		"audio_level", "audio_peak", "audio_beat", "audio_envelope",
		"audio_kick", "audio_snare", "audio_flux", "audio_bpm",
		"audio_beat_phase", "audio_bar_phase", "audio_beat_confidence",
		"audio_reaction_state"
	};
	for (size_t i = 0; i < sizeof(audio_names) / sizeof(audio_names[0]); ++i)
		if (strcmp(name, audio_names[i]) == 0)
			return true;
	if (strncmp(name, "audio_band", 10) == 0 &&
	    name[10] >= '0' && name[10] <= '7' && name[11] == '\0')
		return true;
	if (strcmp(name, "ViewProj") == 0)
		return true;
	if (strcmp(name, "World") == 0)
		return true;
	if (strcmp(name, "WorldViewProj") == 0)
		return true;
	if (strcmp(name, "image") == 0)
		return true;
	if (strncmp(name, "iSight", 6) == 0)
		return true; /* Sight-owned tracking snapshots, never editable metadata sliders */
	if (strcmp(name, "iFacePresent") == 0 || strcmp(name, "iFaceSurface") == 0 ||
	    strcmp(name, "iFaceTriangles") == 0 || strcmp(name, "iLandmarks") == 0 ||
	    strcmp(name, "iReferenceLandmarks") == 0 || strcmp(name, "iSegmentation") == 0)
		return true;
	if (strcmp(name, "iPreviousFrame") == 0)
		return true;
	if (strcmp(name, "iGraphFrame") == 0 || strncmp(name, "iBufferA", 8) == 0 ||
	    strncmp(name, "iBufferB", 8) == 0)
		return true;
	return false;
}

static inline shader_meta_t *hypershader_data_get_metadata(struct hypershader_data *s)
{
	return s && s->metadata_rc ? &s->metadata_rc->meta : NULL;
}

/* Shared Metadata parsing logic */
void parse_shader_metadata(const char *path, shader_meta_t *da, bool *channel_usage,
				  struct shader_capabilities *capabilities);
void free_shader_metadata(shader_meta_t *da);

/* Cache effect params for UI construction (called during shader load, in graphics context) */
void cache_effect_params(struct hypershader_data *s);
void free_effect_param_cache(struct hypershader_data *s);

#endif /* HYPERSHADER_DATA_H */
