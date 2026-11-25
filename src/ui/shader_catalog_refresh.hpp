#pragma once
#include <obs.h>
#include <cstring>
#include <vector>

// Collect strong refs while enumerating, then notify outside the source-list
// enumeration. Rebuilding an OBS properties panel may itself enumerate sources.
inline void refreshHyperShaderProperties()
{
    std::vector<obs_source_t *> sources;
    obs_enum_all_sources([](void *param, obs_source_t *source) {
        const char *id = obs_source_get_unversioned_id(source);
        if (id && (std::strcmp(id,"hypershader_filter")==0 ||
                   std::strcmp(id,"hypershader_source")==0 ||
                   std::strcmp(id,"hypershader_transition")==0)) {
            if (auto *ref = obs_source_get_ref(source))
                static_cast<std::vector<obs_source_t *> *>(param)->push_back(ref);
        }
        return true;
    }, &sources);
    for (auto *source : sources) {
        obs_source_update_properties(source);
        obs_source_release(source);
    }
}
