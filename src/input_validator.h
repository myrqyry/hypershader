/**
 * @file input_validator.h
 * @brief Input validation utilities for paths, numeric ranges, and strings
 * 
 * Provides defensive validation to prevent security vulnerabilities
 * and ensure graceful handling of malformed inputs.
 */

#ifndef INPUT_VALIDATOR_H
#define INPUT_VALIDATOR_H

#include <obs-module.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <sys/stat.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#endif

/**
 * Validation result codes
 */
typedef enum {
	VALIDATION_OK = 0,
	VALIDATION_NULL_INPUT,
	VALIDATION_EMPTY_INPUT,
	VALIDATION_TOO_LONG,
	VALIDATION_INVALID_PATH,
	VALIDATION_UNSAFE_PATH,
	VALIDATION_FILE_NOT_FOUND,
	VALIDATION_OUT_OF_RANGE,
	VALIDATION_INVALID_FORMAT
} validation_result_t;

/**
 * @brief Get human-readable error message for validation result
 * @param result Validation result code
 * @return Static string describing the error
 */
static inline const char *validation_error_message(validation_result_t result)
{
	switch (result) {
	case VALIDATION_OK:
		return "Validation successful";
	case VALIDATION_NULL_INPUT:
		return "Input is NULL";
	case VALIDATION_EMPTY_INPUT:
		return "Input is empty";
	case VALIDATION_TOO_LONG:
		return "Input exceeds maximum length";
	case VALIDATION_INVALID_PATH:
		return "Invalid file path";
	case VALIDATION_UNSAFE_PATH:
		return "Potentially unsafe path (path traversal detected)";
	case VALIDATION_FILE_NOT_FOUND:
		return "File not found";
	case VALIDATION_OUT_OF_RANGE:
		return "Value out of valid range";
	case VALIDATION_INVALID_FORMAT:
		return "Invalid format";
	default:
		return "Unknown validation error";
	}
}

/**
 * @brief Check if path contains path traversal attempts
 * @param path Path to check
 * @return true if path appears safe, false if traversal detected
 */
static inline bool is_path_safe(const char *path)
{
	if (!path)
		return false;

	/* Check for parent directory traversal */
	if (strstr(path, "..") != NULL) {
		return false;
	}

	/* Check for null byte injection */
	size_t len = strlen(path);
	for (size_t i = 0; i < len; i++) {
		if (path[i] == '\0') {
			return false; /* Should be caught by strlen, but defensive */
		}
	}

	return true;
}

/**
 * @brief Check if path is within allowed shader directories
 * @param path Absolute path to check
 * @return true if path is in an allowed location
 */
static inline bool is_allowed_shader_path(const char *path)
{
	if (!path || path[0] == '\0')
		return false;

#ifdef _WIN32
	/* On Windows, path validation is handled by OBS; accept all valid paths */
	return true;
#else
	/* Resolve to absolute path if possible */
	char resolved[PATH_MAX];
	if (!realpath(path, resolved)) {
		/* Path doesn't exist or can't be resolved - allow for new files */
		strncpy(resolved, path, PATH_MAX - 1);
		resolved[PATH_MAX - 1] = '\0';
	}

#ifdef __APPLE__
	const char *allowed_prefixes[] = {
		"/Users/",                /* User home directories */
		"/tmp/",                  /* Temporary files */
		"/Library/",              /* System Library */
		"/Applications/OBS.app/", /* OBS application bundle */
		NULL};
#else
	/* Linux */
	const char *allowed_prefixes[] = {
		"/home/",                /* User home directories */
		"/tmp/",                 /* Temporary files */
		"/usr/share/obs/",       /* System OBS data */
		"/usr/local/share/obs/", /* Local OBS data */
		"/opt/obs-studio/",      /* Alternative OBS install */
		NULL};
#endif

	for (int i = 0; allowed_prefixes[i] != NULL; i++) {
		if (strncmp(resolved, allowed_prefixes[i], strlen(allowed_prefixes[i])) == 0) {
			return true;
		}
	}

	/* Also allow relative paths within plugin data directories */
	if (path[0] != '/') {
		return true; /* Relative paths are resolved by OBS */
	}

	return false;
#endif
}

