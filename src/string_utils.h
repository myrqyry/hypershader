#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <obs.h>

// Safe string formatting with truncation detection
static inline bool safe_snprintf(char *dst, size_t size, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	int ret = vsnprintf(dst, size, fmt, args);
	va_end(args);

	if (ret < 0) {
		dst[0] = '\0';
		return false;
	}
	if ((size_t)ret >= size) {
		blog(LOG_WARNING, "[HyperShader] String truncated (needed %d, had %zu)", ret, size);
		return false;
	}
	return true;
}

// Safe string concatenation
static inline bool safe_strncat(char *dst, const char *src, size_t dst_size)
{
	size_t dst_len = strlen(dst);
	size_t src_len = strlen(src);

	if (dst_len + src_len >= dst_size) {
		blog(LOG_WARNING, "[HyperShader] Concat truncated");
		return false;
	}

	strncat(dst, src, dst_size - dst_len - 1);
	return true;
}

#endif // STRING_UTILS_H
