// HyperShader Standard Library - 3D Particle Systems
// Advanced particle simulation with 3D physics, forces, and rendering

#ifndef _STDLIB_PARTICLES_3D_H_
#define _STDLIB_PARTICLES_3D_H_

// === PARTICLE STRUCTURES ===

struct Particle3D {
    float3 position;
    float3 velocity;
    float3 acceleration;
    float3 rotation;
    float3 angular_velocity;
    float3 color;
    float size;
    float mass;
    float life;
    float max_life;
    float temperature; // For heat-based effects
    int type; // 0=standard, 1=fire, 2=smoke, 3=spark, etc.
    bool active;
};

// Particle system configuration
struct ParticleSystem3D {
    Particle3D particles[256]; // Fixed size for simplicity
    int max_particles;
    int active_count;
    float spawn_rate;
    float time_since_last_spawn;
    float3 spawn_position;
    float3 spawn_velocity;
    float spawn_velocity_variation;
    float particle_lifetime;
    float particle_size;
    float gravity_strength;
    float drag_coefficient;
    float3 wind_force;
    bool looping;
};

// === PHYSICS SIMULATION ===

// Apply gravity force
void applyGravity(inout Particle3D particle, float3 gravity) {
    particle.acceleration += gravity;
}

// Apply drag force
void applyDrag(inout Particle3D particle, float drag_coeff) {
    float3 drag_force = -drag_coeff * length(particle.velocity) * particle.velocity;
    particle.acceleration += drag_force / particle.mass;
}

// Apply wind force
void applyWind(inout Particle3D particle, float3 wind) {
    particle.acceleration += wind / particle.mass;
}

// Turbulence force (noise-based)
void applyTurbulence(inout Particle3D particle, float strength, float scale, float time) {
    float3 noise_pos = particle.position * scale + float3(0.0, time * 0.1, 0.0);
    float3 turbulence = (noise3D(noise_pos) - 0.5) * 2.0 * strength;
    particle.acceleration += turbulence / particle.mass;
}

// Vortex force (spiral motion)
void applyVortex(inout Particle3D particle, float3 center, float strength, float radius) {
    float3 to_center = center - particle.position;
    float dist = length(to_center);

    if (dist > 0.001) {
        float3 tangent = normalize(cross(to_center, float3(0.0, 1.0, 0.0)));
        float force = strength / (dist * dist + radius * radius);
        particle.acceleration += tangent * force / particle.mass;
    }
}

// Attractor force (gravitational pull)
void applyAttractor(inout Particle3D particle, float3 attractor_pos, float strength, float min_dist) {
    float3 to_attractor = attractor_pos - particle.position;
    float dist = length(to_attractor);

    if (dist > min_dist) {
        float3 force = normalize(to_attractor) * strength / (dist * dist);
        particle.acceleration += force / particle.mass;
    }
}

// Repulsor force (anti-gravity)
void applyRepulsor(inout Particle3D particle, float3 repulsor_pos, float strength, float max_dist) {
    float3 from_repulsor = particle.position - repulsor_pos;
    float dist = length(from_repulsor);

    if (dist < max_dist && dist > 0.001) {
        float3 force = normalize(from_repulsor) * strength / (dist * dist);
        particle.acceleration += force / particle.mass;
    }
}

// === COLLISION DETECTION ===

// Ground collision
void checkGroundCollision(inout Particle3D particle, float ground_y, float restitution) {
    if (particle.position.y - particle.size * 0.5 <= ground_y) {
        particle.position.y = ground_y + particle.size * 0.5;
        particle.velocity.y *= -restitution;

        // Add some friction
        particle.velocity.xz *= 0.8;
    }
}

// Sphere collision
void checkSphereCollision(inout Particle3D particle, float3 sphere_center, float sphere_radius, float restitution) {
    float3 to_center = sphere_center - particle.position;
    float dist = length(to_center);

    if (dist < sphere_radius + particle.size * 0.5) {
        float3 normal = normalize(to_center);
        particle.position = sphere_center - normal * (sphere_radius + particle.size * 0.5);
        particle.velocity = reflect(particle.velocity, normal) * restitution;
    }
}

// Box collision
void checkBoxCollision(inout Particle3D particle, float3 box_min, float3 box_max, float restitution) {
    float3 closest = clamp(particle.position, box_min, box_max);
    float3 delta = particle.position - closest;
    float dist = length(delta);

    if (dist < particle.size * 0.5) {
        float3 normal = normalize(delta);
        particle.position = closest + normal * particle.size * 0.5;
        particle.velocity = reflect(particle.velocity, normal) * restitution;
    }
}

