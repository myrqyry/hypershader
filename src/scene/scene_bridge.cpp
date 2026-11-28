#include "scene_manager.hpp"

extern "C" {

void* scene_create() {
    try {
        return new hs::SceneManager();
    } catch (...) {
        return nullptr;
    }
}

void scene_destroy(void* scene_ptr) {
    if (scene_ptr) {
        delete static_cast<hs::SceneManager*>(scene_ptr);
    }
}

void scene_update(void* scene_ptr, void* mp_runner, float dt_ms) {
    if (scene_ptr) {
        auto* mgr = static_cast<hs::SceneManager*>(scene_ptr);
        mgr->update(dt_ms, static_cast<mp_runner_t*>(mp_runner));
    }
}

int scene_get_entities(void* scene_ptr, float* out_array, int max_entities) {
    if (scene_ptr) {
        auto* mgr = static_cast<hs::SceneManager*>(scene_ptr);
        return mgr->get_render_data(out_array, max_entities);
    }
    return 0;
}

} // extern C
