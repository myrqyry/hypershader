#ifndef HYPERSHADER_DISPLAY_NAME_H
#define HYPERSHADER_DISPLAY_NAME_H

/* Role-prefixed display names only. Do not rewrite file paths or preset IDs.
 *
 * root/plasma.effect                   -> source.plasma
 * root/source.ambient.effect           -> source.ambient
 * root/transition/wipe.effect          -> transition.wipe
 * root/rikiya/rikiya_202401.effect     -> filter.rikiya.202401
 *
 * Filenames with explicit "source.", "filter.", or "transition." prefixes
 * advertise their intended OBS role. Existing unmarked files are filters,
 * except a reviewed set of built-in procedural sources below. The label
 * does not change where/how a shader is stored or load it into the wrong
 * plugin execution lane.
 */
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Humanize identifiers in one place for native OBS and the Qt manager. */
static inline const char *hs_shader_title_alias(const char *word, size_t length)
{
    static const struct { const char *key; const char *name; } aliases[] = {
        {"3d","3D"}, {"ps1","PS1"}, {"crt","CRT"}, {"rgb","RGB"},
        {"isf","ISF"}, {"siren","SIREN"}, {"ar","AR"}, {"obs","OBS"},
        {"sdf","SDF"}, {"dof","DOF"}, {"fft","FFT"}, {"pbr","PBR"},
        {"ui","UI"}, {"pframe","P-Frame"}, {"lostjared","LostJared"},
        {"noisemaker","NoiseMaker"}, {"liltoon","lilToon"}
    };
    for (size_t i = 0; i < sizeof(aliases)/sizeof(aliases[0]); ++i) {
        if (strlen(aliases[i].key) != length)
            continue;
        bool match = true;
        for (size_t j = 0; j < length; ++j)
            if (tolower((unsigned char)word[j]) !=
                (unsigned char)aliases[i].key[j])
                match = false;
        if (match)
            return aliases[i].name;
    }
    return NULL;
}

static inline bool hs_shader_append(char *out, size_t cap, size_t *used,
                                    const char *text, size_t length)
{
    if (length >= cap || *used >= cap - length)
        return false;
    memcpy(out + *used, text, length);
    *used += length;
    out[*used] = '\0';
    return true;
}

static inline bool hs_shader_append_title(char *out, size_t cap, size_t *used,
                                          const char *raw, size_t length)
{
    bool new_word = true;
    for (size_t i = 0; i < length;) {
        const unsigned char c = (unsigned char)raw[i];
        if (c == '_' || c == '-' || c == '.' || c == ' ') {
            new_word = true;
            i++;
            continue;
        }
        if (new_word) {
            if (*used && out[*used - 1] != ' ' && out[*used - 1] != '/') {
                if (!hs_shader_append(out, cap, used, " ", 1))
                    return false;
            }
            size_t end = i;
            while (end < length && raw[end] != '_' && raw[end] != '-' &&
                   raw[end] != '.' && raw[end] != ' ')
                ++end;
            const char *alias = hs_shader_title_alias(raw + i, end - i);
            if (alias) {
                if (!hs_shader_append(out, cap, used, alias, strlen(alias)))
                    return false;
                i = end;
            } else {
                const char initial = (char)toupper(c);
                if (!hs_shader_append(out, cap, used, &initial, 1))
                    return false;
                ++i;
            }
            new_word = false;
        } else {
            const char lower = (char)tolower(c);
            if (!hs_shader_append(out, cap, used, &lower, 1))
                return false;
            ++i;
        }
    }
    return true;
}

