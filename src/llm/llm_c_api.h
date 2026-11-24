#ifndef LLM_C_API_H
#define LLM_C_API_H

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle
typedef struct llm_instance llm_instance_t;

// API
llm_instance_t* llm_create_instance(void);
void llm_destroy_instance(llm_instance_t* inst);

/* Load model from directory. Returns 1 on success, 0 on fail. */
int llm_load_model(llm_instance_t* inst, const char* model_dir);

/* Generate shader code. Returns newly allocated string.
 * IMPORTANT: Must be freed with llm_free(), NOT free() or bfree(). */
char* llm_generate(llm_instance_t* inst, const char* prompt);

/* Free string returned by llm_generate */
void llm_free(char* str);

/* Get last error message */
const char* llm_get_error(llm_instance_t* inst);

#ifdef __cplusplus
}
#endif

#endif // LLM_C_API_H
