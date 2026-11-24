/* JSMN 1.0.0 by Serge A. Zaitsev <zserge@github.com> (MIT) */
#ifndef JSMN_H
#define JSMN_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { JSMN_UNDEFINED = 0, JSMN_OBJECT = 1, JSMN_ARRAY = 2, JSMN_STRING = 3, JSMN_PRIMITIVE = 4 } jsmntype_t;

typedef struct {
	jsmntype_t type;
	int start;
	int end;
	int size;
#ifdef JSMN_PARENT_LINKS
	int parent;
#endif
} jsmntok_t;

typedef struct {
	unsigned int pos;     /* offset in the JSON string */
	unsigned int toknext; /* next token to allocate */
	int toksuper;         /* superior token node, e.g parent object or array */
} jsmn_parser;

void jsmn_init(jsmn_parser *parser);

/* Run JSON parser. It parses a JSON data string into an array of tokens, each describing a single JSON object. */
int jsmn_parse(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, unsigned int num_tokens);

#ifdef __cplusplus
}
#endif

#endif /* JSMN_H */