static inline bool hs_shader_display_label(char *out, size_t capacity, const char *relative)
{
    if (!out || !capacity)
        return false;
    out[0] = '\0';
    if (!relative || !*relative)
        return false;

    const char *name = strrchr(relative, '/');
    name = name ? name + 1 : relative;
    const char *backslash = strrchr(name, '\\');
    if (backslash)
        name = backslash + 1;

    const size_t length = strlen(name);
    if (length <= 7 || strcmp(name + length - 7, ".effect") != 0)
        return false;

    const char *role = "filter";
    const char *stem = name;
    const char *roles[] = {"source", "filter", "transition"};
    for (size_t i = 0; i < 3; i++) {
        const size_t n = strlen(roles[i]);
        if (strncmp(stem, roles[i], n) == 0 && stem[n] == '.' &&
            stem[n+1] != '\0' && strcmp(stem+n+1, "effect") != 0) {
            role = roles[i];
            stem += n + 1;
            break;
        }
    }

    /* A collection directory can also declare the intended OBS role. */
    char collection[128] = {0};
    const char *root = strstr(relative, "root/");
    if (root) {
        const char *child = root + strlen("root/");
        const char *slash = strchr(child, '/');
        if (slash && slash > child) {
            const size_t n = (size_t)(slash - child);
            if (n >= sizeof(collection))
                return false;
            memcpy(collection, child, n);
            collection[n] = '\0';
            for (size_t i = 0; i < 3; i++) {
                if (strcmp(collection, roles[i]) == 0) {
                    role = roles[i];
                    collection[0] = '\0';
                    break;
                }
            }
        }
    }

    if (strcmp(collection, "rikiya") == 0 &&
        strncmp(stem, "rikiya_", strlen("rikiya_")) == 0)
        stem += strlen("rikiya_");

    /* Reviewed procedural generators belong in the Source family even when
     * their historical filenames lack a source. prefix. This is a catalog
     * label ONLY; never rename the file or change its persisted preset ID.
     * Explicit roles and subcollection overrides always take precedence. */
    if (strcmp(role, "filter") == 0 && !collection[0] &&
        stem == name) {
        static const char *sources[] = {
            "3d_mesh_gallery.effect", "3d_showcase.effect",
            "3d_ultimate_showcase.effect", "audio_demo.effect",
            "audio_pulse.effect", "blurred_gradient.effect",
            "bubble_metaballs.effect", "bubble_physics.effect",
            "dog_siren_anim.effect", "liquid_grid_flow.effect",
            "magnetic_balls.effect", "numbers_tunnel.effect",
            "orphic_turing_bloom.effect", "plasma.effect",
            "raytracing_demo.effect", "reaction_diffusion.effect",
            "spectrum_bars.effect"
        };
        for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
            if (strcmp(stem, sources[i]) == 0) {
                role = "source";
                break;
            }
        }
    }

    const size_t stem_length = strlen(stem);
    if (stem_length <= 7 || strcmp(stem + stem_length - 7, ".effect") != 0)
        return false;
    size_t used = 0;
    char title_role[16];
    snprintf(title_role, sizeof(title_role), "%s", role);
    title_role[0] = (char)toupper((unsigned char)title_role[0]);
    if (!hs_shader_append(out, capacity, &used, title_role, strlen(title_role)) ||
        !hs_shader_append(out, capacity, &used, " / ", 3))
        goto truncated;
    if (collection[0]) {
        if (!hs_shader_append_title(out, capacity, &used, collection, strlen(collection)) ||
            !hs_shader_append(out, capacity, &used, " / ", 3))
            goto truncated;
    }
    if (!hs_shader_append_title(out, capacity, &used, stem, stem_length - 7))
        goto truncated;
    return true;
truncated:
    out[capacity - 1] = '\0';
    return false;
}

/* Use an authored effect title only in the file header. Parameter @label
 * annotations are not effect titles; in particular some start before the
 * first uniform declaration. Do not change the preset's path or numeric ID. */
static inline bool hs_shader_display_label_from_file(char *out, size_t capacity,
                                                      const char *relative,
                                                      const char *shader_file)
{
    if (!hs_shader_display_label(out, capacity, relative))
        return false;
    if (!shader_file || !*shader_file)
        return true;
    FILE *fp = fopen(shader_file, "rb");
    if (!fp)
        return true;
    char line[1024];
    char authored[256] = {0};
    for (int count = 0; count < 48 && fgets(line, sizeof(line), fp); ++count) {
        char *cursor = line;
        while (*cursor == ' ' || *cursor == '\t')
            ++cursor;
        if (*cursor == '\0' || *cursor == '\n' || *cursor == '\r')
            continue;
        if (cursor[0] != '/' || cursor[1] != '/') {
            if (cursor[0] == '#')
                continue;
            break;
        }
        cursor += 2;
        while (*cursor == ' ' || *cursor == '\t')
            ++cursor;
        if (strncmp(cursor, "@label", 6) != 0 ||
            (cursor[6] != ' ' && cursor[6] != '\t'))
            continue;
        cursor += 6;
        while (*cursor == ' ' || *cursor == '\t')
            ++cursor;
        if (*cursor++ != '"')
            continue;
        const char *end = strchr(cursor, '"');
        if (!end || end == cursor || (size_t)(end - cursor) >= sizeof(authored))
            continue;
        if (strstr(end + 1, "@default") || strstr(end + 1, "@min") ||
            strstr(end + 1, "@max") || strstr(end + 1, "@group") ||
            strstr(end + 1, "@step") || strstr(end + 1, "@audio"))
            continue;
        bool valid = true;
        for (const char *c = cursor; c < end; ++c)
            if ((unsigned char)*c < 32)
                valid = false;
        if (!valid)
            continue;
        memcpy(authored, cursor, (size_t)(end - cursor));
        authored[end - cursor] = '\0';
        break;
    }
    fclose(fp);
    if (!authored[0])
        return true;
    char *last = NULL;
    for (char *scan = strstr(out, " / "); scan; scan = strstr(scan + 3, " / "))
        last = scan;
    if (!last)
        return true;
    const size_t prefix = (size_t)(last - out) + 3;
    if (strlen(authored) >= capacity - prefix)
        return true;
    memcpy(out + prefix, authored, strlen(authored) + 1);
    return true;
}
/* Display-only preference for the matching OBS source registration. This
 * never decides whether a shader may load, nor does it change preset IDs. */
static inline bool hs_shader_role_is_preferred(const char *label, const char *role)
{
    if (!label || !role)
        return false;
    if (strcmp(role, "source") == 0)
        return strncmp(label, "Source / ", 9) == 0;
    if (strcmp(role, "transition") == 0)
        return strncmp(label, "Transition / ", 13) == 0;
    if (strcmp(role, "filter") == 0)
        return strncmp(label, "Filter / ", 9) == 0;
    return false;
}
#endif
