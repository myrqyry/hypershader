#ifndef SCENE_BRIDGE_H
#define SCENE_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

// Opaque pointer to SceneManager
typedef void* scene_handle_t;

// Creates the EnTT scene (Bouncing Balls + Face Anchor Demo)
scene_handle_t scene_create();

// Destroys the scene
void scene_destroy(scene_handle_t scene);

// Updates logic: Syncs with MediaPipe runner (if available) then runs Physics step
// dt: delta time in milliseconds
void scene_update(scene_handle_t scene, void* mp_runner, float dt);

// Fills a flat float array with entity data for the Shader
// Format: [x, y, scale, type] per entity
// Returns: number of active entities written
int scene_get_entities(scene_handle_t scene, float* out_array, int max_entities);

#ifdef __cplusplus
}
#endif

#endif // SCENE_BRIDGE_H
