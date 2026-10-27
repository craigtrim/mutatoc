/*
 * match.c - Indexed ontology matching and token transforms.
 *
 * Reuses view indexes while preserving match order and token history.
 * craigtrim/mutatoc#1
 */

#include "mc.h"
#include <limits.h>

static int underscore_count(const char *s)
{
	int n = 0;
	for (; *s; s++)
		n += *s == '_';
	return n;
}

/*
 * cJSON objects are linked lists, so every GET on a large ontology view is a
 * linear scan. The index hashes each view member that matching reads, once per
 * loaded view. Lookups keep cJSON's first-key-wins behavior for duplicate keys.
 */
#define DENSE_WORDS 4096
typedef struct {
	J *content; /* Owned rule content and key, folded, in match order. */
	J *context; /* Owned and folded, or NULL. */
	char *origin; /* Owned: the unfolded key the rule is stored under. */
	const char *canon;
	int distance, forward, reverse, rank;
} SpanRule;
typedef struct {
	SpanRule *rules;
	int n, cap;
} SpanList;
typedef struct MutableWord MutableWord;
typedef struct MutableWordBlock MutableWordBlock;
struct MatchIndex {
	const J *view;
	/* 1: lookup is not an object; 2: a lookup group is not an array. */
	int lookup_error;
	/* Ascending word counts bound the exact-match windows. */
	int *lengths, length_count;
	Map *words; /* words[n] holds lookup["n"] for n <= DENSE_WORDS */
	int word_cap;
	Map sparse_words; /* decimal word count to Map, above DENSE_WORDS */
	PrefixIndex *prefixes; /* Synonym prefixes ending before whitespace. */
	Map aliases; /* Tokenized synonym to the lookup word it came from. */
	Map fwd, rev, spans, entities, ner;
	int entity_count, live;
	Map mutable_words, mutable_aliases, group_ranks, ambiguous_aliases;
	int mutable_ready;
	MutableWordBlock *word_blocks;
	MutableWord *available_words;
};
static void put_first(Map *m, const char *k, void *v, int borrow)
{
	/* Array members have no key, and GET never finds them; skip them likewise. */
	if (k && !map_get(m, k)) {
		if (borrow)
			map_put_ref(m, k, v);
		else
			map_put(m, k, v);
	}
}

static int cmp_int(const void *a, const void *b)
{
	int x = *(const int *)a, y = *(const int *)b;
	return (x > y) - (x < y);
}

static Map *words_at(const MatchIndex *x, int n)
{
	if (n <= x->word_cap)
		return &x->words[n];
	if (n <= DENSE_WORDS)
		return NULL;
	char key[16];
	snprintf(key, sizeof(key), "%d", n);
	return map_get((Map *)&x->sparse_words, key);
}

static int add_length(MatchIndex *x, int n, int *cap)
{
	int *grown = realloc(x->lengths,
			     (size_t)(x->length_count + 1) * sizeof(*grown));
	if (!grown)
		return 0;
	x->lengths = grown;
	x->lengths[x->length_count++] = n;
	if (n <= DENSE_WORDS && n > *cap)
		*cap = n;
	return 1;
}

static void add_prefixes(MatchIndex *x, const char *s)
{
	prefix_index_change(&x->prefixes, s, 1);
}

/*
 * Input text is tokenized and synonyms are not, so a synonym written with
 * punctuation never equals the window text of its own tokens. Each one is also
 * indexed under that window text, resolving back to the lookup word it came
 * from. An alias never displaces a lookup word, and the first lookup word to
 * claim an alias keeps it (craigtrim/mutatoc#5).
 */
typedef struct {
	char *key;
	const char *word;
	int words;
} Alias;

static int index_words(MatchIndex *x, J *lookup, int cap, const Alias *aliases,
		       int alias_count)
{
	if (x->length_count > 1)
		qsort(x->lengths, (size_t)x->length_count, sizeof(*x->lengths),
		      cmp_int);
	int unique_count = 0;
	for (int i = 0; i < x->length_count; i++)
		if (!unique_count ||
		    x->lengths[unique_count - 1] != x->lengths[i])
			x->lengths[unique_count++] = x->lengths[i];
	x->length_count = unique_count;
	x->words = calloc((size_t)cap + 1, sizeof(*x->words));
	if (!x->words)
		return 0;
	x->word_cap = cap;
	for (int i = 0; i < x->length_count; i++) {
		int n = x->lengths[i];
		char key[16];
		snprintf(key, sizeof(key), "%d", n);
		Map *words = &x->words[n];
		if (n > DENSE_WORDS) {
			words = calloc(1, sizeof(*words));
			if (!words)
				return 0;
			map_put(&x->sparse_words, key, words);
		}
		EACH(word, GET(lookup, key))
			map_put(words, S(word), (void *)1);
	}
	/*
	 * A window's normalized text is a prefix of every longer window from
	 * the same token, ending before whitespace: norm() lowercases and
	 * trims, and neither the final-sigma rule nor trimming looks past the
	 * space that joins tokens. A window whose text is not such a prefix of
	 * any synonym therefore cannot grow into one.
	 */
	EACH(group, lookup) {
		EACH(word, group)
			add_prefixes(x, S(word));
	}
	for (int i = 0; i < alias_count; i++) {
		const Alias *a = &aliases[i];
		Map *words = words_at(x, a->words);
		if (map_get(words, a->key) || map_get(&x->aliases, a->key))
			continue;
		map_put(words, a->key, (void *)1);
		map_put(&x->aliases, a->key, (void *)a->word);
		add_prefixes(x, a->key);
	}
	return 1;
}

static int index_lookup(MatchIndex *x, J *lookup)
{
	if (!lookup)
		return 1;
	if (!cJSON_IsObject(lookup)) {
		x->lookup_error = 1;
		return 1;
	}
	int cap = 0;
	EACH(group, lookup) {
		if (!cJSON_IsArray(group)) {
			x->lookup_error = 2;
			return 1;
		}
		char *end;
		long n = strtol(group->string, &end, 10);
		if (*end || n <= 0 || n > INT_MAX || !SIZE(group))
			continue;
		if (!add_length(x, (int)n, &cap))
			return 0;
	}
	Alias *aliases = NULL;
	int alias_count = 0, ok = 1;
	EACH(group, lookup) {
		EACH(word, group) {
			char *key = cJSON_IsString(word) ?
					    tokenize_key(S(word)) :
					    NULL;
			if (!key)
				continue;
			int words = 1;
			for (const char *p = key; *p; p++)
				words += *p == ' ';
			Alias *grown =
				realloc(aliases, (size_t)(alias_count + 1) *
							 sizeof(*grown));
			if (grown)
				aliases = grown;
			if (!grown || !add_length(x, words, &cap)) {
				free(key);
				ok = 0;
				break;
			}
			aliases[alias_count++] = (Alias){ key, S(word), words };
		}
		if (!ok)
			break;
	}
	if (ok)
		ok = index_words(x, lookup, cap, aliases, alias_count);
	for (int i = 0; i < alias_count; i++)
		free(aliases[i].key);
	free(aliases);
	return ok;
}

