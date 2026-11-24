#include "shader_visibility.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool hs_visibility_parse(const char *expr, hs_visibility_condition *out)
{
	if (!expr || !out)
		return false;
	memset(out, 0, sizeof(*out));
	const unsigned char *p = (const unsigned char *)expr;
	while (isspace(*p))
		p++;
	bool negated = false;
	if (*p == '!') {
		negated = true;
		p++;
	}
	if (!isalpha(*p) && *p != '_')
		return false;
	size_t n = 0;
	while (isalnum(*p) || *p == '_') {
		if (n + 1 >= sizeof(out->controller))
			return false;
		out->controller[n++] = (char)*p++;
	}
	out->controller[n] = '\0';
	while (isspace(*p))
		p++;
	if (!*p) {
		out->operation = negated ? HS_VIS_FALSEY : HS_VIS_TRUTHY;
		return true;
	}
	if (negated)
		return false;
	if (p[0] == '=' && p[1] == '=') {
		out->operation = HS_VIS_EQ;
		p += 2;
	} else if (p[0] == '!' && p[1] == '=') {
		out->operation = HS_VIS_NE;
		p += 2;
	} else if (p[0] == '>' && p[1] == '=') {
		out->operation = HS_VIS_GTE;
		p += 2;
	} else if (p[0] == '<' && p[1] == '=') {
		out->operation = HS_VIS_LTE;
		p += 2;
	} else if (*p == '>') {
		out->operation = HS_VIS_GT;
		p++;
	} else if (*p == '<') {
		out->operation = HS_VIS_LT;
		p++;
	} else {
		return false;
	}
	while (isspace(*p))
		p++;
	if (!*p)
		return false;
	char *end = NULL;
	double value = strtod((const char *)p, &end);
	if (end == (const char *)p || !isfinite(value))
		return false;
	while (isspace((unsigned char)*end))
		end++;
	if (*end)
		return false;
	out->threshold = value;
	return true;
}

bool hs_visibility_evaluate(const hs_visibility_condition *condition, double value)
{
	if (!condition || !isfinite(value))
		return false;
	switch (condition->operation) {
	case HS_VIS_TRUTHY:
		return value != 0;
	case HS_VIS_FALSEY:
		return value == 0;
	case HS_VIS_EQ:
		return value == condition->threshold;
	case HS_VIS_NE:
		return value != condition->threshold;
	case HS_VIS_GT:
		return value > condition->threshold;
	case HS_VIS_GTE:
		return value >= condition->threshold;
	case HS_VIS_LT:
		return value < condition->threshold;
	case HS_VIS_LTE:
		return value <= condition->threshold;
	}
	return false;
}
