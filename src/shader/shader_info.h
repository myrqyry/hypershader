#ifndef HYPERSHADER_SHADER_INFO_H
#define HYPERSHADER_SHADER_INFO_H

/* A small, bounded header-only reader for user-visible attribution.
 * These strings are DATA, not executable shader directives. Never infer an
 * author or license from a filename; leave absent fields absent. */
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct hs_shader_info {
    char description[384];
    char notes[768];
    char author[256];
    char credit[512];
    char source[768];
    char license[160];
};

static inline bool hs_shader_info_prefix(const char *value, const char *prefix)
{
    while (*prefix) {
        if (tolower((unsigned char)*value++) != tolower((unsigned char)*prefix++))
            return false;
    }
    return true;
}

static inline void hs_shader_info_copy(char *dest, size_t size, const char *value)
{
    if (!size || !value)
        return;
    while (isspace((unsigned char)*value))
        ++value;
    if (*value == '"')
        ++value;
    size_t n = 0;
    while (*value && *value != '\r' && *value != '\n' &&
           (unsigned char)*value >= 32 && n < size - 1) {
        if (*value == '"' && (!value[1] || value[1] == ' ' ||
                              value[1] == '\r' || value[1] == '\n'))
            break;
        dest[n++] = *value++;
    }
    while (n && isspace((unsigned char)dest[n - 1]))
        --n;
    dest[n] = '\0';
}

/* Effect-file headers may declare metadata through @author/@credits/
 * @description/@notes/@usage/@source/@license and literal header credits.
 * Stop before shader code so comments inside functions are never treated
 * as author metadata. File and field sizes are bounded by design. */
static inline bool hs_shader_info_read(const char *shader_file, struct hs_shader_info *info)
{
    if (!info)
        return false;
    memset(info, 0, sizeof(*info));
    if (!shader_file || !*shader_file)
        return false;
    FILE *file = fopen(shader_file, "rb");
    if (!file)
        return false;

    char line[1024];
    unsigned count = 0;
    while (count++ < 96 && fgets(line, sizeof(line), file)) {
        const char *comment = line;
        while (isspace((unsigned char)*comment))
            ++comment;
        if (!*comment)
            continue;
        if (comment[0] != '/' || comment[1] != '/')
            break;
        comment += 2;
        while (*comment == ' ' || *comment == '\t')
            ++comment;
        if (hs_shader_info_prefix(comment, "@description "))
            hs_shader_info_copy(info->description, sizeof(info->description), comment + 13);
        else if (hs_shader_info_prefix(comment, "@notes "))
            hs_shader_info_copy(info->notes, sizeof(info->notes), comment + 7);
        else if (hs_shader_info_prefix(comment, "@usage "))
            hs_shader_info_copy(info->notes, sizeof(info->notes), comment + 7);
        else if (hs_shader_info_prefix(comment, "Notes: "))
            hs_shader_info_copy(info->notes, sizeof(info->notes), comment + 7);
        else if (hs_shader_info_prefix(comment, "@author "))
            hs_shader_info_copy(info->author, sizeof(info->author), comment + 8);
        else if (hs_shader_info_prefix(comment, "@credits "))
            hs_shader_info_copy(info->credit, sizeof(info->credit), comment + 9);
        else if (hs_shader_info_prefix(comment, "@source "))
            hs_shader_info_copy(info->source, sizeof(info->source), comment + 8);
        else if (hs_shader_info_prefix(comment, "@license "))
            hs_shader_info_copy(info->license, sizeof(info->license), comment + 9);
        else if (hs_shader_info_prefix(comment, "Author: "))
            hs_shader_info_copy(info->author, sizeof(info->author), comment + 8);
        else if (hs_shader_info_prefix(comment, "Credits: "))
            hs_shader_info_copy(info->credit, sizeof(info->credit), comment + 9);
        else if (hs_shader_info_prefix(comment, "Source: "))
            hs_shader_info_copy(info->source, sizeof(info->source), comment + 8);
        else if (hs_shader_info_prefix(comment, "Upstream: "))
            hs_shader_info_copy(info->source, sizeof(info->source), comment + 10);
        else if (hs_shader_info_prefix(comment, "License: "))
            hs_shader_info_copy(info->license, sizeof(info->license), comment + 9);
        else if (hs_shader_info_prefix(comment, "Copyright") && !info->credit[0]) {
            hs_shader_info_copy(info->credit, sizeof(info->credit), comment);
            if (!info->license[0] && strstr(comment, "MIT"))
                hs_shader_info_copy(info->license, sizeof(info->license), "MIT");
        }
    }
    fclose(file);
    return true;
}

#endif