/* Replaces each string in a with its folded form, keeping the order. */
static void fold_strings(J *a)
{
	EACH(v, a) {
		char *owned;
		ufold_view(S(v), &owned);
		if (owned) {
			cJSON_SetValuestring(v, owned);
			free(owned);
		}
	}
}

static void rule_free(SpanRule *r)
{
	DEL(r->content);
	DEL(r->context);
	free(r->origin);
}

/* Removes the empty strings that folding leaves for invisible-only words. */
static void drop_empty(J *a)
{
	J *v = a ? a->child : NULL;
	while (v) {
		J *next = v->next;
		if (!*S(v))
			DEL(cJSON_DetachItemViaPointer(a, v));
		v = next;
	}
}

/*
 * Keys that fold alike share a list ordered by unfolded key, so replacing one
 * key's rules in an update leaves the order a fresh load builds.
 */
static int place_rule(MatchIndex *x, const char *name, const SpanRule *rule)
{
	SpanList *list = map_get(&x->spans, name);
	if (!list) {
		list = calloc(1, sizeof(*list));
		if (!list)
			return 0;
		map_put(&x->spans, name, list);
	}
	if (list->n == list->cap) {
		int cap = list->cap ? list->cap * 2 : 4;
		SpanRule *grown =
			realloc(list->rules, (size_t)cap * sizeof(*grown));
		if (!grown)
			return 0;
		list->rules = grown;
		list->cap = cap;
	}
	int at = list->n;
	while (at > 0 && strcmp(list->rules[at - 1].origin, rule->origin) > 0)
		at--;
	memmove(list->rules + at + 1, list->rules + at,
		(size_t)(list->n - at) * sizeof(*list->rules));
	list->rules[at] = *rule;
	list->n++;
	return 1;
}

/*
 * Adds the rules stored under one unfolded key to the list its folded form
 * keys, so a rule written with accents matches text without them. Content
 * and context are folded too, a word that folds away is no longer required,
 * and content keeps the order of its unfolded words, which the direction
 * flags read. A key that folds away hands its rules to their first remaining
 * word (craigtrim/mutatoc#25).
 */
static int add_span_rules(MatchIndex *x, const char *key, J *rules)
{
	char *name = ufold(key);
	int ok = 1;
	EACH(r, rules) {
		J *content = GET(r, "content");
		if (!content || !cJSON_IsArray(content))
			continue;
		SpanRule rule = { 0 };
		rule.content = DUP(content);
		unique(rule.content, key);
		sort_strings(rule.content, 1);
		int words = SIZE(rule.content);
		fold_strings(rule.content);
		drop_empty(rule.content);
		/*
		 * A rule that dropping invisible words leaves with one word only
		 * repeats the exact match its folded synonym already makes, and
		 * two such rules could trade one token back and forth.
		 */
		int left = SIZE(rule.content);
		if (!left || (left < 2 && words >= 2)) {
			DEL(rule.content);
			continue;
		}
		rule.context = GET(r, "context") ? DUP(GET(r, "context")) :
						   NULL;
		fold_strings(rule.context);
		drop_empty(rule.context);
		rule.origin = copy(key);
		rule.canon = S(GET(r, "canon"));
		J *distance = GET(r, "distance");
		rule.distance = distance ? distance->valueint : 4;
		rule.forward = cJSON_IsTrue(GET(r, "forward"));
		rule.reverse = cJSON_IsTrue(GET(r, "reverse"));
		rule.rank = underscore_count(rule.canon);
		if (!place_rule(x, *name ? name : S(rule.content->child),
				&rule)) {
			rule_free(&rule);
			ok = 0;
			break;
		}
	}
	free(name);
	return ok;
}

static int index_spans(MatchIndex *x, J *rules)
{
	map_reserve(&x->spans, x->spans.size + (size_t)SIZE(rules));
	Map seen = { 0 };
	int ok = 1;
	EACH(key, rules) {
		if (!key->string || map_get(&seen, key->string))
			continue;
		map_put(&seen, key->string, (void *)1);
		if (!add_span_rules(x, key->string, key)) {
			ok = 0;
			break;
		}
	}
	map_free(&seen);
	return ok;
}

static void mutable_free(MatchIndex *x);

void match_index_free(MatchIndex *x)
{
	if (!x)
		return;
	for (int n = 0; x->words && n <= x->word_cap; n++)
		map_free(&x->words[n]);
	free(x->words);
	for (size_t i = 0; i < x->sparse_words.cap; i++)
		if (x->sparse_words.slots[i].key) {
			map_free(x->sparse_words.slots[i].value);
			free(x->sparse_words.slots[i].value);
		}
	map_free(&x->sparse_words);
	prefix_index_free(x->prefixes);
	x->prefixes = NULL;
	map_free(&x->aliases);
	for (size_t i = 0; i < x->spans.cap; i++)
		if (x->spans.slots[i].key) {
			SpanList *list = x->spans.slots[i].value;
			for (int r = 0; r < list->n; r++)
				rule_free(&list->rules[r]);
			free(list->rules);
			free(list);
		}
	map_free(&x->spans);
	map_free(&x->fwd);
	map_free(&x->rev);
	map_free(&x->entities);
	map_free(&x->ner);
	mutable_free(x);
	free(x->lengths);
	free(x);
}

