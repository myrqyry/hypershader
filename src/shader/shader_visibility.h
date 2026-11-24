#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum hs_visibility_op {
	HS_VIS_TRUTHY,
	HS_VIS_FALSEY,
	HS_VIS_EQ,
	HS_VIS_NE,
	HS_VIS_GT,
	HS_VIS_GTE,
	HS_VIS_LT,
	HS_VIS_LTE
};

typedef struct {
	char controller[128];
	enum hs_visibility_op operation;
	double threshold;
} hs_visibility_condition;

/* Supports: control, !control, control == 2, !=, >, >=, <, <=.
 * The input is untrusted shader metadata; malformed expressions fail closed. */
bool hs_visibility_parse(const char *expr, hs_visibility_condition *out);
bool hs_visibility_evaluate(const hs_visibility_condition *condition, double value);

#ifdef __cplusplus
}
#endif
