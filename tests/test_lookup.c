/* Compare substring rewrites against v0.5.2 (craigtrim/axiom#129). */
#include "mc.h"
#include "lookup_reference.h"
static int check(J *fwd)
{
	J *expected = reference_lookup(fwd), *actual = generate_lookup(fwd);
	char *a = cJSON_PrintUnformatted(expected),
	     *b = cJSON_PrintUnformatted(actual);
	int ok = !strcmp(a, b);
	LookupState *state = lookup_state_build(fwd);
	char *c = cJSON_PrintUnformatted(lookup_state_json(state));
	if (strcmp(a, c)) {
		fprintf(stderr, "persistent lookup mismatch\n%s\n%s\n", a, c);
		ok = 0;
	}
	free(c);
	LookupState *reused = lookup_state_reuse(fwd, actual);
	for (int step = 0; step < 3 && fwd->child; step++) {
		J *one = OBJ(), *old = fwd->child;
		PUT(one, old->string, DUP(old));
		lookup_state_entity(state, one, -1);
		lookup_state_entity(reused, one, -1);
		DEL(cJSON_DetachItemViaPointer(fwd, old));
		DEL(one);
		lookup_state_flush(state);
		lookup_state_flush(reused);
		J *expected_update = generate_lookup(fwd);
		if (!cJSON_Compare(expected_update, lookup_state_json(state),
				   1) ||
		    !cJSON_Compare(expected_update, lookup_state_json(reused),
				   1)) {
			fprintf(stderr,
				"incremental lookup mismatch after removal %d\n",
				step);
			ok = 0;
		}
		DEL(expected_update);
	}
	lookup_state_free(state);
	lookup_state_free(reused);
	if (!ok)
		fprintf(stderr, "lookup mismatch\nexpected: %s\nactual: %s\n",
			a, b);
	free(a);
	free(b);
	DEL(expected);
	DEL(actual);
	return ok;
}
int main(void)
{
	const char *terms[] = { "red green blue",
				"bred green blue",
				"red green blues",
				"red green red green",
				"red green",
				"green blue red green blue",
				"red green blue amber violet black",
				"red green blue amber violet",
				"red green blue amber",
				"red and green",
				"red for green",
				"red to green",
				"red of green",
				"a_b c d",
				"a b_c d",
				"red green!",
				"green blue?",
				"red green.",
				"\xc3\xa9"
				"cole verte bleue",
				"\xce\xbb"
				" ecole verte",
				"green  blue",
				"" };
	unsigned state = 129;
	for (int run = 0; run < 180; run++) {
		J *fwd = OBJ();
		for (int i = 0; i < 15 + run % 30; i++) {
			char key[80];
			snprintf(key, sizeof(key), "entity_%d_%d", run, i);
			J *values = ARR();
			for (int j = 0; j < 4; j++) {
				state = state * 1664525u + 1013904223u;
				ADD(values,
				    STR(terms[state % (sizeof(terms) /
						       sizeof(*terms))]));
			}
			PUT(fwd, key, values);
		}
		int ok = check(fwd);
		DEL(fwd);
		if (!ok)
			return 1;
	}
	puts("180 ordered lookup comparisons passed");
	return 0;
}
