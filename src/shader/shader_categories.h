#ifndef HYPERSHADER_SHADER_CATEGORIES_H
#define HYPERSHADER_SHADER_CATEGORIES_H

/* Presentation-only navigation for OBS's two linked drop-down properties.
 * The actual shader is identified by its unchanged preset integer and path. */
#include "shader_display_name.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static inline const char *hs_shader_category_role(const char *label)
{
    if (!label)
        return NULL;
    if (strncmp(label, "Source / ", 9) == 0) return "source";
    if (strncmp(label, "Filter / ", 9) == 0) return "filter";
    if (strncmp(label, "Transition / ", 13) == 0) return "transition";
    return NULL;
}

/* The installed catalog is one collection deep (root/<folder>/<file>). */
static inline bool hs_shader_collection(const char *id, char *out, size_t cap)
{
    if (!out || cap == 0) return false;
    out[0] = '\0';
    if (!id) return false;
    const char *root = strstr(id, "root/");
    if (!root) return false;
    const char *begin = root + 5;
    const char *end = strchr(begin, '/');
    if (!end || end == begin || (size_t)(end - begin) >= cap)
        return false;
    const size_t n = (size_t)(end - begin);
    memcpy(out, begin, n);
    out[n] = '\0';
    /* Source, filter, transition directories classify by role, not collection. */
    return strcmp(out, "source") != 0 && strcmp(out, "filter") != 0 &&
           strcmp(out, "transition") != 0;
}

static inline bool hs_shader_category_for(const char *id, const char *label,
                                          bool user, char *out, size_t cap)
{
    if (!out || cap < 24) return false;
    char collection[128];
    if (user)
        return snprintf(out, cap, "user") > 0;
    if (hs_shader_collection(id, collection, sizeof(collection)))
        return snprintf(out, cap, "collection:%s", collection) < (int)cap;
    const char *role = hs_shader_category_role(label);
    return role && snprintf(out, cap, "%s", role) < (int)cap;
}

static inline bool hs_shader_category_matches(const char *category,
                                               const char *id,
                                               const char *label,
                                               bool user)
{
    if (!category || !*category || strcmp(category, "all") == 0)
        return true;
    if (strcmp(category, "user") == 0)
        return user;
    if (user)
        return false;
    if (strncmp(category, "collection:", 11) == 0) {
        char collection[128];
        return hs_shader_collection(id, collection, sizeof(collection)) &&
               strcmp(category + 11, collection) == 0;
    }
    const char *role = hs_shader_category_role(label);
    return role && strcmp(category, role) == 0;
}

static inline bool hs_shader_category_display_name(const char *category,
                                                   char *out, size_t cap)
{
    if (!category || !out || cap < 4) return false;
    const char *name = NULL;
    if (strcmp(category, "all") == 0) name = "All Shaders";
    else if (strcmp(category, "source") == 0) name = "Sources";
    else if (strcmp(category, "filter") == 0) name = "Filters";
    else if (strcmp(category, "transition") == 0) name = "Transitions";
    else if (strcmp(category, "user") == 0) name = "My Presets";
    else if (strcmp(category, "collection:shabon") == 0) name = "Shabon FX";
    else if (strcmp(category, "collection:rikiya") == 0) name = "Rikiya";
    if (name)
        return snprintf(out, cap, "%s", name) < (int)cap;
    if (strncmp(category, "collection:", 11) != 0 ||
        category[11] == '\0')
        return false;
    out[0] = '\0';
    size_t used = 0;
    return hs_shader_append_title(out, cap, &used, category + 11,
                                  strlen(category + 11));
}

/* Remove only redundant role/collection prefixes from filtered menus.
 * In All Shaders the role stays visible to disambiguate entries. */
static inline bool hs_shader_category_item_label(const char *category,
                                                  const char *full,
                                                  char *out, size_t cap)
{
    if (!full || !out || !cap) return false;
    const char *title = full;
    const char *first = strstr(full, " / ");
    if (category && strcmp(category, "all") != 0 && first) {
        if (strncmp(category, "collection:", 11) == 0) {
            const char *second = strstr(first + 3, " / ");
            if (second)
                title = second + 3;
        } else if (strcmp(category, "user") != 0 &&
                   strcmp(category, "source") == 0 &&
                   strncmp(full, "Source / ", 9) == 0)
            title = first + 3;
        else if (strcmp(category, "filter") == 0 &&
                 strncmp(full, "Filter / ", 9) == 0)
            title = first + 3;
        else if (strcmp(category, "transition") == 0 &&
                 strncmp(full, "Transition / ", 13) == 0)
            title = first + 3;
    }
    return snprintf(out, cap, "%s", title) < (int)cap;
}

#endif
