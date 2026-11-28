#include <assert.h>
#include <string.h>
#include "jsmn.h"

static int parse(const char *json, jsmntok_t *tokens, unsigned cap)
{
    jsmn_parser p;
    jsmn_init(&p);
    return jsmn_parse(&p, json, strlen(json), tokens, cap);
}
int main(void)
{
    const char *json = "{\"model\":{\"vocab\":{\"a\":0,\"b\\\"\":1}},\"merges\":[\"a b\",true,null,-1.2e+3]}";
    jsmntok_t t[32];
    memset(t, 0xA5, sizeof(t));
    assert(parse(json, NULL, 0) == 15);
    assert(parse(json, t, 32) == 15);
    assert(t[0].type == JSMN_OBJECT && t[0].end == (int)strlen(json));
    assert(t[2].type == JSMN_OBJECT && json[t[2].end-1] == '}');
    assert(t[4].type == JSMN_OBJECT && t[4].size == 4);
    assert(t[6].type == JSMN_PRIMITIVE && t[6].end-t[6].start == 1);
    assert(parse(json, t, 14) < 0);
    const char *bad[] = {"{", "[}", "{\"a\":}", "[1,]", "{\"a\" 1}", "true false",
                         "[01]", "\"\\q\"", "\"\\u123x\"", "\"a\nb\"", "[1e]", ""};
    for (unsigned i=0; i<sizeof(bad)/sizeof(bad[0]); ++i) {
        assert(parse(bad[i], NULL, 0) < 0);
        assert(parse(bad[i], t, 32) < 0);
    }
    char deep[260];
    memset(deep, '[', 129);
    memset(deep+129, ']', 129);
    deep[258]=0;
    assert(parse(deep, NULL, 0) < 0);
    return 0;
}
