/* Compare substring rewrites against v0.5.2 (craigtrim/axiom#129). */
#include "mc.h"
#include "lookup_reference.h"
#include "patterns.h"
static int check(J *fwd)
{
	LookupTrace *trace = NULL;
	J *expected = reference_lookup(fwd),
	  *actual = generate_lookup_traced(fwd, &trace);
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
	LookupState *reused = lookup_state_reuse_traced(fwd, actual, trace);
	lookup_trace_free(trace);
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
/* Raw substring presence and original pattern order are independent of scan direction. */
static int check_patterns(void)
{
	for (int reversed = 0; reversed < 2; reversed++) {
		J *grams = ARR();
		for (int i = 0; i < 64; i++) {
			char term[80];
			snprintf(term, sizeof(term),
				 reversed ? "%03d common ending" :
						  "common beginning %03d",
				 i);
			ADD(grams, STR(term));
		}
		const char *overlap[] = { "aba",
					  "ba",
					  "a",
					  "caba",
					  "\xc3\xa9",
					  "cole",
					  "\xc3\xa9"
					  "cole" };
		for (size_t i = 0; i < sizeof(overlap) / sizeof(*overlap); i++)
			ADD(grams, STR(overlap[i]));
		Patterns *patterns = patterns_build(grams);
		int ok = patterns->reverse == reversed;
		for (int run = 0; run < 120 && ok; run++) {
			Buf text = { 0 };
			for (int i = 0; i < run % 9; i++) {
				buf_put(&text, "x|");
				buf_put(&text,
					patterns->text[(run * 17 + i * 13) %
						       patterns->count]);
			}
			int found = patterns_find(patterns,
						  text.p ? text.p : ""),
			    expected = 0;
			for (int i = 0; i < patterns->count; i++)
				if (strstr(text.p ? text.p : "",
					   patterns->text[i])) {
					if (expected >= found ||
					    patterns->hits[expected] != i)
						ok = 0;
					expected++;
				}
			ok &= expected == found;
			free(text.p);
		}
		patterns_free(patterns);
		DEL(grams);
		if (!ok) {
			fprintf(stderr,
				"pattern direction %d disagrees with strstr\n",
				reversed);
			return 0;
		}
	}
	return 1;
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

static int check_prefixes(void)
{
	const char *terms[] = { "a beta gamma",
				"a betamax delta",
				"aardvark beta gamma",
				"a beta",
				"a  beta gamma",
				"abc def",
				"abacus def",
				"somewhat finish",
				"some word finish",
				"somewhere other end",
				"  a beta gamma",
				"trail ",
				"single",
				"",
				"ab def ghi" };
	int counts[sizeof(terms) / sizeof(*terms)] = { 0 };
	PrefixIndex *index = NULL;
	unsigned random = 129;
	for (int step = 0; step < 1600; step++) {
		random = random * 1664525u + 1013904223u;
		size_t at = (random >> 16) % (sizeof(terms) / sizeof(*terms));
		int delta = counts[at] >= 3 || (counts[at] && (random & 1)) ?
					  -1 :
					  1;
		counts[at] += delta;
		prefix_index_change(&index, terms[at], delta);
		Map expected = { 0 };
		for (size_t i = 0; i < sizeof(terms) / sizeof(*terms); i++) {
			if (!counts[i])
				continue;
			for (const char *space = strchr(terms[i], ' '); space;
			     space = strchr(space + 1, ' ')) {
				if (space == terms[i])
					continue;
				char *key = slice(terms[i],
						  (size_t)(space - terms[i]));
				map_put(&expected, key, (void *)1);
				free(key);
			}
		}
		for (size_t i = 0; i < sizeof(terms) / sizeof(*terms); i++)
			for (size_t n = 0; n <= strlen(terms[i]); n++) {
				char *key = slice(terms[i], n);
				int ok = prefix_index_has(index, key) ==
					 (map_get(&expected, key) != NULL);
				free(key);
				if (!ok) {
					fprintf(stderr,
						"prefix mismatch at step %d term %zu byte %zu\n",
						step, i, n);
					return 0;
				}
			}
		map_free(&expected);
	}
	for (size_t i = 0; i < sizeof(terms) / sizeof(*terms); i++)
		while (counts[i]-- > 0)
			prefix_index_change(&index, terms[i], -1);
	if (prefix_index_has(index, "a") || prefix_index_has(index, "some"))
		return 0;
	/* Grow across many shared branches, then delete and reuse their nodes. */
	for (int i = 0; i < 400; i++) {
		char word[80];
		snprintf(word, sizeof(word), "branch%03d middle tail", i);
		prefix_index_change(&index, word, 1);
	}
	for (int i = 0; i < 400; i += 2) {
		char word[80];
		snprintf(word, sizeof(word), "branch%03d middle tail", i);
		prefix_index_change(&index, word, -1);
		snprintf(word, sizeof(word), "branch%03d modified tail", i);
		prefix_index_change(&index, word, 1);
	}
	for (int i = 0; i < 400; i++) {
		char key[80], word[96];
		snprintf(key, sizeof(key), "branch%03d", i);
		if (!prefix_index_has(index, key))
			return 0;
		snprintf(key, sizeof(key), "branch%03d middle", i);
		if (prefix_index_has(index, key) != (i & 1))
			return 0;
		snprintf(key, sizeof(key), "branch%03d modified", i);
		if (prefix_index_has(index, key) != !(i & 1))
			return 0;
		snprintf(word, sizeof(word), "branch%03d %s tail", i,
			 i & 1 ? "middle" : "modified");
		prefix_index_change(&index, word, -1);
	}
	if (prefix_index_has(index, "branch399"))
		return 0;
	const char *accent = "\xc3\xa9"
			     "cole\xc2\xa0"
			     "du\xe2\x80\x83"
			     "nord";
	const char *other = "\xc3\xaa"
			    "cole sud";
	prefix_index_change(&index, accent, 1);
	prefix_index_change(&index, other, 1);
	if (!prefix_index_has(index, "\xc3\xa9"
				     "cole") ||
	    !prefix_index_has(index, "\xc3\xa9"
				     "cole\xc2\xa0"
				     "du") ||
	    !prefix_index_has(index, "\xc3\xaa"
				     "cole"))
		return 0;
	prefix_index_change(&index, accent, -1);
	if (prefix_index_has(index, "\xc3\xa9"
				    "cole") ||
	    !prefix_index_has(index, "\xc3\xaa"
				     "cole"))
		return 0;
	prefix_index_change(&index, other, -1);
	prefix_index_free(index);
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

/* Split compressed boundary paths, remove owners across several storage
 * blocks, and reuse links while matching partial edges (axiom#129). */
static int check_posting_edits(void)
{
	J *fwd = OBJ();
	for (int i = 0; i < 640; i++) {
		char key[32], term[160];
		snprintf(key, sizeof(key), "posting%d", i);
		snprintf(term, sizeof(term),
			 "stem%03dlongtail bridge finish%03d", i, i);
		J *values = ARR();
		ADD(values, STR(i == 0 ? "tail bridge finish" :
				i == 1 ? "longtail bridge finishline" :
				i == 2 ? "tail bridgelong finishlong" :
					       term));
		PUT(fwd, key, values);
	}
	LookupTrace *trace = NULL;
	J *actual = generate_lookup_traced(fwd, &trace);
	LookupState *state = lookup_state_reuse_traced(fwd, actual, trace);
	lookup_trace_free(trace);
	int ok = 1;
	for (int step = 0; step < 80 && ok; step++) {
		char key[32], term[160];
		snprintf(key, sizeof(key), "posting%d",
			 (step % 4) * 160 + (step / 4) % 16);
		J *old = OBJ();
		PUT(old, key, DUP(GET(fwd, key)));
		lookup_state_entity(state, old, -1);
		DEL(old);
		J *replacement = OBJ(), *values = ARR();
		snprintf(term, sizeof(term),
			 "replacement%03dlongtail bridge finish%03d", step,
			 step);
		ADD(values, STR(step % 4 ? term : "tail bridge finish"));
		ADD(values, STR(step % 3 ? "longtail bridgelong finishline" :
						 "tail bridge finishlong"));
		PUT(replacement, key, values);
		set(fwd, key, DUP(values));
		lookup_state_entity(state, replacement, 1);
		DEL(replacement);
		lookup_state_flush(state);
		J *expected = reference_lookup(fwd);
		ok = cJSON_Compare(actual, expected, 1);
		DEL(expected);
		if (!ok)
			fprintf(stderr,
				"compressed posting mismatch after edit %d\n",
				step);
	}
	lookup_state_free(state);
	DEL(actual);
	DEL(fwd);
	return ok;
}

int main(void)
{
	if (!check_patterns() || !check_sorting() || !check_maps() ||
	    !check_map_bytes() || !check_map_ownership() || !check_prefixes() ||
	    !check_posting_edits())
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
	const char *keys[] = { "_red_green",
			       "red__green_blue",
			       "red_green_",
			       "and_red_green",
			       "red_of_green",
			       "red_green_and",
			       "red_green_blue_amber_violet_black",
			       "___" };
	for (size_t i = 0; i < sizeof(keys) / sizeof(*keys); i++) {
		J *fwd = OBJ(), *values = ARR();
		ADD(values, STR("bred green blues"));
		PUT(fwd, keys[i], values);
		int ok = check(fwd);
		DEL(fwd);
		if (!ok)
			return 1;
	}
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
