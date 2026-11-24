/* Minimal JSMN implementation (public domain / MIT style) */
#include <string.h>
#include "jsmn.h"

static int jsmn_parse_primitive(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, size_t num_tokens,
				size_t *toknext)
{
	size_t start = parser->pos;
	while (parser->pos < len && js[parser->pos] != '\0') {
		char c = js[parser->pos];
		if (c == ',' || c == ']' || c == '}' || c == '\n' || c == '\r' || c == '\t' || c == ' ')
			break;
		parser->pos++;
	}
	if (*toknext >= (size_t)num_tokens)
		return -1;
	tokens[*toknext].type = JSMN_PRIMITIVE;
	tokens[*toknext].start = (int)start;
	tokens[*toknext].end = (int)parser->pos;
	tokens[*toknext].size = 0;
	(*toknext)++;
	return 0;
}

static void jsmn_skip_whitespace(jsmn_parser *parser, const char *js, size_t len)
{
	while (parser->pos < len) {
		char c = js[parser->pos];
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
			parser->pos++;
		else
			break;
	}
}

void jsmn_init(jsmn_parser *parser)
{
	parser->pos = 0;
	parser->toknext = 0;
	parser->toksuper = -1;
}

int jsmn_parse(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, unsigned int num_tokens)
{
	size_t i;
	size_t toknext = parser->toknext;

	for (i = parser->pos; i < len; i = parser->pos) {
		jsmn_skip_whitespace(parser, js, len);
		if (parser->pos >= len)
			break;
		char c = js[parser->pos++];
		switch (c) {
		case '{':
		case '[': {
			if (toknext >= (size_t)num_tokens)
				return -1;
			tokens[toknext].type = (c == '{') ? JSMN_OBJECT : JSMN_ARRAY;
			tokens[toknext].start = (int)(parser->pos - 1);
			tokens[toknext].size = 0;
#ifdef JSMN_PARENT_LINKS
			tokens[toknext].parent = parser->toksuper;
#endif
			parser->toksuper = (int)toknext;
			toknext++;
			break;
		}
		case '}':
		case ']': {
			jsmntype_t type = (c == '}') ? JSMN_OBJECT : JSMN_ARRAY;
			int found = 0;
			for (int j = (int)toknext - 1; j >= 0; j--) {
				if (tokens[j].start != -1 && tokens[j].end == -1) {
					if (tokens[j].type == type) {
						tokens[j].end = (int)parser->pos;
						found = 1;
#ifdef JSMN_PARENT_LINKS
						parser->toksuper = tokens[j].parent;
#else
						parser->toksuper = -1;
#endif
						break;
					}
				}
			}
			if (!found)
				return -1;
			break;
		}
		case '"': {
			if (toknext >= (size_t)num_tokens)
				return -1;
			size_t start = parser->pos;
			while (parser->pos < len) {
				char ch = js[parser->pos];
				if (ch == '"')
					break;
				if (ch == '\\')
					parser->pos++; /* skip escaped */
				parser->pos++;
			}
			if (parser->pos >= len)
				return -1;
			tokens[toknext].type = JSMN_STRING;
			tokens[toknext].start = (int)start;
			tokens[toknext].end = (int)parser->pos;
			tokens[toknext].size = 0;
#ifdef JSMN_PARENT_LINKS
			tokens[toknext].parent = parser->toksuper;
#endif
			toknext++;
			parser->pos++; /* skip closing quote */
			break;
		}
		default: {
			/* primitive */
			parser->pos--;
			if (jsmn_parse_primitive(parser, js, len, tokens, num_tokens, &toknext) != 0)
				return -1;
			break;
		}
		}
	}

	parser->pos = i;
	parser->toknext = (int)toknext;
	return (int)toknext;
}