static MatchIndex *build_index(const J *view, int mutable)
{
	MatchIndex *x = calloc(1, sizeof(*x));
	if (!x)
		return NULL;
	x->view = view;
	J *synonyms = GET(view, "synonyms");
	map_reserve(&x->fwd, (size_t)SIZE(GET(synonyms, "fwd")));
	map_reserve(&x->rev, (size_t)SIZE(GET(synonyms, "rev")));
	map_reserve(&x->entities, (size_t)SIZE(GET(view, "entities")));
	map_reserve(&x->ner, (size_t)SIZE(GET(view, "ner")));
	int ok = 1;
	if (mutable)
		match_index_mutable(x);
	else
		ok = index_lookup(x, GET(synonyms, "lookup"));
	ok = ok && index_spans(x, GET(view, "spans"));
	EACH(k, GET(synonyms, "fwd"))
		put_first(&x->fwd, k->string, (void *)1, 0);
	EACH(k, GET(synonyms, "rev"))
		put_first(&x->rev, k->string, k, 1);
	/*
	 * Folded entity names lead back to the first entity that folds to
	 * them (craigtrim/mutatoc#25).
	 */
	EACH(v, GET(view, "entities")) {
		char *owned;
		const char *name = ufold_view(S(v), &owned);
		put_first(&x->entities, name, (void *)S(v), !owned);
		free(owned);
		x->entity_count++;
	}
	EACH(k, GET(view, "ner"))
		put_first(&x->ner, k->string, k, 1);
	x->live = cJSON_IsTrue(GET(view, "_live"));
	if (!ok) {
		match_index_free(x);
		return NULL;
	}
	return x;
}

MatchIndex *match_index_build(const J *view)
{
	return build_index(view, 0);
}
MatchIndex *match_index_build_mutable(const J *view)
{
	return build_index(view, 1);
}

const J *match_index_view(const MatchIndex *x)
{
	return x ? x->view : NULL;
}

static J *swap(J *ts, int start, int end, const char *canon, const char *kind,
	       const J *names, J *ner, double confidence)
{
	J *r = OBJ(), *history = OBJ(), *originals = ARR();
	Buf text = { 0 };
	J *first = AT(ts, start), *t = first, *last = first;
	for (int i = start; i < end; i++, t = t->next) {
		ADD(originals, DUP(t));
		char *s = norm(S(GET(t, "text")), 0, 0);
		if (i > start)
			buf_put(&text, " ");
		buf_put(&text, s);
		free(s);
		last = t;
	}
	PUT(r, "id", DUP(GET(first, "id")));
	PUT(r, "x", DUP(GET(first, "x")));
	PUT(r, "y", DUP(GET(last, "y")));
	if (cJSON_IsString(ner) && *S(ner)) {
		char *value = upper(S(ner));
		PUT(r, "ner", STR(value));
		free(value);
	} else
		PUT(r, "ner", ner ? DUP(ner) : NIL());
	PUT(r, "text", STR(text.p ? text.p : ""));
	PUT(r, "normal", STR(canon));
	free(text.p);
	PUT(history, "tokens", originals);
	PUT(history, "canon", STR(canon));
	PUT(history, "type", STR(kind));
	PUT(history, "ontologies", DUP(names));
	PUT(history, "confidence", NUM(confidence));
	PUT(r, "swaps", history);
	return r;
}

static J *collapse(J *ts, int start, int end, J *r)
{
	/*
	 * The replacement already owns copies of its history. Keep every
	 * unrelated token in place instead of copying the entire document for
	 * every match.
	 */
	J *first = AT(ts, start), *token = first->next;
	for (int i = start + 1; i < end; i++) {
		J *next = token->next;
		DEL(cJSON_DetachItemViaPointer(ts, token));
		token = next;
	}
	cJSON_ReplaceItemViaPointer(ts, first, r);
	return ts;
}

typedef struct {
	J *token;
	int length;
	int words;
} ExactWindow;
static int spacing_token(J *token)
{
	if (GET(token, "swaps"))
		return 0;
	/*
	 * Joiners are as invisible as whitespace, and a normal counts as empty
	 * when it folds away, so tokens prepared before folding agree with new
	 * ones. Folding never removes ASCII (craigtrim/mutatoc#25).
	 */
	const char *normal = S(GET(token, "normal"));
	if ((unsigned char)*normal >= 0x80) {
		char *owned;
		int empty = !*ufold_view(normal, &owned);
		free(owned);
		if (!empty)
			return 0;
	} else if (*normal)
		return 0;
	const char *text = S(GET(token, "text"));
	while (*text) {
		uint32_t c = uread(&text);
		if (!uspace(c) && !ujoiner(c))
			return 0;
	}
	return 1;
}

/*
 * Window text compares folded whoever produced the tokens, so tokens prepared
 * before folding still match (craigtrim/mutatoc#25). Takes ownership of s.
 */
static char *folded(char *s)
{
	char *owned;
	ufold_view(s, &owned);
	if (!owned)
		return s;
	free(s);
	return owned;
}

/* Mandatory line breaks in whitespace tokens (craigtrim/mutatoc#19). */
static int line_boundary(J *token)
{
	if (!spacing_token(token))
		return 0;
	const char *text = S(GET(token, "text"));
	while (*text) {
		uint32_t c = uread(&text);
		if ((c >= 10 && c <= 13) || c == 0x85 ||
		    c == 0x2028 || c == 0x2029)
			return 1;
	}
	return 0;
}

static char *exact_sequence(J *token, int length)
{
	Buf b = { 0 };
	int words = 0;
	for (int i = 0; i < length; i++, token = token->next) {
		if (spacing_token(token))
			continue;
		if (words++)
			buf_put(&b, " ");
		buf_put(&b, S(GET(token, "normal")));
	}
	char *s = folded(norm(b.p ? b.p : "", 1, 0));
	free(b.p);
	return s;
}

static void window_matches(ExactWindow *window, int remaining,
			   const MatchIndex *x, int limit, int line_breaks)
{
	window->length = window->words = 0;
	if (spacing_token(window->token))
		return;
	Buf b = { 0 };
	int n = 0;
	J *token = window->token;
	for (int length = 1; length <= remaining && n < limit;
	     length++, token = token->next) {
		if (line_breaks && line_boundary(token))
			break;
		if (spacing_token(token))
			continue;
		if (n++)
			buf_put(&b, " ");
		buf_put(&b, S(GET(token, "normal")));
		Map *words = words_at(x, n);
		char *s = folded(norm(b.p, 1, 0));
		if (words && words->size &&
		    !(n == 1 && GET(window->token, "swaps")) &&
		    map_get(words, s)) {
			window->length = length;
			window->words = n;
		}
		/* Empty text has no fixed start: trimming a longer window removes its space. */
		int longer = !*s || prefix_index_has(x->prefixes, s);
		free(s);
		if (!longer)
			break;
	}
	free(b.p);
}

