#ifndef HS_GLSL_TRANSLATE_H
#define HS_GLSL_TRANSLATE_H
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <util/dstr.h>
#include <util/bmem.h>

/* Keep the existing compatibility vocabulary, not a general GLSL compiler.
 * ASCII word runs also preserve the old non-match for e.g. 2vec2. */
static inline bool hs_glsl_word(unsigned char c)
{
    return (c>='a' && c<='z') || (c>='A' && c<='Z') ||
           (c>='0' && c<='9') || c=='_';
}

static inline const char *hs_glsl_replacement(const char *p,size_t n)
{
    if (n==4 && p[3]>='2' && p[3]<='4') {
        static const char *vectors[]={"float2","float3","float4"};
        static const char *matrices[]={"float2x2","float3x3","float4x4"};
        if (!memcmp(p,"vec",3)) return vectors[p[3]-'2'];
        if (!memcmp(p,"mat",3)) return matrices[p[3]-'2'];
    }
    if (n==5) {
        static const char *vectors[]={"int2","int3","int4"};
        if (p[4]>='2' && p[4]<='4' && !memcmp(p,"ivec",4))
            return vectors[p[4]-'2'];
        if (!memcmp(p,"fract",5)) return "frac";
    }
    if (n==3) {
        if (!memcmp(p,"mix",3)) return "lerp";
        if (!memcmp(p,"mod",3)) return "fmod";
    }
    if (n==9 && !memcmp(p,"sampler2D",9)) return "texture2d";
    return NULL;
}

/* One lexical traversal and one growable output. Only changed spans are
 * appended individually; unchanged text is copied in runs. Directives,
 * comments and quoted literals retain their exact spelling. */
static inline char *translate_glsl_to_obs(const char *source)
{
    if (!source) return NULL;
    struct dstr output={0};
    enum {HS_CODE,HS_BLOCK,HS_LINE,HS_QUOTE} state=HS_CODE;
    bool line_start=true,directive=false;
    char quote=0;
    const char *p=source,*run=source;
    while (*p) {
        /* Preserve escaped physical newlines in directives, comments and
         * strings, including CRLF. Do not reset the logical line. */
        if (*p=='\\' && (p[1]=='\n' || (p[1]=='\r' && p[2]=='\n'))) {
            p+=p[1]=='\r' ? 3 : 2;
            continue;
        }
        if (*p=='\n') {
            /* Block comments are whitespace, even across physical lines. */
            if (state!=HS_BLOCK) {
                state=HS_CODE;
                line_start=true;
                directive=false;
            }
            ++p;
            continue;
        }
        if (state==HS_BLOCK) {
            if (p[0]=='*' && p[1]=='/') {state=HS_CODE;p+=2;}
            else ++p;
            continue;
        }
        if (state==HS_QUOTE) {
            if (*p=='\\' && p[1]) p+=2;
            else if (*p==quote) {state=HS_CODE;++p;}
            else ++p;
            continue;
        }
        if (state==HS_LINE) {++p;continue;}
        /* Track comments even inside directives; a block comment may
         * extend beyond its last physical line. */
        if (p[0]=='/' && p[1]=='*') {state=HS_BLOCK;p+=2;continue;}
        if (p[0]=='/' && p[1]=='/') {state=HS_LINE;p+=2;continue;}
        if (line_start && (*p==' ' || *p=='\t' || *p=='\r' || *p=='\v' || *p=='\f')) {
            ++p;continue;
        }
        if (line_start && *p=='#') {directive=true;line_start=false;++p;continue;}
        line_start=false;
        if (*p=='"' || *p=='\'') {quote=*p++;state=HS_QUOTE;continue;}
        if (directive) {++p;continue;}
        if (hs_glsl_word((unsigned char)*p)) {
            const char *start=p++;
            while (hs_glsl_word((unsigned char)*p)) ++p;
            const char *replacement=hs_glsl_replacement(start,(size_t)(p-start));
            if (replacement) {
                dstr_ncat(&output,run,(size_t)(start-run));
                dstr_cat(&output,replacement);
                run=p;
            }
        } else ++p;
    }
    dstr_ncat(&output,run,(size_t)(p-run));
    /* Match the old translator's newline normalization. */
    if (p!=source && p[-1]!='\n') dstr_cat(&output,"\n");
    return output.array ? output.array : bstrdup("");
}
#endif
