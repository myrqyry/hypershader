// Deterministic parser/evaluator contract for native shader visibility metadata.
#include "shader/shader_visibility.h"
#include <cassert>
#include <cstdio>
int main()
{
	hs_visibility_condition c{};
	assert(hs_visibility_parse("show_vectors", &c));
	assert(!hs_visibility_evaluate(&c, 0));
	assert(hs_visibility_evaluate(&c, 1));
	assert(hs_visibility_parse("!show_vectors", &c));
	assert(hs_visibility_evaluate(&c, 0));
	assert(!hs_visibility_evaluate(&c, 1));
	assert(hs_visibility_parse("mode == 2", &c));
	assert(hs_visibility_evaluate(&c, 2));
	assert(!hs_visibility_evaluate(&c, 1));
	assert(hs_visibility_parse("mode != 2", &c));
	assert(hs_visibility_evaluate(&c, 1));
	assert(hs_visibility_parse("strength >= 0.5", &c));
	assert(hs_visibility_evaluate(&c, 0.5));
	assert(!hs_visibility_evaluate(&c, 0.49));
	assert(hs_visibility_parse("strength <= 0.5", &c));
	assert(hs_visibility_evaluate(&c, 0.5));
	assert(hs_visibility_parse("strength > 0.5", &c));
	assert(!hs_visibility_evaluate(&c, 0.5));
	assert(hs_visibility_parse("strength < 0.5", &c));
	assert(!hs_visibility_evaluate(&c, 0.5));
	assert(!hs_visibility_parse("a || b", &c));
	assert(!hs_visibility_parse("a == notanumber", &c));
	assert(!hs_visibility_parse("a == 1 extras", &c));
	assert(!hs_visibility_parse("123foo", &c));
	assert(!hs_visibility_parse("a.b", &c));
	std::puts("CONDITIONAL_CONTROLS_PARSE_PASS=1");
}