static J *canonical(const MatchIndex *x, const char *s)
{
	if (map_get((Map *)&x->fwd, s))
		return STR(s);
	J *v = map_get((Map *)&x->rev, s);
	if (v)
		return STR(cJSON_IsArray(v) ? S(v->child) : S(v));
	char *r = replace(s, "_", " ");
	v = map_get((Map *)&x->rev, r);
	free(r);
	if (v)
		return STR(cJSON_IsArray(v) ? S(v->child) : S(v));
	if (strchr(s, ' ') || strchr(s, '\'')) {
		char *a = replace(s, " ", "_"), *tmp = replace(a, "'", ""),
		     *b = norm(tmp, 1, 0);
		free(tmp);
		free(a);
		J *out = strcmp(s, b) ? canonical(x, b) : NIL();
		free(b);
		return out;
	}
	return NIL();
}

static J *exact(const MatchIndex *x, J *ts, const J *names,
		int line_breaks, mc_error *e)
{
	if (x->lookup_error == 1) {
		fail(e, 2, "Synonym lookup must be an object");
		return ts;
	}
	if (x->lookup_error == 2) {
		fail(e, 2, "Synonym lookup groups must be arrays");
		return ts;
	}
	int count = SIZE(ts), operations = 0, at = 0, limit = 0;
	/*
	 * Dotted abbreviations consume punctuation tokens too. Derive the
	 * window bound from this ontology, instead of silently discarding long
	 * synonyms.
	 */
	for (int i = x->length_count - 1; i >= 0 && !limit; i--)
		if (x->lengths[i] <= count)
			limit = x->lengths[i];
	if (!limit)
		return ts;
	ExactWindow *windows = malloc((size_t)count * sizeof(*windows));
	if (!windows) {
		fail(e, 1, "Cannot allocate matching windows");
		return ts;
	}
	EACH(token, ts) {
		windows[at].token = token;
		window_matches(&windows[at], count - at, x, limit, line_breaks);
		at++;
	}
	while (!e->code) {
		int start = -1, length = 0, best = 0;
		/* Preserve the original longest-window, then leftmost selection order. */
		for (int i = 0; i < count; i++)
			if (windows[i].words > best) {
				start = i;
				length = windows[i].length;
				best = windows[i].words;
			}
		if (start < 0)
			break;
		char *s = exact_sequence(windows[start].token, length);
		J *canon = canonical(x, s);
		const char *alias = cJSON_IsString(canon) ?
					    NULL :
					    map_get((Map *)&x->aliases, s);
		if (alias) {
			DEL(canon);
			canon = canonical(x, alias);
		}
		if (!canon || !cJSON_IsString(canon)) {
			fail(e, 4, "Canonical form not found for %s", s);
			DEL(canon);
			free(s);
			break;
		}
		J *r = swap(ts, start, start + length, S(canon), "exact", names,
			    NULL, 100.0);
		ts = collapse(ts, start, start + length, r);
		DEL(canon);
		free(s);
		memmove(windows + start + 1, windows + start + length,
			(size_t)(count - start - length) * sizeof(*windows));
		count -= length - 1;
		windows[start].token = r;
		/*
		 * Only windows containing the replacement can change. Count
		 * visible tokens backward, retaining intervening whitespace in
		 * swap history.
		 */
		int first = start, preceding = 0;
		while (first > 0 && preceding < limit - 1) {
			first--;
			if (line_breaks && line_boundary(windows[first].token))
				break;
			if (!spacing_token(windows[first].token))
				preceding++;
		}
		for (int i = first; i <= start; i++)
			window_matches(&windows[i], count - i, x, limit,
				       line_breaks);
		if (++operations > 100000) {
			fail(e, 4, "Matching operation limit exceeded");
			break;
		}
	}
	free(windows);
	return ts;
}

/* Ascending token positions of one normal form. */
typedef struct {
	int *at, n;
} Occurrences;

/* The first position in o at or after v, or -1 when there is none. */
static int occurrence_from(const Occurrences *o, int v)
{
	int lo = 0, hi = o->n;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;
		if (o->at[mid] < v)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo < o->n ? o->at[lo] : -1;
}

/*
 * Chooses one occurrence of every content word so that the chosen positions
 * lie as close together as possible, preferring the leftmost window on ties.
 * The distance bounds every chosen position, not only the first and last
 * content words, and a repeated word may use any of its occurrences. The
 * direction flags keep their meaning over the length-sorted content: forward
 * lets the first content word follow the last, and reverse lets it precede
 * the last. craigtrim/mutatoc#9
 */
static int span_window(Occurrences *const *words, int k, const SpanRule *r,
		       const int *line, int *x, int *y)
{
	const Occurrences *a = words[0], *b = words[k - 1];
	int found = 0, tightest = -1;
	if (a != b && !r->forward && !r->reverse)
		return 0;
	/*
	 * No window is tighter than one position per distinct word. Starts are
	 * taken in ascending order, so the first window that tight which begins
	 * at its own start is the leftmost of the tightest. craigtrim/mutatoc#11
	 */
	for (int w = 0; w < k; w++) {
		int seen = 0;
		for (int v = 0; v < w && !seen; v++)
			seen = words[v] == words[w];
		tightest += !seen;
	}
	for (int s = -1;;) {
		int next = -1;
		for (int w = 0; w < k; w++) {
			int p = occurrence_from(words[w], s + 1);
			if (p >= 0 && (next < 0 || p < next))
				next = p;
		}
		if (next < 0)
			break;
		s = next;
		int pa, pb;
		/* One word in both places is never out of order. */
		if (a == b) {
			pa = pb = s;
		} else if (r->forward && r->reverse) {
			pa = occurrence_from(a, s);
			pb = occurrence_from(b, s);
		} else if (r->reverse) {
			pa = occurrence_from(a, s);
			pb = pa < 0 ? -1 : occurrence_from(b, pa + 1);
		} else {
			pb = occurrence_from(b, s);
			pa = pb < 0 ? -1 : occurrence_from(a, pb + 1);
		}
		if (pa < 0 || pb < 0)
			continue;
		int lo = pa < pb ? pa : pb, hi = pa < pb ? pb : pa;
		for (int j = 1; j < k - 1 && lo >= 0; j++) {
			int p = occurrence_from(words[j], s);
			if (p < 0)
				lo = -1;
			else if (p < lo)
				lo = p;
			else if (p > hi)
				hi = p;
		}
		if (lo < 0 || hi - lo > r->distance ||
		    (line && line[lo] != line[hi]))
			continue;
		if (!found || hi - lo < *y - *x ||
		    (hi - lo == *y - *x && lo < *x)) {
			*x = lo;
			*y = hi;
			found = 1;
		}
		if (*y - *x == tightest && *x == s)
			break;
	}
	return found;
}

/* One distinct normal form in the token stream and its current positions. */
typedef struct {
	char *name;
	const SpanList *rules; /* the span rules this form keys, or NULL */
	Occurrences at;
} Form;

