/**
 * @file audio_thread.h
 * @brief Thread-safe audio worker management with explicit state machine
 * 
 * Provides proper synchronization for audio thread lifecycle to prevent
 * race conditions during start/stop operations.
 */

#ifndef AUDIO_THREAD_H
#define AUDIO_THREAD_H

#include <pthread.h>
#include <stdbool.h>
#include <obs-module.h>

/**
 * Audio thread state enumeration for explicit state machine
 */
typedef enum {
	AUDIO_THREAD_STATE_STOPPED = 0,
	AUDIO_THREAD_STATE_STARTING,
	AUDIO_THREAD_STATE_RUNNING,
	AUDIO_THREAD_STATE_STOPPING
} audio_thread_state_t;

/**
 * Audio thread manager structure
 */
typedef struct audio_thread_manager {
	pthread_t thread;
	pthread_mutex_t state_lock;
	pthread_cond_t state_cv;
	audio_thread_state_t state;
	volatile bool request_stop;
	void *user_data;
} audio_thread_manager_t;

/**
 * @brief Initialize the audio thread manager
 * @param mgr Pointer to manager structure
 * @return true on success, false on failure
 */
static inline bool audio_thread_manager_init(audio_thread_manager_t *mgr)
{
	if (!mgr)
		return false;

	mgr->state = AUDIO_THREAD_STATE_STOPPED;
	mgr->request_stop = false;
	mgr->user_data = NULL;

	if (pthread_mutex_init(&mgr->state_lock, NULL) != 0) {
		blog(LOG_ERROR, "[AudioThread] Failed to initialize state mutex");
		return false;
	}

	if (pthread_cond_init(&mgr->state_cv, NULL) != 0) {
		pthread_mutex_destroy(&mgr->state_lock);
		blog(LOG_ERROR, "[AudioThread] Failed to initialize state condition variable");
		return false;
	}

	return true;
}

/**
 * @brief Destroy the audio thread manager (must be stopped first)
 * @param mgr Pointer to manager structure
 */
static inline void audio_thread_manager_destroy(audio_thread_manager_t *mgr)
{
	if (!mgr)
		return;

	/* Ensure thread is stopped before destroying */
	pthread_mutex_lock(&mgr->state_lock);
	if (mgr->state != AUDIO_THREAD_STATE_STOPPED) {
		blog(LOG_WARNING, "[AudioThread] Destroying manager while thread not stopped!");
		pthread_mutex_unlock(&mgr->state_lock);
		return;
	}
	pthread_mutex_unlock(&mgr->state_lock);

	pthread_cond_destroy(&mgr->state_cv);
	pthread_mutex_destroy(&mgr->state_lock);
}

/**
 * @brief Get current thread state (thread-safe)
 * @param mgr Pointer to manager structure
 * @return Current state
 */
static inline audio_thread_state_t audio_thread_get_state(audio_thread_manager_t *mgr)
{
	if (!mgr)
		return AUDIO_THREAD_STATE_STOPPED;

	pthread_mutex_lock(&mgr->state_lock);
	audio_thread_state_t state = mgr->state;
	pthread_mutex_unlock(&mgr->state_lock);

	return state;
}

/**
 * @brief Check if stop has been requested (for use in worker loop)
 * @param mgr Pointer to manager structure
 * @return true if stop requested
 */
static inline bool audio_thread_should_stop(audio_thread_manager_t *mgr)
{
	if (!mgr)
		return true;
	return mgr->request_stop;
}

/**
 * @brief Set state to RUNNING (called by worker after initialization)
 * @param mgr Pointer to manager structure
 */
static inline void audio_thread_set_running(audio_thread_manager_t *mgr)
{
	if (!mgr)
		return;

	pthread_mutex_lock(&mgr->state_lock);
	if (mgr->state == AUDIO_THREAD_STATE_STARTING) {
		mgr->state = AUDIO_THREAD_STATE_RUNNING;
		pthread_cond_broadcast(&mgr->state_cv);
	}
	pthread_mutex_unlock(&mgr->state_lock);
}

