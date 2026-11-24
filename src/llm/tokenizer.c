#include "tokenizer.h"
#include "../jsmn.h"
#include <obs-module.h> /* Required for bmalloc/bzalloc/bfree/bstrdup */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_TOKEN_LEN 128
#define INITIAL_BUCKETS 262144

/* Simple Hash Table for Vocab */
typedef struct token_node {
	char *token;
	int32_t id;
	struct token_node *next;
} token_node_t;

struct llm_tokenizer {
	token_node_t **buckets;
	size_t num_buckets;
	char **reverse_map; /* ID to Token map */
	int32_t vocab_size;
	int32_t unk_id;
	int32_t bos_id;
	int32_t eos_id;
};

static uint32_t hash_str(const char *str)
{
	uint32_t hash = 5381;
	int c;
	while ((c = *str++))
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	return hash;
}

static void ht_insert(llm_tokenizer_t *t, const char *token, int32_t id)
{
	uint32_t h = hash_str(token) % t->num_buckets;
	token_node_t *n = bmalloc(sizeof(token_node_t));
	n->token = bstrdup(token);
	n->id = id;
	n->next = t->buckets[h];
	t->buckets[h] = n;
}

static int32_t ht_lookup(llm_tokenizer_t *t, const char *token)
{
	uint32_t h = hash_str(token) % t->num_buckets;
	token_node_t *n = t->buckets[h];
	while (n) {
		if (strcmp(n->token, token) == 0)
			return n->id;
		n = n->next;
	}
	return -1;
}

static char *read_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	if (len <= 0) {
		fclose(f);
		return NULL;
	}
	fseek(f, 0, SEEK_SET);
	char *data = bmalloc(len + 1);
	if (!data) {
		fclose(f);
		return NULL;
	}
	size_t read_bytes = fread(data, 1, len, f);
	if (read_bytes != (size_t)len) {
		blog(LOG_WARNING, "[LLM Tokenizer] Partial read: expected %ld, got %zu", len, read_bytes);
	}
	data[read_bytes] = '\0';
	fclose(f);
	return data;
}

llm_tokenizer_t *llm_tokenizer_create(const char *json_path)
{
	char *json_str = read_file(json_path);
	if (!json_str)
		return NULL;

	llm_tokenizer_t *t = bzalloc(sizeof(llm_tokenizer_t));
	t->num_buckets = INITIAL_BUCKETS;
	t->buckets = bzalloc(t->num_buckets * sizeof(token_node_t *));
	/* Max vocab size safe alloc (260k for Gemma) */
	t->vocab_size = 262144;
	t->reverse_map = bzalloc(t->vocab_size * sizeof(char *));

	t->unk_id = 1; // Default fallback
	t->bos_id = 2;
	t->eos_id = 1; // Usually

	jsmn_parser p;
	jsmn_init(&p);
	int res = jsmn_parse(&p, json_str, strlen(json_str), NULL, 0);
	if (res < 0) {
		bfree(json_str);
		bfree(t);
		return NULL;
	}

	jsmntok_t *tokens = bmalloc(sizeof(jsmntok_t) * res);
	jsmn_init(&p);
	jsmn_parse(&p, json_str, strlen(json_str), tokens, res);

	/* Very naive traversal to find "vocab" inside "model" */
	/* Check structure: model -> vocab -> [token: id] OR {token: id} */
	/* Gemma tokenizer.json usually: {"model": {"vocab": {"<pad>": 0, ...}}} */

	int i = 0;
	/* Find model object */
	bool in_vocab = false;

	/* Skip root object */
	if (tokens[0].type == JSMN_OBJECT)
		i++;

	for (; i < res; i++) {
		if (tokens[i].type == JSMN_STRING) {
			char *key = json_str + tokens[i].start;
			int key_len = tokens[i].end - tokens[i].start;
			char key_buf[MAX_TOKEN_LEN];
			if (key_len < MAX_TOKEN_LEN) {
				strncpy(key_buf, key, key_len);
				key_buf[key_len] = '\0';
			} else
				key_buf[0] = '\0';

			if (!in_vocab && strcmp(key_buf, "vocab") == 0) {
				in_vocab = true;
				i++; // Skip key
				// Next should be object start
				continue;
			}

			if (in_vocab) {
				/* We are inside vocab object. Key is token, Value is ID */
				/* Handle token escaping? JSON string might contain escaped chars.
                   For simplicity, we use raw string segment for now. 
                   Real tokenizer.json uses specialized char replacements (e.g. whitespace) */

				// key is extracted above (key_buf).
				// But jsmntok points to raw JSON, including quotes/escapes?
				// JSMN gives content inside quotes.

				int32_t val_id = 0;
				// Move to value
				if (i + 1 < res) {
					// Check if value is integer (ID) or map (score/id)
					if (tokens[i + 1].type == JSMN_PRIMITIVE) {
						// Parse int
						char *val_str = json_str + tokens[i + 1].start;
						char scratch[32];
						int vlen = tokens[i + 1].end - tokens[i + 1].start;
						if (vlen < 32) {
							strncpy(scratch, val_str, vlen);
							scratch[vlen] = '\0';
							val_id = atoi(scratch);

							/* Insert */
							/* Important: Handle " " (sentencepiece underscore) replacement */
							/* Gemma uses " " (U+2581) for space */
							/* For simplicity in this greedy tokenizer, we will manually handle input mapping */

							ht_insert(t, key_buf, val_id);

							if (val_id >= 0 && val_id < t->vocab_size) {
								/* Replace SPIECE_UNDERLINE (0xE2 0x96 0x81) with space for readable decode?
                                    Or keep raw? For Shader code, we want raw usually, but standard text needs space.
                                    Let's store raw for now. */
								if (t->reverse_map[val_id])
									bfree(t->reverse_map[val_id]);
								t->reverse_map[val_id] = bstrdup(key_buf);
							}
						}
					}
					// Skip value
					i++;
				}

				/* Check if vocab object ended? 
                    JSMN flattened structure limits effective scope tracking without nesting count. 
                    We assume vocab is the big map and we iterate until end or weird generic structure. */
			}
		} else if (tokens[i].type == JSMN_OBJECT || tokens[i].type == JSMN_ARRAY) {
			/* Nested structures handling omitted for strict "vocab": { map } structure assumption */
		}
	}

	bfree(tokens);
	bfree(json_str);
	return t;
}