typedef struct {
	Map ids; /* normal form to its index plus one */
	Form *forms;
	int count, cap;
	int *keyed, keyed_count; /* forms that key span rules, in byte order */
} Forms;

/*
 * The index of a normal form, added on first sight; -1 if memory runs out.
 * Forms are folded like the span rules they key (craigtrim/mutatoc#25).
 */
static int form_id(Forms *f, const MatchIndex *index, const char *unfolded)
{
	char *owned;
	const char *name = ufold_view(unfolded, &owned);
	int id = (int)(uintptr_t)map_get(&f->ids, name);
	if (id) {
		free(owned);
		return id - 1;
	}
	if (f->count == f->cap) {
		int cap = f->cap ? f->cap * 2 : 64;
		Form *forms = realloc(f->forms, (size_t)cap * sizeof(*forms));
		int *keyed = realloc(f->keyed, (size_t)cap * sizeof(*keyed));
		if (forms)
			f->forms = forms;
		if (keyed)
			f->keyed = keyed;
		if (!forms || !keyed) {
			free(owned);
			return -1;
		}
		f->cap = cap;
	}
	Form *x = &f->forms[f->count];
	x->name = owned ? owned : copy(name);
	if (!x->name)
		return -1;
	name = x->name;
	x->rules = map_get((Map *)&index->spans, name);
	x->at = (Occurrences){ 0 };
	map_put(&f->ids, name, (void *)(uintptr_t)(f->count + 1));
	if (x->rules) {
		int at = f->keyed_count;
		while (at > 0 &&
		       strcmp(f->forms[f->keyed[at - 1]].name, name) > 0)
			at--;
		memmove(f->keyed + at + 1, f->keyed + at,
			(size_t)(f->keyed_count - at) * sizeof(*f->keyed));
		f->keyed[at] = f->count;
		f->keyed_count++;
	}
	return f->count++;
}

static void forms_free(Forms *f)
{
	for (int i = 0; i < f->count; i++)
		free(f->forms[i].name);
	free(f->forms);
	free(f->keyed);
	map_free(&f->ids);
}

/*
 * Applies span rules until none fits, so a text gets every span its words
 * allow rather than one per sweep (craigtrim/mutatoc#11). Each round takes the
 * best-ranked rule's tightest window, as a single pass always did. The forms
 * and their positions are indexed once and updated after each collapse
 * instead of being rebuilt from the token list. Whitespace-only tokens take
 * no position, just as they never interrupt an exact phrase, but a span
 * still covers those between its words (craigtrim/mutatoc#12).
 */
/*
 * A span over a single token changes nothing when that token is already the
 * rule's entity. Folding can make a token's form equal to its own canonical
 * form, so such a span would apply forever (craigtrim/mutatoc#25).
 */
static int no_progress(J *t, const char *canon)
{
	J *swaps = GET(t, "swaps");
	return swaps && !strcmp(S(GET(swaps, "canon")), canon);
}

static J *spans(const MatchIndex *index, J *ts, const J *names,
		int line_breaks, mc_error *e)
{
	int n = SIZE(ts), applications = 0, word_cap = 0, i = 0;
	Forms f = { 0 };
	int *form = malloc((size_t)(n ? n : 1) * sizeof(*form));
	int *at = malloc((size_t)(n ? n : 1) * sizeof(*at));
	/* Whether each token is whitespace only, and the token at each position. */
	int *spacing = malloc((size_t)(n ? n : 1) * sizeof(*spacing));
	int *token = malloc((size_t)(n ? n : 1) * sizeof(*token));
	int *line = line_breaks ?
		malloc((size_t)(n ? n : 1) * sizeof(*line)) : NULL;
	/* Each token by index, refreshed every round (craigtrim/mutatoc#25). */
	J **items = malloc((size_t)(n ? n : 1) * sizeof(*items));
	Occurrences **words = NULL;
	if (!form || !at || !spacing || !token || !items ||
	    (line_breaks && !line)) {
		fail(e, 1, "Cannot allocate span positions");
		goto done;
	}
	EACH(t, ts) {
		spacing[i] = spacing_token(t);
		if (line_breaks && line_boundary(t))
			spacing[i] |= 2;
		if ((form[i++] = form_id(&f, index, S(GET(t, "normal")))) < 0) {
			fail(e, 1, "Cannot allocate span positions");
			goto done;
		}
	}
	while (!e->code) {
		i = 0;
		EACH(t, ts)
			items[i++] = t;
		for (int g = 0; g < f.count; g++)
			f.forms[g].at.n = 0;
		for (i = 0; i < n; i++)
			if (!spacing[i])
				f.forms[form[i]].at.n++;
		for (int g = 0, offset = 0; g < f.count; g++) {
			f.forms[g].at.at = at + offset;
			offset += f.forms[g].at.n;
			f.forms[g].at.n = 0;
		}
		for (int position = i = 0, segment = 0; i < n; i++) {
			if (spacing[i]) {
				segment += (spacing[i] & 2) != 0;
				continue;
			}
			Occurrences *o = &f.forms[form[i]].at;
			if (line)
				line[position] = segment;
			token[position] = i;
			o->at[o->n++] = position++;
		}
		const SpanRule *best = NULL;
		int bx = 0, by = 0, score = -1;
		for (int key = 0; key < f.keyed_count; key++) {
			const Form *keyed = &f.forms[f.keyed[key]];
			if (!keyed->at.n)
				continue;
			for (int r_at = 0; r_at < keyed->rules->n; r_at++) {
				const SpanRule *r = &keyed->rules->rules[r_at];
				int k = SIZE(r->content), valid = k > 0;
				/* A rule that cannot outrank the best one changes nothing. */
				if (!valid || r->rank <= score)
					continue;
				if (k > word_cap) {
					Occurrences **grown = realloc(
						words,
						(size_t)k * sizeof(*words));
					if (!grown) {
						fail(e, 1,
						     "Cannot allocate span positions");
						goto done;
					}
					words = grown;
					word_cap = k;
				}
				int w = 0;
				EACH(v, r->content) {
					int id = (int)(uintptr_t)map_get(&f.ids,
									 S(v));
					if (!id || !f.forms[id - 1].at.n) {
						valid = 0;
						break;
					}
					words[w++] = &f.forms[id - 1].at;
				}
				EACH(v, r->context) {
					int id = (int)(uintptr_t)map_get(&f.ids,
									 S(v));
					if (valid &&
					    (!id || !f.forms[id - 1].at.n))
						valid = 0;
				}
				int x = 0, y = 0;
				valid = valid &&
					span_window(words, k, r, line, &x, &y);
				/*
				 * One position means every word is the same form;
				 * take its first occurrence that changes something.
				 */
				if (valid && x == y) {
					const Occurrences *o = words[0];
					valid = 0;
					for (int j = 0; j < o->n && !valid; j++)
						if (!no_progress(
							    items[token[o->at[j]]],
							    r->canon)) {
							x = y = o->at[j];
							valid = 1;
						}
				}
				if (valid) {
					best = r;
					bx = token[x];
					by = token[y] + 1;
					score = r->rank;
				}
			}
		}
		if (!best)
			break;
		if (++applications > 100000) {
			fail(e, 4, "Matching operation limit exceeded");
			break;
		}
		const char *canon = best->canon;
		int id = form_id(&f, index, canon);
		if (id < 0) {
			fail(e, 1, "Cannot allocate span positions");
			break;
		}
		J *ner = index->live ? STR("NER") :
				       DUP(map_get((Map *)&index->ner, canon));
		J *r = swap(ts, bx, by, canon, "spans", names, ner, 100.0);
		DEL(ner);
		ts = collapse(ts, bx, by, r);
		form[bx] = id;
		spacing[bx] = 0;
		memmove(form + bx + 1, form + by,
			(size_t)(n - by) * sizeof(*form));
		memmove(spacing + bx + 1, spacing + by,
			(size_t)(n - by) * sizeof(*spacing));
		n -= by - bx - 1;
	}
done:
	free(items);
	free(line);
	free(words);
	free(token);
	free(spacing);
	free(at);
	free(form);
	forms_free(&f);
	return ts;
}

