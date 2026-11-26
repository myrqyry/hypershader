// hypershader Standard Library - 3D Voxel Utilities
// Advanced 3D voxel operations and physics

#ifndef _STDLIB_VOXEL_3D_H_
#define _STDLIB_VOXEL_3D_H_

// === VOXEL STRUCTURES ===

struct Voxel3D {
    vec3 position;
    vec3 velocity;
    vec3 rotation;
    vec3 angular_velocity;
    vec3 color;
    float mass;
    float size;
    float start_time;
    bool active;
    int material_type; // 0=dielectric, 1=metallic, 2=glass, 3=emissive
};

// === PHYSICS SIMULATION ===

// Gravity force
vec3 voxelGravityForce(float mass, vec3 gravity_dir) {
    return mass * gravity_dir;
}

// Air resistance (drag)
vec3 voxelDragForce(vec3 velocity, float drag_coefficient) {
    return -drag_coefficient * velocity * length(velocity);
}

// Collision response (simple bounce)
void voxelBounce(inout vec3 position, inout vec3 velocity, vec3 normal, float restitution) {
    float vel_dot_normal = dot(velocity, normal);
    if (vel_dot_normal < 0.0) {
        velocity = velocity - (1.0 + restitution) * vel_dot_normal * normal;
        // Prevent sinking
        position += normal * (0.001 - dot(position, normal));
    }
}

// Angular physics
vec3 voxelTorque(vec3 force, vec3 contact_point, vec3 center_of_mass) {
    vec3 r = contact_point - center_of_mass;
    return cross(r, force);
}

// === VOXEL RENDERING ===

// Voxel SDF (signed distance field)
float voxelSDF(vec3 p, Voxel3D voxel) {
    if (!voxel.active) return 1000.0;

    // Transform to voxel local space
    vec3 local_p = p - voxel.position;

    // Apply rotation
    float cx = cos(voxel.rotation.x), sx = sin(voxel.rotation.x);
    float cy = cos(voxel.rotation.y), sy = sin(voxel.rotation.y);
    float cz = cos(voxel.rotation.z), sz = sin(voxel.rotation.z);

    mat3 rotX = mat3(1,0,0, 0,cx,-sx, 0,sx,cx);
    mat3 rotY = mat3(cy,0,sy, 0,1,0, -sy,0,cy);
    mat3 rotZ = mat3(cz,-sz,0, sz,cz,0, 0,0,1);

    local_p = rotX * rotY * rotZ * local_p;

    // Voxel shape (box)
    return sdBox(local_p, vec3(voxel.size));
}

// Voxel normal calculation
vec3 voxelNormal(vec3 p, Voxel3D voxel) {
    float eps = 0.001;
    vec3 n = vec3(
        voxelSDF(p + vec3(eps, 0.0, 0.0), voxel) - voxelSDF(p - vec3(eps, 0.0, 0.0), voxel),
        voxelSDF(p + vec3(0.0, eps, 0.0), voxel) - voxelSDF(p - vec3(0.0, eps, 0.0), voxel),
        voxelSDF(p + vec3(0.0, 0.0, eps), voxel) - voxelSDF(p - vec3(0.0, 0.0, eps), voxel)
    );
    return normalize(n);
}

// === MATERIAL SHADING ===

// PBR shading for voxels
vec3 voxelPBRShading(Voxel3D voxel, vec3 normal, vec3 view_dir, vec3 light_dir, vec3 light_color) {
    vec3 albedo = voxel.color;
    float metallic = (voxel.material_type == 1) ? 0.8 : 0.0;
    float roughness = 0.3 + 0.4 * hash13(voxel.position); // Vary roughness per voxel

    // Half vector
    vec3 half_dir = normalize(view_dir + light_dir);

    // Dot products
    float NdotL = dotClamped(normal, light_dir);
    float NdotV = dotClamped(normal, view_dir);
    float NdotH = dotClamped(normal, half_dir);
    float LdotH = dotClamped(light_dir, half_dir);

    // F0
    vec3 F0 = computeF0(albedo, metallic);

    // BRDF
    vec3 specular = CookTorrance(NdotH, NdotV, NdotL, LdotH, roughness, F0);
    vec3 diffuse = diffuseEnergyConservation(albedo, F0, metallic) / PI;

    // Ambient
    vec3 ambient = albedo * 0.1;

    // Combine
    vec3 color = (diffuse + specular) * light_color * NdotL + ambient;

    // Material-specific effects
    if (voxel.material_type == 2) { // Glass
        color += fresnel(view_dir, normal, 2.0) * vec3(0.8, 0.9, 1.0) * 0.5;
    } else if (voxel.material_type == 3) { // Emissive
        color += albedo * 2.0;
    }

    return color;
}