/**
 * @brief Validate a file path
 * @param path Path to validate
 * @param max_len Maximum allowed path length
 * @param must_exist If true, file must exist
 * @return Validation result
 */
static inline validation_result_t validate_file_path(const char *path, size_t max_len, bool must_exist)
{
	if (!path) {
		return VALIDATION_NULL_INPUT;
	}

	size_t len = strlen(path);
	if (len == 0) {
		return VALIDATION_EMPTY_INPUT;
	}

	if (len >= max_len) {
		return VALIDATION_TOO_LONG;
	}

	/* Check for path traversal */
	if (!is_path_safe(path)) {
		return VALIDATION_UNSAFE_PATH;
	}

	/* Check allowed locations */
	if (!is_allowed_shader_path(path)) {
		return VALIDATION_UNSAFE_PATH;
	}

	/* Check file existence if required */
	if (must_exist) {
		struct stat st;
		if (stat(path, &st) != 0) {
			return VALIDATION_FILE_NOT_FOUND;
		}
	}

	return VALIDATION_OK;
}

/**
 * @brief Validate a numeric value is within range
 * @param value Value to check
 * @param min Minimum allowed value
 * @param max Maximum allowed value
 * @return Validation result
 */
static inline validation_result_t validate_numeric_range(double value, double min, double max)
{
	/* Check for NaN or Infinity */
	if (isnan(value) || isinf(value)) {
		return VALIDATION_OUT_OF_RANGE;
	}

	/* Check bounds */
	if (value < min || value > max) {
		return VALIDATION_OUT_OF_RANGE;
	}

	return VALIDATION_OK;
}

/**
 * @brief Clamp a numeric value to valid range (with logging)
 * @param value Value to clamp
 * @param min Minimum value
 * @param max Maximum value
 * @param param_name Parameter name for logging
 * @return Clamped value
 */
static inline double clamp_numeric_range(double value, double min, double max, const char *param_name)
{
	if (isnan(value)) {
		blog(LOG_WARNING, "[HyperShader] %s: NaN value, defaulting to %f",
		     param_name ? param_name : "parameter", min);
		return min;
	}

	if (isinf(value)) {
		blog(LOG_WARNING, "[HyperShader] %s: Infinite value, clamping", param_name ? param_name : "parameter");
		return (value > 0) ? max : min;
	}

	if (value < min) {
		blog(LOG_DEBUG, "[HyperShader] %s: value %f below minimum %f, clamping",
		     param_name ? param_name : "parameter", value, min);
		return min;
	}

	if (value > max) {
		blog(LOG_DEBUG, "[HyperShader] %s: value %f above maximum %f, clamping",
		     param_name ? param_name : "parameter", value, max);
		return max;
	}

	return value;
}

/**
 * @brief Validate string length
 * @param str String to check
 * @param max_len Maximum allowed length
 * @return Validation result
 */
static inline validation_result_t validate_string_length(const char *str, size_t max_len)
{
	if (!str) {
		return VALIDATION_NULL_INPUT;
	}

	if (str[0] == '\0') {
		return VALIDATION_EMPTY_INPUT;
	}

	if (strlen(str) > max_len) {
		return VALIDATION_TOO_LONG;
	}

	return VALIDATION_OK;
}

/**
 * @brief Validate shader file path with logging
 * @param path Path to validate
 * @param caller Function name for logging
 * @return true if valid, false otherwise (with logged error)
 */
static inline bool validate_shader_path_with_log(const char *path, const char *caller)
{
	validation_result_t result = validate_file_path(path, PATH_MAX, false);

	if (result != VALIDATION_OK) {
		blog(LOG_ERROR, "[HyperShader] %s: Invalid shader path: %s (%s)", caller ? caller : "unknown",
		     path ? path : "(null)", validation_error_message(result));
		return false;
	}

	return true;
}

#endif /* INPUT_VALIDATOR_H */