static void surface(J *token, J *forms)
{
	unique(forms, S(GET(token, "normal")));
	EACH(v, GET(token, "ancestors"))
		unique(forms, S(v));
	EACH(v, GET(token, "descendants"))
		unique(forms, S(v));
	EACH(t, GET(GET(token, "swaps"), "tokens")) {
		EACH(v, GET(t, "ancestors"))
			unique(forms, S(v));
		EACH(v, GET(t, "descendants"))
			unique(forms, S(v));
	}
	sort_strings(forms, 0);
}

static char *product(J **choices, int count, int at, Buf *parts, Map *entities,
		     size_t *budget)
{
	if (!(*budget)--)
		return NULL;
	if (at == count) {
		/* Lowercase the joined form, not each part, because final sigma is contextual. */
		char *l = folded(lower(parts->p ? parts->p : ""));
		const char *entity = map_get(entities, l);
		free(l);
		return entity ? copy(entity) : NULL;
	}
	size_t mark = parts->n;
	EACH(v, choices[at]) {
		if (at)
			buf_put(parts, "_");
		buf_put(parts, S(v));
		char *r = product(choices, count, at + 1, parts, entities,
				  budget);
		parts->n = mark;
		if (parts->p)
			parts->p[mark] = 0;
		if (r)
			return r;
	}
	return NULL;
}

static J *hierarchy(const MatchIndex *index, J *ts, const J *names,
		    int line_breaks, mc_error *e)
{
	if (!index->entity_count)
		return ts;
	int changed = 1;
	while (changed && !e->code) {
		changed = 0;
		const int count = SIZE(ts);
		/*
		 * Surface forms depend only on the token, so compute them once
		 * per pass instead of once for every window that contains the
		 * token.
		 */
		J **forms =
			malloc((size_t)(count ? count : 1) * sizeof(*forms));
		int *multi = malloc((size_t)(count + 1) * sizeof(*multi));
		int *breaks = line_breaks ?
			malloc((size_t)(count + 1) * sizeof(*breaks)) : NULL;
		if (!forms || !multi || (line_breaks && !breaks)) {
			free(forms);
			free(multi);
			free(breaks);
			fail(e, 1, "Cannot allocate hierarchy windows");
			break;
		}
		int at = 0;
		multi[0] = 0;
		if (breaks)
			breaks[0] = 0;
		EACH(t, ts) {
			forms[at] = ARR();
			surface(t, forms[at]);
			multi[at + 1] = multi[at] + (SIZE(forms[at]) > 1);
			if (breaks)
				breaks[at + 1] = breaks[at] + line_boundary(t);
			at++;
		}
		for (int n = 9; n >= 2 && !changed; n--) {
			J *first = ts->child;
			for (int i = 0; i + n <= count;
			     i++, first = first->next) {
				if (multi[i + n] == multi[i] ||
				    (breaks && breaks[i + n] != breaks[i]))
					continue;
				Buf parts = { 0 };
				size_t budget = 1000000;
				char *canon = product(forms + i, n, 0, &parts,
						      (Map *)&index->entities,
						      &budget);
				free(parts.p);
				if (!budget)
					fail(e, 4,
					     "Hierarchy combination limit exceeded");
				if (canon) {
					/*
					 * Only an earlier match supplies a
					 * label; caller token fields do not.
					 */
					J *r = swap(ts, i, i + n, canon,
						    "hierarchy", names,
						    GET(first, "ner"), 75.0);
					ts = collapse(ts, i, i + n, r);
					changed = 1;
					free(canon);
					break;
				}
			}
		}
		for (int i = 0; i < count; i++)
			DEL(forms[i]);
		free(forms);
		free(multi);
		free(breaks);
	}
	return ts;
}

static int valid_tokens(const J *input, mc_error *e)
{
	if (!cJSON_IsArray(input)) {
		fail(e, 2, "tokens must be an array");
		return 0;
	}
	EACH(t, input) {
		if (!cJSON_IsObject(t) || !GET(t, "id") ||
		    !cJSON_IsNumber(GET(t, "x")) ||
		    !cJSON_IsNumber(GET(t, "y")) ||
		    !cJSON_IsString(GET(t, "text")) ||
		    !cJSON_IsString(GET(t, "normal"))) {
			fail(e, 2,
			     "Each token requires id, numeric x/y, and string text/normal");
			return 0;
		}
	}
	return 1;
}

J *match_tokens(const MatchIndex *x, J *ts, const J *names, int ctr,
		int line_breaks, mc_error *e)
{
	/* Takes ownership of ts, which each request already owns, instead of copying it. */
	if (!valid_tokens(ts, e)) {
		DEL(ts);
		return NULL;
	}
	if (ctr < -1000) {
		DEL(ts);
		fail(e, 4, "Matching recursion limit exceeded");
		return NULL;
	}
	int sweeps = ctr >= 2 ? 1 : 3 - ctr;
	for (int i = 0; i < sweeps && !e->code; i++) {
		ts = exact(x, ts, names, line_breaks, e);
		if (!e->code)
			ts = spans(x, ts, names, line_breaks, e);
		if (!e->code)
			ts = hierarchy(x, ts, names, line_breaks, e);
	}
	if (e->code) {
		DEL(ts);
		return NULL;
	}
	return ts;
}