// === VOXEL GENERATION ===

// Create voxel from texture sample
Voxel3D createVoxelFromTexture(vec2 uv, vec2 grid_pos, vec2 grid_res, float time, float progress,
                               float gravity, float random_delay, float rotation_amount) {
    Voxel3D voxel;
    voxel.active = false;

    // Sample texture
    vec4 tex_color = texture(hs_input0, (grid_pos + 0.5) / grid_res);
    if (tex_color.a < 0.1) return voxel;

    // Random seed for this voxel
    float seed = hash12(grid_pos);

    // Timing
    float start_threshold = (1.0 - uv.y) * (1.0 - random_delay) + (seed * random_delay);
    float local_t = clamp((progress - start_threshold * 0.5) * 2.0, 0.0, 1.0);

    if (local_t <= 0.0) return voxel;

    // Physics
    float drop_offset = 0.5 * gravity * local_t * local_t;

    // Position (convert grid to 3D world space)
    voxel.position = vec3(
        (grid_pos.x - grid_res.x * 0.5) / grid_res.x * 4.0,
        2.0 - drop_offset,
        (grid_pos.y - grid_res.y * 0.5) / grid_res.y * 4.0
    );

    // Velocity (for physics simulation)
    voxel.velocity = vec3(0.0, -gravity * local_t, 0.0);

    // Rotation
    voxel.rotation = vec3(
        local_t * rotation_amount * (seed - 0.5) * 12.0,
        local_t * rotation_amount * (hash12(grid_pos + 1.0) - 0.5) * 8.0,
        local_t * rotation_amount * (hash12(grid_pos + 2.0) - 0.5) * 6.0
    );

    // Angular velocity
    voxel.angular_velocity = vec3(
        rotation_amount * (seed - 0.5) * 6.0,
        rotation_amount * (hash12(grid_pos + 1.0) - 0.5) * 4.0,
        rotation_amount * (hash12(grid_pos + 2.0) - 0.5) * 3.0
    );

    // Properties
    voxel.color = tex_color.rgb;
    voxel.mass = 0.5 + seed * 0.5;
    voxel.size = 0.4;
    voxel.start_time = start_threshold;
    voxel.material_type = int(seed * 4.0); // Random material type
    voxel.active = true;

    return voxel;
}

// === COLLISION DETECTION ===

// Voxel-voxel collision (simplified)
bool voxelCollision(Voxel3D a, Voxel3D b) {
    if (!a.active || !b.active) return false;

    vec3 delta = a.position - b.position;
    float min_dist = a.size + b.size;

    return length(delta) < min_dist;
}

// Voxel-ground collision
bool voxelGroundCollision(Voxel3D voxel, float ground_y) {
    return voxel.position.y - voxel.size <= ground_y;
}

// === UTILITY FUNCTIONS ===

// Convert screen UV to world ray
void screenToWorldRay(vec2 uv, mat3 camera_matrix, float focal_length,
                      out vec3 ray_origin, out vec3 ray_direction) {
    vec2 p = (uv - 0.5) * 2.0;
    ray_direction = normalize(camera_matrix * vec3(p, -focal_length));
    ray_origin = camera_matrix[2] * (-3.0); // Camera position from matrix
}

// Camera matrix from angles
mat3 cameraMatrix(float angle_x, float angle_y, float distance) {
    vec3 eye = vec3(
        sin(angle_x) * cos(angle_y) * distance,
        sin(angle_y) * distance,
        cos(angle_x) * cos(angle_y) * distance
    );

    vec3 target = vec3(0.0, 0.0, 0.0);
    vec3 up = vec3(0.0, 1.0, 0.0);

    vec3 forward = normalize(target - eye);
    vec3 right = normalize(cross(forward, up));
    vec3 new_up = cross(right, forward);

    return mat3(right, new_up, -forward);
}

#endif