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
	J *content; /* Owned rule content and key, in match order. */
	J *context;
	const char *canon;
	int distance, forward, reverse, rank;
} SpanRule;
typedef struct {
	SpanRule *rules;
	int n;
} SpanList;
struct MatchIndex {
	const J *view;
	/* 1: lookup is not an object; 2: a lookup group is not an array. */
	int lookup_error;
	/* Ascending word counts bound the exact-match windows. */
	int *lengths, length_count;
	Map *words; /* words[n] holds lookup["n"] for n <= DENSE_WORDS */
	int word_cap;
	Map sparse_words; /* decimal word count to Map, above DENSE_WORDS */
	Map prefixes; /* Synonym prefixes ending before whitespace. */
	Map aliases; /* Tokenized synonym to the lookup word it came from. */
	Map fwd, rev, spans, entities, ner;
	int entity_count, live;
};
static void put_first(Map *m, const char *k, void *v)
{
	/* Array members have no key, and GET never finds them; skip them likewise. */
	if (k && !map_get(m, k))
		map_put(m, k, v);
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
	const char *p = s;
	while (*p) {
		const char *at = p;
		uint32_t c = uread(&p);
		if (at > s && uspace(c)) {
			char *prefix = slice(s, (size_t)(at - s));
			map_put(&x->prefixes, prefix, (void *)1);
			free(prefix);
		}
	}
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

static int index_spans(MatchIndex *x, J *rules)
{
	EACH(key, rules) {
		if (!key->string || map_get(&x->spans, key->string))
			continue;
		SpanList *list = calloc(1, sizeof(*list));
		if (!list)
			return 0;
		map_put(&x->spans, key->string, list);
		int n = 0;
		EACH(r, key)
			n++;
		list->rules = calloc((size_t)n + 1, sizeof(*list->rules));
		if (!list->rules)
			return 0;
		EACH(r, key) {
			J *content = GET(r, "content");
			if (!content || !cJSON_IsArray(content))
				continue;
			SpanRule *rule = &list->rules[list->n++];
			rule->content = DUP(content);
			unique(rule->content, key->string);
			sort_strings(rule->content, 1);
			rule->context = GET(r, "context");
			rule->canon = S(GET(r, "canon"));
			J *distance = GET(r, "distance");
			rule->distance = distance ? distance->valueint : 4;
			rule->forward = cJSON_IsTrue(GET(r, "forward"));
			rule->reverse = cJSON_IsTrue(GET(r, "reverse"));
			rule->rank = underscore_count(rule->canon);
		}
	}
	return 1;
}

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
	map_free(&x->prefixes);
	map_free(&x->aliases);
	for (size_t i = 0; i < x->spans.cap; i++)
		if (x->spans.slots[i].key) {
			SpanList *list = x->spans.slots[i].value;
			for (int r = 0; r < list->n; r++)
				DEL(list->rules[r].content);
			free(list->rules);
			free(list);
		}
	map_free(&x->spans);
	map_free(&x->fwd);
	map_free(&x->rev);
	map_free(&x->entities);
	map_free(&x->ner);
	free(x->lengths);
	free(x);
}

MatchIndex *match_index_build(const J *view)
{
	MatchIndex *x = calloc(1, sizeof(*x));
	if (!x)
		return NULL;
	x->view = view;
	J *synonyms = GET(view, "synonyms");
	int ok = index_lookup(x, GET(synonyms, "lookup")) &&
		 index_spans(x, GET(view, "spans"));
	EACH(k, GET(synonyms, "fwd"))
		put_first(&x->fwd, k->string, (void *)1);
	EACH(k, GET(synonyms, "rev"))
		put_first(&x->rev, k->string, k);
	EACH(v, GET(view, "entities")) {
		put_first(&x->entities, S(v), (void *)1);
		x->entity_count++;
	}
	EACH(k, GET(view, "ner"))
		put_first(&x->ner, k->string, k);
	x->live = cJSON_IsTrue(GET(view, "_live"));
	if (!ok) {
		match_index_free(x);
		return NULL;
	}
	return x;
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
	if (*S(GET(token, "normal")) || GET(token, "swaps"))
		return 0;
	const char *text = S(GET(token, "text"));
	while (*text)
		if (!uspace(uread(&text)))
			return 0;
	return 1;
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
	char *s = norm(b.p ? b.p : "", 1, 0);
	free(b.p);
	return s;
}