J *transform_tokens(J *d, const MatchIndex *x, const J *input, const J *names,
		    const char *stage, int line_breaks, mc_error *err)
{
	if (!valid_tokens(input, err))
		return NULL;
	J *ts = DUP(input);
	if (!strcmp(stage, "exact"))
		return exact(x, ts, names, line_breaks, err);
	if (!strcmp(stage, "spans"))
		return spans(x, ts, names, line_breaks, err);
	if (!strcmp(stage, "hierarchy"))
		return hierarchy(x, ts, names, line_breaks, err);
	if (!strcmp(stage, "augment")) {
		EACH(t, ts) {
			const char *text = S(GET(t, "normal")), *p = text;
			int alpha = *p != 0;
			while (*p)
				if (!ualpha(uread(&p)))
					alpha = 0;
			if (!alpha)
				continue;
			J *args = ARR();
			ADD(args, STR(text));
			const char *methods[] = { "ancestors", "descendants" };
			for (size_t i = 0; i < 2; i++) {
				J *values =
					cJSON_IsTrue(GET(d, "_live")) ?
						data_query(d, methods[i], args,
							   NULL, err) :
						ontology_query(d, methods[i],
							       args, err);
				set(t, methods[i], values);
			}
			DEL(args);
			if (err->code) {
				DEL(ts);
				return NULL;
			}
		}
		return ts;
	}
	DEL(ts);
	fail(err, 2, "Unknown token transformation: %s", stage);
	return NULL;
}

char *render(const J *tokens)
{
	Buf b = { 0 };
	EACH(t, tokens) {
		J *sw = GET(t, "swaps");
		char *s = sw ? copy(S(GET(sw, "canon"))) :
			       norm(S(GET(t, "text")), 0, 0);
		if (*s) {
			if (b.n)
				buf_put(&b, " ");
			buf_put(&b, s);
		}
		free(s);
	}
	return buf_take(&b);
}

/* Patch lookup membership and token aliases after edits (craigtrim/axiom#129). */
struct MutableWord {
	const char *text; /* Borrowed from a lookup JSON node until removal. */
	char *alias;
	MutableWord *available;
	int words;
	size_t chars;
};
struct MutableWordBlock {
	MutableWordBlock *next;
	MutableWord words[256];
};
/* Only words with token aliases need separate candidate records. */
static MutableWord *mutable_word_create(MatchIndex *x)
{
	if (!x->available_words) {
		MutableWordBlock *block = calloc(1, sizeof(*block));
		block->next = x->word_blocks;
		x->word_blocks = block;
		for (int i = 255; i >= 0; i--) {
			block->words[i].available = x->available_words;
			x->available_words = &block->words[i];
		}
	}
	MutableWord *word = x->available_words;
	x->available_words = word->available;
	memset(word, 0, sizeof(*word));
	return word;
}
static int mutable_count(const char *s)
{
	int n = 1;
	for (; *s; s++)
		n += *s == ' ';
	return n;
}
/* Literal references count by two; the low bit records the selected alias.
 * Literal presence therefore lives in the existing per-length word index. */
static void counter(Map *map, const char *key, int delta, int borrow)
{
	size_t n = (size_t)(uintptr_t)map_get(map, key);
	size_t amount = (size_t)(delta > 0 ? delta : -delta);
	if (delta > 0 && borrow)
		map_put_ref(map, key, (void *)(uintptr_t)(n + amount));
	else if (delta > 0)
		map_put(map, key, (void *)(uintptr_t)(n + amount));
	else if (n > amount)
		map_put(map, key, (void *)(uintptr_t)(n - amount));
	else
		map_delete(map, key);
}
static Map *mutable_group(MatchIndex *x, int n)
{
	if (n <= DENSE_WORDS && n > x->word_cap) {
		int old = x->word_cap;
		x->words =
			realloc(x->words, ((size_t)n + 1) * sizeof(*x->words));
		memset(x->words + old + 1, 0,
		       (size_t)(n - old) * sizeof(*x->words));
		x->word_cap = n;
	}
	Map *m = words_at(x, n);
	if (!m) {
		char key[32];
		snprintf(key, sizeof(key), "%d", n);
		m = calloc(1, sizeof(*m));
		map_put(&x->sparse_words, key, m);
	}
	int found = 0;
	for (int i = 0; i < x->length_count; i++)
		found |= x->lengths[i] == n;
	if (!found) {
		int cap = x->word_cap;
		add_length(x, n, &cap);
		qsort(x->lengths, (size_t)x->length_count, sizeof(*x->lengths),
		      cmp_int);
	}
	return m;
}
static void mutable_prefixes(MatchIndex *x, const char *s, int delta)
{
	prefix_index_change(&x->prefixes, s, delta);
}

static int mutable_literal(const MatchIndex *x, const char *text)
{
	int words = mutable_count(text);
	Map *index = words_at(x, words);
	if (index && (uintptr_t)map_get(index, text) >= 2)
		return 1;
	/* Also preserve literal precedence for a lookup with unusual group names. */
	for (int i = 0; i < x->length_count; i++) {
		if (x->lengths[i] == words)
			continue;
		index = words_at(x, x->lengths[i]);
		if (index && (uintptr_t)map_get(index, text) >= 2)
			return 1;
	}
	return 0;
}