/**
 * @brief Set state to STOPPED (called by worker before exiting)
 * @param mgr Pointer to manager structure
 */
static inline void audio_thread_set_stopped(audio_thread_manager_t *mgr)
{
	if (!mgr)
		return;

	pthread_mutex_lock(&mgr->state_lock);
	mgr->state = AUDIO_THREAD_STATE_STOPPED;
	mgr->request_stop = false;
	pthread_cond_broadcast(&mgr->state_cv);
	pthread_mutex_unlock(&mgr->state_lock);
}

/**
 * @brief Start the audio worker thread
 * @param mgr Pointer to manager structure
 * @param worker_fn Worker function to run
 * @param user_data User data to pass to worker
 * @return true on success, false if already running or error
 */
static inline bool audio_thread_start(audio_thread_manager_t *mgr, void *(*worker_fn)(void *), void *user_data)
{
	if (!mgr || !worker_fn)
		return false;

	pthread_mutex_lock(&mgr->state_lock);

	/* Check current state - only start if stopped */
	if (mgr->state != AUDIO_THREAD_STATE_STOPPED) {
		blog(LOG_DEBUG, "[AudioThread] Cannot start: already in state %d", mgr->state);
		pthread_mutex_unlock(&mgr->state_lock);
		return false;
	}

	/* Transition to STARTING atomically */
	mgr->state = AUDIO_THREAD_STATE_STARTING;
	mgr->request_stop = false;
	mgr->user_data = user_data;

	pthread_mutex_unlock(&mgr->state_lock);

	/* Create thread (outside lock to avoid holding during syscall) */
	int ret = pthread_create(&mgr->thread, NULL, worker_fn, user_data);

	if (ret != 0) {
		blog(LOG_ERROR, "[AudioThread] pthread_create failed: %d", ret);

		pthread_mutex_lock(&mgr->state_lock);
		mgr->state = AUDIO_THREAD_STATE_STOPPED;
		pthread_mutex_unlock(&mgr->state_lock);

		return false;
	}

	blog(LOG_DEBUG, "[AudioThread] Thread started successfully");
	return true;
}

/**
 * @brief Stop the audio worker thread (blocking)
 * @param mgr Pointer to manager structure
 * @param wake_cv Optional condition variable to signal for waking worker
 * @return true if stopped successfully, false if not running
 */
static inline bool audio_thread_stop(audio_thread_manager_t *mgr, pthread_cond_t *wake_cv)
{
	if (!mgr)
		return false;

	pthread_mutex_lock(&mgr->state_lock);

	/* Check if actually running */
	if (mgr->state != AUDIO_THREAD_STATE_RUNNING && mgr->state != AUDIO_THREAD_STATE_STARTING) {
		pthread_mutex_unlock(&mgr->state_lock);
		return false;
	}

	/* Signal stop request and transition state */
	mgr->state = AUDIO_THREAD_STATE_STOPPING;
	mgr->request_stop = true;

	pthread_mutex_unlock(&mgr->state_lock);

	/* Wake worker if condition variable provided */
	if (wake_cv) {
		pthread_cond_signal(wake_cv);
	}

	/* Wait for thread to exit (outside lock to prevent deadlock) */
	pthread_join(mgr->thread, NULL);

	/* Ensure state is STOPPED */
	pthread_mutex_lock(&mgr->state_lock);
	mgr->state = AUDIO_THREAD_STATE_STOPPED;
	mgr->request_stop = false;
	pthread_mutex_unlock(&mgr->state_lock);

	blog(LOG_DEBUG, "[AudioThread] Thread stopped successfully");
	return true;
}

/**
 * @brief Check if thread is currently running
 * @param mgr Pointer to manager structure
 * @return true if running or starting
 */
static inline bool audio_thread_is_active(audio_thread_manager_t *mgr)
{
	if (!mgr)
		return false;

	audio_thread_state_t state = audio_thread_get_state(mgr);
	return (state == AUDIO_THREAD_STATE_RUNNING || state == AUDIO_THREAD_STATE_STARTING);
}

#endif /* AUDIO_THREAD_H */
