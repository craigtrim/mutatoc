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
static int check_maps(void)
{
	Map map = { 0 };
	char key[32];
	map_put(&map, "item0", (void *)(uintptr_t)1);
	size_t boundary = map.cap / 2;
	for (size_t i = 1; i < boundary; i++) {
		snprintf(key, sizeof(key), "item%zu", i);
		map_put(&map, key, (void *)(uintptr_t)(i + 1));
	}
	Slot *slots = map.slots;
	map_put(&map, "item0", (void *)(uintptr_t)2);
	if (map.slots != slots) {
		fprintf(stderr, "updating an existing key resized its table\n");
		return 0;
	}
	map_free(&map);
	size_t expected[128] = { 0 };
	unsigned random = 129;
	for (int step = 0; step < 3000; step++) {
		random = random * 1664525u + 1013904223u;
		unsigned index = (random >> 8) % 128;
		snprintf(key, sizeof(key), "item%u", index);
		if (random & 1) {
			expected[index] = (size_t)step + 1;
			map_put(&map, key, (void *)(uintptr_t)expected[index]);
		} else {
			expected[index] = 0;
			map_delete(&map, key);
		}
		if (step == 500)
			map_reserve(&map, 300);
		for (unsigned i = 0; i < 128; i++) {
			snprintf(key, sizeof(key), "item%u", i);
			if ((size_t)(uintptr_t)map_get(&map, key) !=
			    expected[i]) {
				fprintf(stderr,
					"hash table mismatch after operation %d\n",
					step);
				return 0;
			}
		}
	}
	map_free(&map);
	return 1;
}

static int check_map_ownership(void)
{
	Map map = { 0 };
	char keys[160][32];
	for (int i = 0; i < 160; i++) {
		snprintf(keys[i], sizeof(keys[i]), "reference%d", i);
		if (i & 1)
			map_put(&map, keys[i], (void *)(uintptr_t)(i + 1));
		else
			map_put_ref(&map, keys[i], (void *)(uintptr_t)(i + 1));
	}
	map_reserve(&map, 500);
	for (int i = 0; i < 160; i += 3)
		map_delete(&map, keys[i]);
	for (int i = 0; i < 160; i++)
		if ((size_t)(uintptr_t)map_get(&map, keys[i]) !=
		    (i % 3 ? (size_t)i + 1 : 0))
			return 0;
	char *first = copy("changing"), *second = copy("changing");
	map_put_ref(&map, first, (void *)1);
	map_put_ref(&map, second, (void *)2);
	free(first);
	if (map_get(&map, "changing") != (void *)2)
		return 0;
	map_put(&map, second, (void *)3);
	free(second);
	if (map_get(&map, "changing") != (void *)3)
		return 0;
	map_put_ref(&map, "changing", (void *)4);
	if (map_get(&map, "changing") != (void *)4)
		return 0;
	map_delete(&map, "changing");
	map_free(&map);
	return 1;
}

/* Exercise every hash tail and address alignment with exact-size buffers. */
static int check_map_bytes(void)
{
	Map map = { 0 };
	for (size_t offset = 0; offset < 8; offset++) {
		for (size_t length = 0; length < 80; length++) {
			char *allocation = malloc(offset + length + 1);
			char *key = allocation + offset;
			for (size_t i = 0; i < length; i++)
				key[i] = (char)(1 + (i * 73 + length) % 255);
			key[length] = 0;
			map_put(&map, key, (void *)(uintptr_t)(length + 1));
			free(allocation);
		}
		for (size_t length = 0; length < 80; length++) {
			char *allocation = malloc(offset + length + 1);
			char *key = allocation + offset;
			for (size_t i = 0; i < length; i++)
				key[i] = (char)(1 + (i * 73 + length) % 255);
			key[length] = 0;
			int ok = (size_t)(uintptr_t)map_get(&map, key) ==
				 length + 1;
			map_delete(&map, key);
			ok &= map_get(&map, key) == NULL;
			free(allocation);
			if (!ok) {
				fprintf(stderr,
					"hash byte boundary mismatch: %zu/%zu\n",
					length, offset);
				map_free(&map);
				return 0;
			}
		}
	}
	map_free(&map);
	return 1;
}

static int check_sorting(void)
{
	const char *terms[] = {
		"",	"x",	 "\xc3\xa9", "\xce\xbb", "\xf0\x9f\x90\x95",
		"\x01", "longer"
	};
	for (int run = 0; run < 7; run++)
		for (int mode = -1; mode <= 1; mode++) {
			J *actual = ARR();
			char prefix[514];
			memset(prefix, 'p', sizeof(prefix) - 1);
			prefix[run == 2 ? 513 : 8] = 0;
			for (int i = 0; i < 700; i++) {
				char text[640];
				snprintf(text, sizeof(text), "%s%s%04d", prefix,
					 terms[(i * 17 + run) % 7],
					 run == 3 ? 0 : (i * 31) % 203);
				if (run >= 5)
					snprintf(text, sizeof(text), "%s%04d",
						 prefix,
						 run == 5 ? i : 699 - i);
				ADD(actual, STR(run == 4 ? "" : text));
			}
			J *expected = DUP(actual);
			reference_sort(expected, mode);
			sort_strings(actual, mode);
			int ok = cJSON_Compare(actual, expected, 1);
			DEL(actual);
			DEL(expected);
			if (!ok) {
				fprintf(stderr,
					"large string sort mismatch: run %d mode %d\n",
					run, mode);
				return 0;
			}
		}
	return 1;
}

int main(void)
{
	if (!check_sorting() || !check_maps() || !check_map_bytes() ||
	    !check_map_ownership())
		return 1;
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