static void window_matches(ExactWindow *window, int remaining,
			   const MatchIndex *x, int limit)
{
	window->length = window->words = 0;
	if (spacing_token(window->token))
		return;
	Buf b = { 0 };
	int n = 0;
	J *token = window->token;
	for (int length = 1; length <= remaining && n < limit;
	     length++, token = token->next) {
		if (spacing_token(token))
			continue;
		if (n++)
			buf_put(&b, " ");
		buf_put(&b, S(GET(token, "normal")));
		Map *words = words_at(x, n);
		char *s = norm(b.p, 1, 0);
		if (words && words->size &&
		    !(n == 1 && GET(window->token, "swaps")) &&
		    map_get(words, s)) {
			window->length = length;
			window->words = n;
		}
		/* Empty text has no fixed start: trimming a longer window removes its space. */
		int longer = !*s || map_get((Map *)&x->prefixes, s) != NULL;
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

static J *exact(const MatchIndex *x, J *ts, const J *names, mc_error *e)
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
		window_matches(&windows[at], count - at, x, limit);
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
			if (!spacing_token(windows[first].token))
				preceding++;
		}
		for (int i = first; i <= start; i++)
			window_matches(&windows[i], count - i, x, limit);
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
		       int *x, int *y)
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
		if (lo < 0 || hi - lo > r->distance)
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

/* The index of a normal form, added on first sight; -1 if memory runs out. */
static int form_id(Forms *f, const MatchIndex *index, const char *name)
{
	int id = (int)(uintptr_t)map_get(&f->ids, name);
	if (id)
		return id - 1;
	if (f->count == f->cap) {
		int cap = f->cap ? f->cap * 2 : 64;
		Form *forms = realloc(f->forms, (size_t)cap * sizeof(*forms));
		int *keyed = realloc(f->keyed, (size_t)cap * sizeof(*keyed));
		if (forms)
			f->forms = forms;
		if (keyed)
			f->keyed = keyed;
		if (!forms || !keyed)
			return -1;
		f->cap = cap;
	}
	Form *x = &f->forms[f->count];
	x->name = copy(name);
	if (!x->name)
		return -1;
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
static J *spans(const MatchIndex *index, J *ts, const J *names, mc_error *e)
{
	int n = SIZE(ts), applications = 0, word_cap = 0, i = 0;
	Forms f = { 0 };
	int *form = malloc((size_t)(n ? n : 1) * sizeof(*form));
	int *at = malloc((size_t)(n ? n : 1) * sizeof(*at));
	/* Whether each token is whitespace only, and the token at each position. */
	int *spacing = malloc((size_t)(n ? n : 1) * sizeof(*spacing));
	int *token = malloc((size_t)(n ? n : 1) * sizeof(*token));
	Occurrences **words = NULL;
	if (!form || !at || !spacing || !token) {
		fail(e, 1, "Cannot allocate span positions");
		goto done;
	}
	EACH(t, ts) {
		spacing[i] = spacing_token(t);
		if ((form[i++] = form_id(&f, index, S(GET(t, "normal")))) < 0) {
			fail(e, 1, "Cannot allocate span positions");
			goto done;
		}
	}
	while (!e->code) {
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
		for (int position = i = 0; i < n; i++) {
			if (spacing[i])
				continue;
			Occurrences *o = &f.forms[form[i]].at;
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
				if (valid && span_window(words, k, r, &x, &y)) {
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
		char *l = lower(parts->p ? parts->p : "");
		if (map_get(entities, l))
			return l;
		free(l);
		return NULL;
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

static J *hierarchy(const MatchIndex *index, J *ts, const J *names, mc_error *e)
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
		if (!forms || !multi) {
			free(forms);
			free(multi);
			fail(e, 1, "Cannot allocate hierarchy windows");
			break;
		}
		int at = 0;
		multi[0] = 0;
		EACH(t, ts) {
			forms[at] = ARR();
			surface(t, forms[at]);
			multi[at + 1] = multi[at] + (SIZE(forms[at]) > 1);
			at++;
		}
		for (int n = 9; n >= 2 && !changed; n--) {
			J *first = ts->child;
			for (int i = 0; i + n <= count;
			     i++, first = first->next) {
				if (multi[i + n] == multi[i])
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
		mc_error *e)
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
		ts = exact(x, ts, names, e);
		if (!e->code)
			ts = spans(x, ts, names, e);
		if (!e->code)
			ts = hierarchy(x, ts, names, e);
	}
	if (e->code) {
		DEL(ts);
		return NULL;
	}
	return ts;
}

J *transform_tokens(J *d, const MatchIndex *x, const J *input, const J *names,
		    const char *stage, mc_error *err)
{
	if (!valid_tokens(input, err))
		return NULL;
	J *ts = DUP(input);
	if (!strcmp(stage, "exact"))
		return exact(x, ts, names, err);
	if (!strcmp(stage, "spans"))
		return spans(x, ts, names, err);
	if (!strcmp(stage, "hierarchy"))
		return hierarchy(x, ts, names, err);
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