// === PARTICLE LIFECYCLE ===

// Initialize particle
void initParticle(inout Particle3D particle, float3 pos, float3 vel, float life, float size, float mass, int type) {
    particle.position = pos;
    particle.velocity = vel;
    particle.acceleration = float3(0.0);
    particle.rotation = float3(0.0);
    particle.angular_velocity = float3(0.0, 0.0, 0.0);
    particle.color = float3(1.0);
    particle.size = size;
    particle.mass = mass;
    particle.life = life;
    particle.max_life = life;
    particle.temperature = 0.0;
    particle.type = type;
    particle.active = true;
}

// Update particle physics
void updateParticle(inout Particle3D particle, float delta_time) {
    if (!particle.active) return;

    // Integrate velocity
    particle.velocity += particle.acceleration * delta_time;
    particle.position += particle.velocity * delta_time;

    // Integrate rotation
    particle.rotation += particle.angular_velocity * delta_time;

    // Reset acceleration
    particle.acceleration = float3(0.0);

    // Update life
    particle.life -= delta_time;
    if (particle.life <= 0.0) {
        particle.active = false;
    }

    // Update temperature (cooling)
    particle.temperature *= 0.99;
}

// Update particle appearance based on type and life
void updateParticleAppearance(inout Particle3D particle) {
    float life_ratio = particle.life / particle.max_life;

    switch (particle.type) {
        case 0: // Standard particle
            particle.color = lerp(float3(0.8, 0.9, 1.0), float3(0.2, 0.3, 0.8), 1.0 - life_ratio);
            particle.size *= (0.5 + life_ratio * 0.5);
            break;

        case 1: // Fire particle
            particle.color = lerp(float3(1.0, 0.3, 0.1), float3(1.0, 0.8, 0.2), life_ratio);
            particle.temperature = lerp(1000.0, 300.0, 1.0 - life_ratio);
            particle.size *= (0.8 + life_ratio * 0.4);
            break;

        case 2: // Smoke particle
            particle.color = lerp(float3(0.1, 0.1, 0.1), float3(0.3, 0.3, 0.3), life_ratio);
            particle.size *= (1.0 + (1.0 - life_ratio) * 2.0);
            break;

        case 3: // Spark particle
            particle.color = float3(1.0, 0.8, 0.3);
            particle.size *= life_ratio;
            break;

        case 4: // Water particle
            particle.color = lerp(float3(0.3, 0.6, 1.0), float3(0.1, 0.3, 0.8), 1.0 - life_ratio);
            break;
    }
}

// === PARTICLE SYSTEM MANAGEMENT ===

// Initialize particle system
void initParticleSystem(inout ParticleSystem3D system, int max_particles, float spawn_rate,
                       float3 spawn_pos, float particle_lifetime, float particle_size) {
    system.max_particles = max_particles;
    system.active_count = 0;
    system.spawn_rate = spawn_rate;
    system.time_since_last_spawn = 0.0;
    system.spawn_position = spawn_pos;
    system.spawn_velocity = float3(0.0, 1.0, 0.0);
    system.spawn_velocity_variation = 0.5;
    system.particle_lifetime = particle_lifetime;
    system.particle_size = particle_size;
    system.gravity_strength = -9.8;
    system.drag_coefficient = 0.1;
    system.wind_force = float3(0.0);
    system.looping = true;

    // Deactivate all particles
    for (int i = 0; i < max_particles; i++) {
        system.particles[i].active = false;
    }
}

// Spawn new particle
void spawnParticle(inout ParticleSystem3D system, int type) {
    if (system.active_count >= system.max_particles) return;

    // Find inactive particle
    for (int i = 0; i < system.max_particles; i++) {
        if (!system.particles[i].active) {
            // Random velocity variation
            float3 vel_variation = (noise3D(float3(i, hs_time, 0)) - 0.5) * 2.0 * system.spawn_velocity_variation;
            float3 velocity = system.spawn_velocity + vel_variation;

            // Random position variation
            float3 pos_variation = (noise3D(float3(i, hs_time, 1)) - 0.5) * 2.0 * 0.1;
            float3 position = system.spawn_position + pos_variation;

            initParticle(system.particles[i], position, velocity,
                        system.particle_lifetime, system.particle_size, 1.0, type);
            system.active_count++;
            break;
        }
    }
}

