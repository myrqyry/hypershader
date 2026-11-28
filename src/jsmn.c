/* Minimal JSMN implementation (public domain / MIT style) */
#include "jsmn.h"
#include <limits.h>
#include <string.h>

#define HS_JSON_MAX_DEPTH 128

struct scanner {
    jsmn_parser *p;
    const char *js;
    size_t len;
    jsmntok_t *tokens;
    unsigned cap;
};

static char peek(const struct scanner *s)
{
    return s->p->pos < s->len ? s->js[s->p->pos] : '\0';
}

static void whitespace(struct scanner *s)
{
    while (peek(s) == ' ' || peek(s) == '\t' || peek(s) == '\r' || peek(s) == '\n')
        ++s->p->pos;
}

static bool digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool hex(char c)
{
    return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int begin(struct scanner *s, jsmntype_t type, int parent)
{
    if (s->p->toknext >= INT_MAX || (s->tokens && s->p->toknext >= s->cap))
        return -1;
    int i = (int)s->p->toknext++;
    if (s->tokens) {
        s->tokens[i] = (jsmntok_t){.type = type, .start = (int)s->p->pos, .end = -1};
#ifdef JSMN_PARENT_LINKS
        s->tokens[i].parent = parent;
#endif
        if (parent >= 0)
            ++s->tokens[parent].size;
    }
    return i;
}

static void finish(struct scanner *s, int i)
{
    if (s->tokens)
        s->tokens[i].end = (int)s->p->pos;
}

static bool string(struct scanner *s, int parent)
{
    ++s->p->pos;
    int i = begin(s, JSMN_STRING, parent);
    if (i < 0)
        return false;
    while (s->p->pos < s->len) {
        unsigned char c = (unsigned char)peek(s);
        if (c == '"') {
            finish(s, i);
            ++s->p->pos;
            return true;
        }
        if (c < 32)
            return false;
        ++s->p->pos;
        if (c == '\\') {
            char escape = peek(s);
            if (!escape || !strchr("\"\\/bfnrtu", escape))
                return false;
            ++s->p->pos;
            if (escape == 'u') {
                for (int n = 0; n < 4; ++n) {
                    if (!hex(peek(s)))
                        return false;
                    ++s->p->pos;
                }
            }
        }
    }
    return false;
}

static bool number(struct scanner *s)
{
    if (peek(s) == '-')
        ++s->p->pos;
    if (!digit(peek(s)))
        return false;
    if (peek(s) == '0')
        ++s->p->pos;
    else
        while (digit(peek(s)))
            ++s->p->pos;
    if (peek(s) == '.') {
        ++s->p->pos;
        if (!digit(peek(s)))
            return false;
        while (digit(peek(s)))
            ++s->p->pos;
    }
    if (peek(s) == 'e' || peek(s) == 'E') {
        ++s->p->pos;
        if (peek(s) == '+' || peek(s) == '-')
            ++s->p->pos;
        if (!digit(peek(s)))
            return false;
        while (digit(peek(s)))
            ++s->p->pos;
    }
    return true;
}

static bool value(struct scanner *s, int parent, unsigned depth)
{
    whitespace(s);
    char c = peek(s);
    if (c == '"')
        return string(s, parent);
    if (c == '{' || c == '[') {
        if (depth >= HS_JSON_MAX_DEPTH)
            return false;
        bool object = c == '{';
        char close = object ? '}' : ']';
        int i = begin(s, object ? JSMN_OBJECT : JSMN_ARRAY, parent);
        if (i < 0)
            return false;
        ++s->p->pos;
        whitespace(s);
        if (peek(s) != close) {
            for (;;) {
                if (object) {
                    if (peek(s) != '"' || !string(s, i))
                        return false;
                    whitespace(s);
                    if (peek(s) != ':')
                        return false;
                    ++s->p->pos;
                }
                if (!value(s, i, depth + 1))
                    return false;
                whitespace(s);
                if (peek(s) == close)
                    break;
                if (peek(s) != ',')
                    return false;
                ++s->p->pos;
                whitespace(s);
            }
        }
        ++s->p->pos;
        finish(s, i);
        return true;
    }
    int i = begin(s, JSMN_PRIMITIVE, parent);
    if (i < 0)
        return false;
    const char *literal = c == 't' ? "true" : c == 'f' ? "false" : c == 'n' ? "null" : NULL;
    if (literal) {
        size_t n = strlen(literal);
        if (s->len - s->p->pos < n || memcmp(s->js + s->p->pos, literal, n))
            return false;
        s->p->pos += (unsigned)n;
    } else if (!number(s)) {
        return false;
    }
    finish(s, i);
    return true;
}

void jsmn_init(jsmn_parser *parser)
{
    *parser = (jsmn_parser){.toksuper = -1};
}

int jsmn_parse(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, unsigned int num_tokens)
{
    if (!parser || !js || len > INT_MAX || parser->pos || parser->toknext)
        return -1;
    struct scanner s = {parser, js, len, tokens, num_tokens};
    if (!value(&s, -1, 0))
        return -1;
    whitespace(&s);
    return parser->pos == len ? (int)parser->toknext : -1;
}