void llm_tokenizer_destroy(llm_tokenizer_t *t)
{
	if (!t)
		return;
	for (size_t i = 0; i < t->num_buckets; i++) {
		token_node_t *n = t->buckets[i];
		while (n) {
			token_node_t *next = n->next;
			bfree(n->token);
			bfree(n);
			n = next;
		}
	}
	for (int i = 0; i < t->vocab_size; i++) {
		if (t->reverse_map[i])
			bfree(t->reverse_map[i]);
	}
	bfree(t->reverse_map);
	bfree(t->buckets);
	bfree(t);
}

size_t llm_tokenize(llm_tokenizer_t *t, const char *text, int32_t *output, size_t max_tokens)
{
	if (!t || !text)
		return 0;
	size_t count = 0;

	/* Add BOS */
	if (count < max_tokens)
		output[count++] = t->bos_id;

	const char *ptr = text;
	while (*ptr) {
		/* Greedy strategy: find longest prefix in vocab */
		int best_len = 0;
		int32_t best_id = -1;

		/* Limit check length to reasonable token size */
		for (int len = 1; len <= MAX_TOKEN_LEN && ptr[len - 1]; len++) {
			char buf[MAX_TOKEN_LEN + 1];
			/* Map space to SPM underscore if needed? 
               Gemma usually maps space " " to " " (U+2581).
               We need to duplicate this mapping logic. 
               Hack: Try exact match first. If fail, maybe try prepending " " if at word start? */

			/* Construct candidate string. 
               Note: This is VASTLY simplified. Proper SPM handles unicode/bytes. */

			/* Attempt 1: Raw substring (for code tokens like "vec3") */
			strncpy(buf, ptr, len);
			buf[len] = '\0';

			int32_t id = ht_lookup(t, buf);
			if (id != -1) {
				best_len = len;
				best_id = id;
			}

			/* Attempt 2: Prepend space (Gemma specific) */
			/* If we are at start of word (previous was space or BOS), try " " + token */
			/* This is harder without state. */
		}

		if (best_id != -1) {
			if (count < max_tokens)
				output[count++] = best_id;
			ptr += best_len;
		} else {
			/* Unknown char, skip */
			ptr++;
		}
	}

	return count;
}

size_t llm_decode(llm_tokenizer_t *t, const int32_t *tokens, size_t num_tokens, char *output, size_t max_len)
{
	if (!t || !tokens || !output || max_len == 0)
		return 0;

	size_t written = 0;
	output[0] = '\0';

	for (size_t i = 0; i < num_tokens; i++) {
		int32_t id = tokens[i];
		if (id < 0 || id >= t->vocab_size)
			continue;

		const char *str = t->reverse_map[id];
		if (str) {
			size_t len = strlen(str);

			/* Handle SPIECE_UNDERLINE "\xe2\x96\x81" -> space " " conversion manually */
			/* Check if starts with lower-one-eighth block */
			const char *content = str;
			if (strncmp(str, "\xe2\x96\x81", 3) == 0) {
				if (written + 1 < max_len) {
					output[written++] = ' '; // Replace with space
				}
				content += 3; // Advance past underscore
				len -= 3;
			}

			/* Append remaining content */
			if (len > 0 && written + len < max_len) {
				memcpy(output + written, content, len);
				written += len;
			}
		}
	}

	if (written < max_len) {
		output[written] = '\0';
	} else {
		output[max_len - 1] = '\0';
	}
	return written;
}
