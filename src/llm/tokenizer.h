#ifndef LLM_TOKENIZER_H
#define LLM_TOKENIZER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct llm_tokenizer llm_tokenizer_t;

/* Create tokenizer from tokenizer.json file */
llm_tokenizer_t* llm_tokenizer_create(const char* json_path);

/* Destroy tokenizer */
void llm_tokenizer_destroy(llm_tokenizer_t* tokenizer);

/* Tokenize text into token IDs. 
   Returns number of tokens written to output buffer. 
   If max_tokens is too small, returns needed size but writes truncated. */
size_t llm_tokenize(llm_tokenizer_t* tokenizer, const char* text, int32_t* output, size_t max_tokens);

/* Decode token IDs back to text (simplified) */
size_t llm_decode(llm_tokenizer_t* tokenizer, const int32_t* tokens, size_t num_tokens, char* output, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif // LLM_TOKENIZER_H