// Update particle system
void updateParticleSystem(inout ParticleSystem3D system, float delta_time) {
    // Spawn new particles
    system.time_since_last_spawn += delta_time;
    float spawn_interval = 1.0 / system.spawn_rate;

    while (system.time_since_last_spawn >= spawn_interval) {
        spawnParticle(system, 0); // Default type
        system.time_since_last_spawn -= spawn_interval;
    }

    // Update all particles
    system.active_count = 0;
    for (int i = 0; i < system.max_particles; i++) {
        if (system.particles[i].active) {
            // Apply forces
            applyGravity(system.particles[i], float3(0.0, system.gravity_strength, 0.0));
            applyDrag(system.particles[i], system.drag_coefficient);
            applyWind(system.particles[i], system.wind_force);

            // Apply turbulence
            applyTurbulence(system.particles[i], 2.0, 0.1, hs_time);

            // Update physics
            updateParticle(system.particles[i], delta_time);

            // Check collisions
            checkGroundCollision(system.particles[i], -2.0, 0.3);

            // Update appearance
            updateParticleAppearance(system.particles[i]);

            if (system.particles[i].active) {
                system.active_count++;
            }
        }
    }
}

// === RENDERING ===

// Render particle as SDF sphere
float particleSDF(float3 p, Particle3D particle) {
    if (!particle.active) return 1000.0;
    return sdSphere(p - particle.position, particle.size);
}

// Render particle as billboard (for sprites)
float4 renderParticleBillboard(Particle3D particle, float3 view_dir, float3 right, float3 up,
                              sampler2D particle_texture) {
    // Simple billboard rendering
    float2 uv = float2(0.5, 0.5); // Center of particle
    float4 tex_color = texture(particle_texture, uv);

    // Distance-based alpha
    float alpha = tex_color.a * (particle.life / particle.max_life);

    return float4(particle.color * tex_color.rgb, alpha);
}

// === UTILITY FUNCTIONS ===

// Get particle count
int getActiveParticleCount(ParticleSystem3D system) {
    return system.active_count;
}

// Set system properties
void setParticleSystemGravity(inout ParticleSystem3D system, float gravity) {
    system.gravity_strength = gravity;
}

void setParticleSystemWind(inout ParticleSystem3D system, float3 wind) {
    system.wind_force = wind;
}

void setParticleSystemSpawnVelocity(inout ParticleSystem3D system, float3 velocity, float variation) {
    system.spawn_velocity = velocity;
    system.spawn_velocity_variation = variation;
}

// === PRESETS ===

// Fire particle system preset
ParticleSystem3D createFireSystem(float3 position) {
    ParticleSystem3D system;
    initParticleSystem(system, 128, 50.0, position, 2.0, 0.05);
    system.gravity_strength = -1.0; // Reduced gravity for fire
    system.drag_coefficient = 0.05;
    system.spawn_velocity = float3(0.0, 2.0, 0.0);
    system.spawn_velocity_variation = 1.0;
    return system;
}

// Smoke particle system preset
ParticleSystem3D createSmokeSystem(float3 position) {
    ParticleSystem3D system;
    initParticleSystem(system, 64, 20.0, position, 5.0, 0.1);
    system.gravity_strength = -0.5; // Slow rising
    system.drag_coefficient = 0.02;
    system.spawn_velocity = float3(0.0, 0.5, 0.0);
    system.spawn_velocity_variation = 0.2;
    return system;
}

// Waterfall particle system preset
ParticleSystem3D createWaterfallSystem(float3 position) {
    ParticleSystem3D system;
    initParticleSystem(system, 200, 100.0, position, 3.0, 0.02);
    system.gravity_strength = -15.0; // Strong gravity
    system.drag_coefficient = 0.1;
    system.spawn_velocity = float3(0.0, 0.0, 0.0);
    system.spawn_velocity_variation = 0.5;
    return system;
}

// Explosion particle system preset
ParticleSystem3D createExplosionSystem(float3 position) {
    ParticleSystem3D system;
    initParticleSystem(system, 100, 1000.0, position, 1.5, 0.03);
    system.gravity_strength = -3.0;
    system.drag_coefficient = 0.05;
    system.spawn_velocity = float3(0.0, 0.0, 0.0);
    system.spawn_velocity_variation = 5.0; // High variation for explosion
    return system;
}

#endif