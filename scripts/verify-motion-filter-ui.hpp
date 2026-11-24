#pragma once
// Real libobs filter-property contract. Intentionally independent of shader metadata
// to catch controls hidden, renamed, misgrouped, wrongly typed, or defaulting to zero.
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

struct MotionUiSpec {
    const char *name;
    const char *label;
    const char *group;
    enum obs_property_type type;
    double min, max, step, default_value;
};
static bool verify_motion_filter_ui(obs_source_t *filter, const std::string &path)
{
    const bool mosh = path.find("filter.motion_mosh.effect") != std::string::npos;
    const bool isolate = path.find("filter.motion_isolate.effect") != std::string::npos;
    if (!mosh && !isolate)
        return true;
    const std::vector<MotionUiSpec> expected = mosh ? std::vector<MotionUiSpec>{
        {"search_radius", "Motion Search (px)", "Prediction", OBS_PROPERTY_FLOAT, 0, 24, 1, 9},
        {"block_size", "Block Size (px)", "Prediction", OBS_PROPERTY_FLOAT, 8, 48, 4, 16},
        {"motion_carry", "Motion Carry", "Prediction", OBS_PROPERTY_FLOAT, 0, 3, .05, 1.45},
        {"residual_loss", "Missing Residual", "Damage", OBS_PROPERTY_FLOAT, 0, 1, .01, .88},
        {"color_hold", "Chroma Persistence", "Damage", OBS_PROPERTY_FLOAT, 0, 1, .01, .70},
        {"block_damage", "Block Loss Variation", "Damage", OBS_PROPERTY_FLOAT, 0, 1, .01, .58},
        {"mismatch_heal", "Poor Match Recovery", "Recovery", OBS_PROPERTY_FLOAT, 0, 1, .01, .22},
        {"clean_every", "Clean Every N Frames (0=off)", "Recovery", OBS_PROPERTY_INT, 0, 240, 1, 0},
        {"force_clean", "Force Clean Keyframe", "Recovery", OBS_PROPERTY_BOOL, 0, 1, 1, 0},
        {"show_vectors", "Show Motion Vectors", "Debug", OBS_PROPERTY_BOOL, 0, 1, 1, 0},
        {"vector_brightness", "Vector Brightness", "Debug", OBS_PROPERTY_FLOAT, 0, 3, .05, 1.0},
        {"effect_mix", "Effect Mix", "Output", OBS_PROPERTY_FLOAT, 0, 1, .01, .90}
    } : std::vector<MotionUiSpec>{
        {"background_hold", "Background Memory", "Motion", OBS_PROPERTY_FLOAT, 0, .995, .005, .965},
        {"motion_threshold", "Motion Threshold", "Motion", OBS_PROPERTY_FLOAT, .01, .9, .01, .16},
        {"edge_feather", "Edge Feather", "Motion", OBS_PROPERTY_FLOAT, 0, .4, .01, .12},
        {"mask_retention", "Trail Retention", "Motion", OBS_PROPERTY_FLOAT, 0, .995, .005, .83},
        {"background_mute", "Still Area Desaturation", "Look", OBS_PROPERTY_FLOAT, 0, 1, .01, .92},
        {"accent_strength", "Motion Boundary", "Look", OBS_PROPERTY_FLOAT, 0, 1, .01, .25},
        {"show_mask", "View Motion Mask", "Debug", OBS_PROPERTY_BOOL, 0, 1, 1, 0},
        {"effect_mix", "Effect Mix", "Output", OBS_PROPERTY_FLOAT, 0, 1, .01, .86}
    };
    bool good = true;
    obs_properties_t *props = obs_source_properties(filter);
    obs_data_t *settings = obs_source_get_settings(filter);
    obs_property_t *outer = props ? obs_properties_get(props, "shader_params_group") : nullptr;
    obs_properties_t *groups = outer && obs_property_get_type(outer) == OBS_PROPERTY_GROUP
        ? obs_property_group_content(outer) : nullptr;
    if (!groups) {
        std::cerr << "MOTION_UI no shader_params_group for " << path << "\n";
        good = false;
    }
    size_t present = 0;
    if (groups) {
        obs_property_t *group = obs_properties_first(groups);
        while (group) {
            if (obs_property_get_type(group) == OBS_PROPERTY_GROUP) {
                obs_properties_t *children = obs_property_group_content(group);
                obs_property_t *child = children ? obs_properties_first(children) : nullptr;
                while (child) {
                    if (std::strncmp(obs_property_name(child), "param_", 6) == 0)
                        present++;
                    obs_property_next(&child);
                }
            } else if (std::strncmp(obs_property_name(group), "param_", 6) == 0) {
                present++;
            }
            obs_property_next(&group);
        }
    }
    if (present != expected.size()) {
        std::cerr << "MOTION_UI expected " << expected.size() << " visible controls, found "
                  << present << "\n";
        good = false;
    }
    for (const auto &spec : expected) {
        const std::string group_name = std::string("group_") + spec.group;
        const std::string name = std::string("param_") + spec.name;
        obs_property_t *group = groups ? obs_properties_get(groups, group_name.c_str()) : nullptr;
        obs_properties_t *group_contents = group && obs_property_get_type(group) == OBS_PROPERTY_GROUP
            ? obs_property_group_content(group) : nullptr;
        obs_property_t *p = group_contents ? obs_properties_get(group_contents, name.c_str()) : nullptr;
        if (!p) {
            std::cerr << "MOTION_UI MISSING " << group_name << '/' << name << "\n";
            good = false;
            continue;
        }
        if (!obs_property_visible(p) || std::strcmp(obs_property_description(p), spec.label) ||
            obs_property_get_type(p) != spec.type) {
            std::cerr << "MOTION_UI wrong visibility/label/type " << name << " type="
                      << obs_property_get_type(p) << " expected=" << spec.type << "\n";
            good = false;
        }
        if (spec.type == OBS_PROPERTY_FLOAT) {
            if (std::abs(obs_property_float_min(p) - spec.min) > .0001 ||
                std::abs(obs_property_float_max(p) - spec.max) > .0001 ||
                std::abs(obs_property_float_step(p) - spec.step) > .0001)
                good = false;
        } else if (spec.type == OBS_PROPERTY_INT) {
            if (obs_property_int_min(p) != int(spec.min) ||
                obs_property_int_max(p) != int(spec.max) ||
                obs_property_int_step(p) != int(spec.step))
                good = false;
        }
        double actual = spec.type == OBS_PROPERTY_BOOL ? double(obs_data_get_bool(settings, name.c_str())) :
                        spec.type == OBS_PROPERTY_INT ? double(obs_data_get_int(settings, name.c_str())) :
                        obs_data_get_double(settings, name.c_str());
        if (std::abs(actual - spec.default_value) > .0001) {
            std::cerr << "MOTION_UI incorrect default " << name << " got=" << actual
                      << " wanted=" << spec.default_value << "\n";
            good = false;
        }
    }
    if (settings)
        obs_data_release(settings);
    if (props)
        obs_properties_destroy(props);
    std::cout << "MOTION_UI_SETTINGS=" << path << " expected=" << expected.size()
              << " present=" << present << " PASS=" << good << "\n";
    return good;
}