static void mutable_alias(MatchIndex *x, const char *key)
{
	Map *candidates = map_get(&x->mutable_aliases, key);
	if (!candidates)
		return;
	MutableWord *best = NULL;
	if (!mutable_literal(x, key)) {
		for (size_t i = 0; i < candidates->cap; i++)
			if (candidates->slots[i].key) {
				MutableWord *w = candidates->slots[i].value;
				char wk[32], bk[32];
				snprintf(wk, sizeof(wk), "%d", w->words);
				snprintf(bk, sizeof(bk), "%d",
					 best ? best->words : 0);
				int rank = (int)(uintptr_t)map_get(
					&x->group_ranks, wk);
				int best_rank = (int)(uintptr_t)map_get(
					&x->group_ranks, bk);
				if (!best || rank < best_rank ||
				    (rank == best_rank &&
				     (w->chars > best->chars ||
				      (w->chars == best->chars &&
				       strcmp(w->text, best->text) > 0))))
					best = w;
			}
	}
	int had = map_get(&x->aliases, key) != NULL;
	if (best)
		map_put(&x->aliases, key, (void *)best->text);
	else
		map_delete(&x->aliases, key);
	if (!!best != had) {
		int delta = best ? 1 : -1;
		counter(mutable_group(x, mutable_count(key)), key, delta, 0);
		mutable_prefixes(x, key, delta);
	}
}
static void mutable_word_add(MatchIndex *x, int words, const char *text,
			     int ensure_rank)
{
	char *alias = tokenize_key(text);
	MutableWord *w = NULL;
	if (alias) {
		w = mutable_word_create(x);
		w->text = text;
		w->alias = alias;
		w->words = words;
		w->chars = ulen(text);
	}
	if (ensure_rank) {
		char key[32];
		snprintf(key, sizeof(key), "%d", words);
		if (!map_get(&x->group_ranks, key))
			map_put(&x->group_ranks, key,
				(void *)(uintptr_t)(x->group_ranks.size + 1));
	}
	if (w)
		map_put_ref(&x->mutable_words, text, w);
	counter(mutable_group(x, words), text, 2, 1);
	mutable_prefixes(x, text, 1);
	mutable_alias(x, text);
	if (alias) {
		Map *bucket = map_get(&x->mutable_aliases, alias);
		if (!bucket) {
			bucket = calloc(1, sizeof(*bucket));
			map_put(&x->mutable_aliases, alias, bucket);
		}
		map_put_small(bucket, text, w);
		if (bucket->size > 1)
			map_put(&x->ambiguous_aliases, alias, bucket);
		mutable_alias(x, alias);
	}
}
void match_index_word(void *context, int words, const char *text, int delta)
{
	MatchIndex *x = context;
	if (delta > 0) {
		mutable_word_add(x, words, text, 1);
	} else {
		Map *index = words_at(x, words);
		if (!index || (uintptr_t)map_get(index, text) < 2)
			return;
		MutableWord *w = map_get(&x->mutable_words, text);
		if (w)
			map_delete(&x->mutable_words, text);
		counter(index, text, -2, 0);
		mutable_prefixes(x, text, -1);
		mutable_alias(x, text);
		if (!w)
			return;
		if (w->alias) {
			Map *bucket = map_get(&x->mutable_aliases, w->alias);
			map_delete(bucket, text);
			if (bucket->size < 2)
				map_delete(&x->ambiguous_aliases, w->alias);
			mutable_alias(x, w->alias);
			if (!bucket->size) {
				map_free(bucket);
				free(bucket);
				map_delete(&x->mutable_aliases, w->alias);
			}
		}
		free(w->alias);
		w->available = x->available_words;
		x->available_words = w;
	}
}
void match_index_mutable(MatchIndex *x)
{
	if (x->mutable_ready)
		return;
	x->mutable_ready = 1;
	if (!x->words)
		x->words = calloc(1, sizeof(*x->words));
	for (int n = 0; n <= x->word_cap; n++)
		map_free(&x->words[n]);
	for (size_t i = 0; i < x->sparse_words.cap; i++)
		if (x->sparse_words.slots[i].key)
			map_free(x->sparse_words.slots[i].value);
	map_free(&x->aliases);
	prefix_index_free(x->prefixes);
	x->prefixes = NULL;
	J *lookup = GET(GET(x->view, "synonyms"), "lookup");
	int rank = 0;
	EACH(group, lookup)
		map_put(&x->group_ranks, group->string,
			(void *)(uintptr_t)++rank);
	EACH(group, lookup) {
		int words = atoi(group->string);
		if (group->child) {
			Map *index = mutable_group(x, words);
			map_reserve(index, index->size + (size_t)SIZE(group));
		}
		EACH(word, group)
			mutable_word_add(x, words, S(word), 0);
	}
}
static void mutable_free(MatchIndex *x)
{
	for (size_t i = 0; i < x->mutable_words.cap; i++)
		if (x->mutable_words.slots[i].key) {
			MutableWord *w = x->mutable_words.slots[i].value;
			free(w->alias);
		}
	for (size_t i = 0; i < x->mutable_aliases.cap; i++)
		if (x->mutable_aliases.slots[i].key) {
			Map *m = x->mutable_aliases.slots[i].value;
			map_free(m);
			free(m);
		}
	map_free(&x->mutable_words);
	map_free(&x->mutable_aliases);
	map_free(&x->group_ranks);
	map_free(&x->ambiguous_aliases);
	while (x->word_blocks) {
		MutableWordBlock *next = x->word_blocks->next;
		free(x->word_blocks);
		x->word_blocks = next;
	}
}
void match_index_member(MatchIndex *x, const char *kind, const char *key,
			J *value)
{
	Map *m = !strcmp(kind, "rev") ? &x->rev :
		 !strcmp(kind, "ner") ? &x->ner :
					      &x->spans;
	if (m == &x->spans) {
		/*
		 * The folded list may also hold rules of other keys that fold
		 * alike; replace only this key's. A key that folds away put its
		 * rules under other words, so every list is searched for them
		 * (craigtrim/mutatoc#25).
		 */
		char *name = ufold(key);
		J *names = ARR();
		if (*name)
			ADD(names, STR(name));
		else
			for (size_t i = 0; i < m->cap; i++)
				if (m->slots[i].key)
					ADD(names, STR(m->slots[i].key));
		EACH(n, names) {
			SpanList *list = map_get(m, S(n));
			if (!list)
				continue;
			int kept = 0;
			for (int i = 0; i < list->n; i++) {
				if (!strcmp(list->rules[i].origin, key))
					rule_free(&list->rules[i]);
				else
					list->rules[kept++] = list->rules[i];
			}
			list->n = kept;
			if (!kept) {
				free(list->rules);
				free(list);
				map_delete(m, S(n));
			}
		}
		DEL(names);
		free(name);
		if (value)
			add_span_rules(x, key, value);
	} else if (value)
		map_put(m, key, value);
	else
		map_delete(m, key);
}

void match_index_group_order(MatchIndex *x, J *order)
{
	int rank = 0, changed = 0;
	EACH(key, order) {
		rank++;
		changed |= (int)(uintptr_t)map_get(&x->group_ranks, S(key)) !=
			   rank;
		map_put(&x->group_ranks, S(key), (void *)(uintptr_t)rank);
	}
	if (!changed)
		return;
	/* Only aliases with competing source words can change their winner. */
	for (size_t i = 0; i < x->ambiguous_aliases.cap; i++)
		if (x->ambiguous_aliases.slots[i].key)
			mutable_alias(x, x->ambiguous_aliases.slots[i].key);
}
