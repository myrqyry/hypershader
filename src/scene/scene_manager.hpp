#pragma once
#include <entt/entt.hpp>
#include "../mediapipe/mp_wrapper.h"
#include <obs-module.h>
#include <vector>
#include <memory>
#include <random>
#include <cmath>

namespace hs {

// ===================================
// Components
// ===================================

struct Transform {
	float x = 0.5f; // Normalized 0-1
	float y = 0.5f;
	float scale = 0.1f;
	float rotation = 0.0f;
	int type = 0; // 0=None, 1=Circle, 2=Sprite...
};

struct Physics {
	float vx = 0.0f;
	float vy = 0.0f;
	float gravity = 0.0f;
	float elasticity = 0.9f;
};

struct FaceAnchor {
	int landmark_index = 0; // e.g. 4 (Tip of nose)
	float offset_x = 0.0f;
	float offset_y = 0.0f;
};

// ===================================
// Scene Manager
// ===================================

class SceneManager {
public:
	entt::registry registry;
	bool initialized = false;

	SceneManager() { init_demo(); }

	void init_demo()
	{
		if (initialized)
			return;

		// Spawn bouncing balls demo
		std::mt19937 rng(1234);
		std::uniform_real_distribution<float> dist(0.1f, 0.9f);
		std::uniform_real_distribution<float> vel(-0.005f, 0.005f);

		for (int i = 0; i < 10; ++i) {
			auto entity = registry.create();
			registry.emplace<Transform>(entity, dist(rng), dist(rng), 0.03f, 0.0f, 1);
			registry.emplace<Physics>(entity, vel(rng), vel(rng), 0.0002f, 0.98f);
		}

		// Face Anchor Demo (Nose) - Entity Type 2 (Star?)
		auto nose = registry.create();
		registry.emplace<Transform>(nose, 0.5f, 0.5f, 0.06f, 0.0f, 2);
		registry.emplace<FaceAnchor>(nose, 4, 0.0f, 0.0f);

		initialized = true;
	}

	void update(float dt_ms, mp_runner_t *mp_runner)
	{
		sync_mediapipe(mp_runner);
		update_physics(dt_ms);
	}

	void update_physics(float dt_ms)
	{
		// Simple Euler integration
		// dt is mostly constant 16.6ms in OBS, but we can normalize
		float dt = dt_ms / 16.666f;

		auto view = registry.view<Transform, Physics>();
		view.each([dt](auto /*entity*/, Transform &t, Physics &p) {
			p.vy += p.gravity * dt;
			t.x += p.vx * dt;
			t.y += p.vy * dt;

			// Screen Bounds (0-1)
			if (t.y > 1.0f) {
				t.y = 1.0f;
				p.vy *= -p.elasticity;
			}
			if (t.y < 0.0f) {
				t.y = 0.0f;
				p.vy *= -p.elasticity;
			}
			if (t.x > 1.0f) {
				t.x = 1.0f;
				p.vx *= -p.elasticity;
			}
			if (t.x < 0.0f) {
				t.x = 0.0f;
				p.vx *= -p.elasticity;
			}
		});
	}

	void sync_mediapipe(mp_runner_t *runner)
	{
		if (!runner)
			return;

		mp_landmark_list_t landmarks = {0};
		// Use non-blocking get
		if (mp_get_landmarks(runner, &landmarks) && landmarks.count > 0) {

			auto view = registry.view<Transform, FaceAnchor>();
			view.each([&landmarks](auto /*entity*/, Transform &t, const FaceAnchor &anchor) {
				if (anchor.landmark_index < landmarks.count) {
					float lx = landmarks.landmarks[anchor.landmark_index].x;
					float ly = landmarks.landmarks[anchor.landmark_index].y;

					t.x = lx + anchor.offset_x;
					t.y = ly + anchor.offset_y;
				}
			});

			bfree(landmarks.landmarks);

			// Interaction: Collide Physics objects with Face objects
			auto ph_view = registry.view<Transform, Physics>();
			auto fa_view = registry.view<Transform, FaceAnchor>();

			ph_view.each([&fa_view](auto /*p_ent*/, Transform &pt, Physics &pp) {
				fa_view.each([&pt, &pp](auto /*f_ent*/, Transform &ft, FaceAnchor & /*fa*/) {
					float dx = pt.x - ft.x;
					float dy = pt.y - ft.y;
					float dist2 = dx * dx + dy * dy;
					float min_dist = (pt.scale + ft.scale) * 0.5f;

					if (dist2 < min_dist * min_dist && dist2 > 0.00001f) {
						float dist = std::sqrt(dist2);
						float nx = dx / dist;
						float ny = dy / dist;

						// Push out
						float overlap = min_dist - dist;
						pt.x += nx * overlap;
						pt.y += ny * overlap;

						// Bounce
						float dot = pp.vx * nx + pp.vy * ny;
						pp.vx -= 2 * dot * nx;
						pp.vy -= 2 * dot * ny;

						// Kick
						pp.vx += nx * 0.005f;
						pp.vy += ny * 0.005f;
					}
				});
			});
		}
	}

	// Serialization for Shader Uniforms
	// Output format: [x,y,scale,type, x,y,scale,type...]
	int get_render_data(float *output, int max_entities)
	{
		int count = 0;
		auto view = registry.view<Transform>();

		// 4 floats per entity
		int stride = 4;

		for (auto entity : view) {
			if (count >= max_entities)
				break;
			const auto &t = view.get<Transform>(entity);

			int idx = count * stride;
			output[idx + 0] = t.x;
			output[idx + 1] = t.y;
			output[idx + 2] = t.scale;
			output[idx + 3] = (float)t.type;

			count++;
		}

		// Zero out remaining
		for (int i = count; i < max_entities; ++i) {
			int idx = i * stride;
			output[idx + 0] = -1.0f; // Hidden
			output[idx + 1] = -1.0f;
			output[idx + 2] = 0.0f;
			output[idx + 3] = 0.0f;
		}

		return count;
	}
};

} // namespace hs
