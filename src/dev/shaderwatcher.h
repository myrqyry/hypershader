#ifndef SHADER_WATCHER_H
#define SHADER_WATCHER_H

#ifdef _DEBUG

#include <sys/stat.h>
#include <time.h>
#include <obs-module.h>

struct shader_watcher {
    char *path;
    uint64_t last_modified_ns;
    bool enabled;
};

static struct shader_watcher *shader_watcher_create(const char *path) {
    if (!path) return NULL;
    struct shader_watcher *w = bzalloc(sizeof(struct shader_watcher));
    w->path = bstrdup(path);
    w->enabled = true;
    
    struct stat st;
    if (stat(path, &st) == 0) {
        #ifdef __APPLE__
        w->last_modified_ns = (uint64_t)st.st_mtimespec.tv_sec * 1000000000ULL + st.st_mtimespec.tv_nsec;
        #else
        w->last_modified_ns = (uint64_t)st.st_mtim.tv_sec * 1000000000ULL + st.st_mtim.tv_nsec;
        #endif
    }
    return w;
}

static void shader_watcher_destroy(struct shader_watcher *w) {
    if (!w) return;
    bfree(w->path);
    bfree(w);
}

static bool check_shader_modified(struct shader_watcher *w) {
    if (!w || !w->enabled || !w->path) return false;
    
    struct stat st;
    if (stat(w->path, &st) != 0) return false;
    
    #ifdef __APPLE__
    uint64_t current_mtime = (uint64_t)st.st_mtimespec.tv_sec * 1000000000ULL + st.st_mtimespec.tv_nsec;
    #else
    uint64_t current_mtime = (uint64_t)st.st_mtim.tv_sec * 1000000000ULL + st.st_mtim.tv_nsec;
    #endif

    if (current_mtime > w->last_modified_ns) {
        w->last_modified_ns = current_mtime;
        return true;
    }
    return false;
}

#endif // _DEBUG

#endif // SHADER_WATCHER_H
