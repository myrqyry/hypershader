/**
 * @file safe_alloc.h
 * @brief Safe memory allocation wrappers with NULL checks and error logging
 * 
 * These wrappers provide defensive memory management to prevent crashes
 * from allocation failures and ensure proper cleanup.
 */

#ifndef SAFE_ALLOC_H
#define SAFE_ALLOC_H

#include <obs-module.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Safe malloc with NULL check and error logging
 * @param size Number of bytes to allocate
 * @param caller Name of calling function for error reporting
 * @return Allocated memory or NULL on failure (with logged error)
 */
static inline void *safe_bmalloc(size_t size, const char *caller)
{
	if (size == 0) {
		blog(LOG_WARNING, "[hypershader] %s: attempted zero-size allocation", caller ? caller : "unknown");
		return NULL;
	}

	void *ptr = bmalloc(size);
	if (!ptr) {
		blog(LOG_ERROR, "[hypershader] %s: memory allocation failed (%zu bytes)", caller ? caller : "unknown",
		     size);
	}
	return ptr;
}

/**
 * @brief Safe zero-initialized allocation with NULL check and error logging
 * @param size Total number of bytes to allocate (zero-initialized)
 * @param caller Name of calling function for error reporting
 * @return Allocated zero-initialized memory or NULL on failure
 */
static inline void *safe_bzalloc(size_t size, const char *caller)
{
	if (size == 0) {
		blog(LOG_WARNING, "[hypershader] %s: attempted zero-size allocation", caller ? caller : "unknown");
		return NULL;
	}

	void *ptr = bzalloc(size);
	if (!ptr) {
		blog(LOG_ERROR, "[hypershader] %s: memory allocation failed (%zu bytes)", caller ? caller : "unknown",
		     size);
	}
	return ptr;
}

/**
 * @brief Safe string duplication with length limit
 * @param src Source string
 * @param len Maximum number of characters to copy
 * @param caller Name of calling function for error reporting
 * @return Newly allocated null-terminated string or NULL on failure
 */
static inline char *safe_strdup_n(const char *src, size_t len, const char *caller)
{
	if (!src) {
		blog(LOG_WARNING, "[hypershader] %s: NULL source in string duplication", caller ? caller : "unknown");
		return NULL;
	}

	if (len == 0) {
		/* Return empty string for zero length */
		char *empty = bmalloc(1);
		if (empty)
			empty[0] = '\0';
		return empty;
	}

	/* Calculate actual copy length (don't exceed source length) */
	size_t src_len = strlen(src);
	size_t copy_len = (len < src_len) ? len : src_len;

	char *dest = bmalloc(copy_len + 1);
	if (!dest) {
		blog(LOG_ERROR, "[hypershader] %s: failed to allocate string (%zu bytes)", caller ? caller : "unknown",
		     copy_len + 1);
		return NULL;
	}

	memcpy(dest, src, copy_len);
	dest[copy_len] = '\0';
	return dest;
}

/**
 * @brief Safe string duplication (full string)
 * @param src Source string
 * @param caller Name of calling function for error reporting
 * @return Newly allocated string copy or NULL on failure
 */
static inline char *safe_bstrdup(const char *src, const char *caller)
{
	if (!src) {
		blog(LOG_WARNING, "[hypershader] %s: NULL source in string duplication", caller ? caller : "unknown");
		return NULL;
	}

	char *dest = bstrdup(src);
	if (!dest) {
		blog(LOG_ERROR, "[hypershader] %s: failed to duplicate string", caller ? caller : "unknown");
	}
	return dest;
}

/**
 * @brief Safe free with NULL guard
 * @param ptr Pointer to free (may be NULL)
 */
static inline void safe_bfree(void *ptr)
{
	if (ptr) {
		bfree(ptr);
	}
}

/**
 * @brief Safe free and NULL assignment for pointer variables
 * @param ptr_ptr Pointer to the pointer variable to free and NULL
 */
#define SAFE_BFREE_NULL(ptr_ptr) do { \
    if (*(ptr_ptr)) { \
        bfree(*(ptr_ptr)); \
        *(ptr_ptr) = NULL; \
    } \
} while(0)

#endif /* SAFE_ALLOC_H */
