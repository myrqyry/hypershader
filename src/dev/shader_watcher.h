/**
 * @file shader_watcher.h
 * @brief Shader file hot-reload support for development
 * 
 * Monitors shader files for modifications and triggers automatic
 * reloading during development. Disabled by default in production.
 */

#ifndef SHADER_WATCHER_H
#define SHADER_WATCHER_H

#include <obs-module.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include "../safe_alloc.h"

/**
 * Shader watcher configuration
 */
typedef struct shader_watcher {
    char *watched_path;          /* Path to shader file being watched */
    time_t last_modified;        /* Last modification time */
    time_t last_check;           /* Last time we checked for modifications */
    double check_interval_sec;   /* Minimum seconds between checks */
    bool enabled;                /* Hot-reload enabled flag */
    bool needs_reload;           /* Flag set when reload is needed */
} shader_watcher_t;

/**
 * @brief Initialize shader watcher
 * @param watcher Pointer to watcher structure
 */
static inline void shader_watcher_init(shader_watcher_t *watcher)
{
    if (!watcher) return;
    memset(watcher, 0, sizeof(*watcher));
    watcher->check_interval_sec = 1.0; /* Check once per second by default */
    watcher->enabled = false;
}

/**
 * @brief Cleanup shader watcher resources
 * @param watcher Pointer to watcher structure
 */
static inline void shader_watcher_cleanup(shader_watcher_t *watcher)
{
    if (!watcher) return;
    safe_bfree(watcher->watched_path);
    memset(watcher, 0, sizeof(*watcher));
}

/**
 * @brief Set the file path to watch
 * @param watcher Pointer to watcher structure
 * @param path File path to watch
 * @return true on success
 */
static inline bool shader_watcher_set_path(shader_watcher_t *watcher, const char *path)
{
    if (!watcher) return false;
    
    /* Free old path */
    safe_bfree(watcher->watched_path);
    watcher->watched_path = NULL;
    watcher->last_modified = 0;
    watcher->needs_reload = false;
    
    if (!path || path[0] == '\0') {
        return true; /* Successfully cleared */
    }
    
    /* Set new path */
    watcher->watched_path = safe_bstrdup(path, "shader_watcher");
    if (!watcher->watched_path) {
        return false;
    }
    
    /* Get initial modification time */
    struct stat st;
    if (stat(path, &st) == 0) {
        watcher->last_modified = st.st_mtime;
    }
    
    return true;
}

/**
 * @brief Enable or disable hot-reload
 * @param watcher Pointer to watcher structure
 * @param enabled Enable flag
 */
static inline void shader_watcher_set_enabled(shader_watcher_t *watcher, bool enabled)
{
    if (!watcher) return;
    watcher->enabled = enabled;
    
    if (enabled) {
        blog(LOG_INFO, "[hypershader] Shader hot-reload enabled for: %s",
             watcher->watched_path ? watcher->watched_path : "(none)");
    } else {
        blog(LOG_DEBUG, "[hypershader] Shader hot-reload disabled");
    }
}

/**
 * @brief Check if shader file has been modified
 * 
 * Should be called periodically (e.g., once per frame or once per second).
 * Returns true if the file was modified since the last reload.
 * 
 * @param watcher Pointer to watcher structure
 * @return true if file was modified and reload is needed
 */
static inline bool shader_watcher_check(shader_watcher_t *watcher)
{
    if (!watcher || !watcher->enabled || !watcher->watched_path) {
        return false;
    }
    
    /* Throttle checks */
    time_t now = time(NULL);
    if (difftime(now, watcher->last_check) < watcher->check_interval_sec) {
        return watcher->needs_reload; /* Return cached result */
    }
    watcher->last_check = now;
    
    /* Check file modification time */
    struct stat st;
    if (stat(watcher->watched_path, &st) != 0) {
        /* File disappeared or inaccessible */
        return false;
    }
    
    /* Check if modified */
    if (st.st_mtime > watcher->last_modified) {
        blog(LOG_INFO, "[hypershader] Shader file modified, scheduling reload: %s",
             watcher->watched_path);
        watcher->needs_reload = true;
        return true;
    }
    
    return false;
}

/**
 * @brief Mark reload as complete
 * 
 * Call this after successfully reloading the shader.
 * 
 * @param watcher Pointer to watcher structure
 */
static inline void shader_watcher_acknowledge(shader_watcher_t *watcher)
{
    if (!watcher || !watcher->watched_path) return;
    
    /* Update modification time */
    struct stat st;
    if (stat(watcher->watched_path, &st) == 0) {
        watcher->last_modified = st.st_mtime;
    }
    
    watcher->needs_reload = false;
    blog(LOG_DEBUG, "[hypershader] Shader reload acknowledged");
}

/**
 * @brief Set check interval
 * @param watcher Pointer to watcher structure
 * @param interval_sec Seconds between checks (minimum 0.1)
 */
static inline void shader_watcher_set_interval(shader_watcher_t *watcher, double interval_sec)
{
    if (!watcher) return;
    if (interval_sec < 0.1) interval_sec = 0.1;
    watcher->check_interval_sec = interval_sec;
}

#endif /* SHADER_WATCHER_H */
