/*
 * terms.c - Term matching without an ontology.
 *
 * match_terms finds where each term in a list occurs in a text. A plain term
 * matches as an exact phrase, and an alpha+beta term also as a span rule,
 * exactly as the same text does as an ontology label: the terms become a view
 * through the label builders, and the text runs through the tokenizer and the
 * matching stages unchanged. The engine's own ontology and match index are
 * never read or replaced. craigtrim/mutatoc#23
 */

#include "mc.h"

/*
 * A term as load reads a label literal, lowercased and trimmed, with each
 * part between + trimmed too, since span keys come from those parts as they
 * stand. Terms that read alike are one term. The normal forms of its tokens,
 * joined by underscores, give the words of its canonical form.
 */
static int label(const char *term, Buf *value, Buf *words)
{
	char *copy_of = copy(term);
	if (!copy_of)
		return 0;
	int ok = 1;
	char *part = copy_of;
	for (;;) {
		char *plus = strchr(part, '+');
		if (plus)
			*plus = 0;
		char *trimmed = norm(part, 1, 1);
		J *tokens = trimmed ? tokenize_text(trimmed) : NULL;
		if (!tokens) {
			free(trimmed);
			ok = 0;
			break;
		}
		buf_put(value, trimmed);
		free(trimmed);
		EACH(t, tokens) {
			const char *normal = S(GET(t, "normal"));
			if (!*normal)
				continue;
			if (words->n)
				buf_put(words, "_");
			buf_put(words, normal);
		}
		DEL(tokens);
		if (!plus)
			break;
		buf_put(value, "+");
		part = plus + 1;
	}
	free(copy_of);
	return ok;
}

/*
 * Keeps only the lookup words that are a term's aliases. generate_lookup also
 * lists each canonical form and its variants, as it does for class names, and
 * those must never match text.
 */
static void aliases_only(J *lookup, const J *fwd)
{
	Map aliases = { 0 };
	EACH(k, fwd)
		EACH(v, k)
			map_put(&aliases, S(v), (void *)1);
	EACH(group, lookup) {
		J *v = group->child;
		while (v) {
			J *next = v->next;
			if (!map_get(&aliases, S(v)))
				DEL(cJSON_DetachItemViaPointer(group, v));
			v = next;
		}
	}
	map_free(&aliases);
}

/* Whether a span word keeps whitespace once folded. */
static int spaced(const char *s)
{
	char *owned;
	const char *p = ufold_view(s, &owned);
	int found = 0;
	while (*p && !found)
		found = uspace(uread(&p));
	free(owned);
	return found;
}

/*
 * Drops span rules with a word that keeps whitespace once folded. Whitespace
 * splits tokens, so such a word never matches text; it could only match a
 * canonical form, which always holds a space.
 */
static void text_rules_only(J *spans)
{
	J *k = spans ? spans->child : NULL;
	while (k) {
		J *next = k->next;
		J *r = spaced(k->string) ? NULL : k->child;
		while (r) {
			J *after = r->next;
			int drop = 0;
			EACH(w, GET(r, "content"))
				drop |= spaced(S(w));
			EACH(w, GET(r, "context"))
				drop |= spaced(S(w));
			if (drop)
				DEL(cJSON_DetachItemViaPointer(k, r));
			r = after;
		}
		if (spaced(k->string) || !k->child)
			DEL(cJSON_DetachItemViaPointer(spans, k));
		k = next;
	}
}

J *match_terms(const J *q, int line_breaks, mc_error *e)
{
	const J *text = GET(q, "text"), *terms = GET(q, "terms");
	if (!cJSON_IsString(text)) {
		fail(e, 2, "match_terms requires string text");
		return NULL;
	}
	if (!cJSON_IsArray(terms)) {
		fail(e, 2, "match_terms requires an array of terms");
		return NULL;
	}
	EACH(t, terms)
		if (!cJSON_IsString(t)) {
			fail(e, 2, "match_terms terms must be strings");
			return NULL;
		}
	/*
	 * Each distinct term gets its own canonical form: its words joined by
	 * underscores, which the spans stage ranks by, then the index of the
	 * first term that reads that way and a space. A canonical form always
	 * holds a space, which no word of text does, so with the lookup and
	 * span rules kept to text one term's match never feeds another term,
	 * and the side map gives back that first term.
	 */
	J *raw = OBJ(), *out = ARR();
	Map patterns = { 0 }, first = { 0 };
	int index_of = 0;
	EACH(t, terms) {
		Buf read = { 0 }, words = { 0 };
		int ok = label(S(t), &read, &words);
		char *value = buf_take(&read), *w = buf_take(&words);
		if (!ok || !value || !w) {
			free(value);
			free(w);
			fail(e, 1, "Cannot allocate terms");
			break;
		}
		if (*w && !map_get(&patterns, value)) {
			char suffix[32];
			Buf b = { 0 };
			buf_put(&b, w);
			snprintf(suffix, sizeof(suffix), " %d ", index_of);
			buf_put(&b, suffix);
			char *name = buf_take(&b);
			J *labels = ARR();
			ADD(labels, STR(value));
			PUT(raw, name, labels);
			map_put(&patterns, value, (void *)1);
			map_put(&first, name,
				(void *)(uintptr_t)(index_of + 1));
			free(name);
		}
		free(value);
		free(w);
		index_of++;
	}
	map_free(&patterns);
	if (e->code || !raw->child) {
		map_free(&first);
		DEL(raw);
		if (e->code) {
			DEL(out);
			return NULL;
		}
		return out;
	}
	J *view = OBJ(), *synonyms = OBJ(), *fwd = generate_synonyms(raw, 0);
	J *lookup = generate_lookup(fwd);
	aliases_only(lookup, fwd);
	PUT(synonyms, "lookup", lookup);
	PUT(synonyms, "fwd", fwd);
	PUT(synonyms, "rev", generate_synonyms(raw, 1));
	PUT(view, "synonyms", synonyms);
	/* Only + terms make span rules, as plus_only does for labels. */
	J *spans = generate_spans(raw, 4, 1, 0);
	text_rules_only(spans);
	PUT(view, "spans", spans);
	DEL(raw);
	MatchIndex *index = match_index_build(view);
	J *tokens = index ? tokenize_text(S(text)) : NULL;
	J *names = ARR(), *matched = NULL;
	if (!index || !tokens)
		fail(e, 1, "Cannot allocate term matching");
	else
		matched = match_tokens(index, tokens, names, 0, line_breaks, e);
	EACH(t, matched) {
		const char *canon = S(GET(GET(t, "swaps"), "canon"));
		int term = (int)(uintptr_t)map_get(&first, canon);
		if (!term)
			continue;
		J *m = OBJ();
		PUT(m, "term", NUM(term - 1));
		PUT(m, "x", DUP(GET(t, "x")));
		PUT(m, "y", DUP(GET(t, "y")));
		ADD(out, m);
	}
	DEL(matched);
	DEL(names);
	match_index_free(index);
	DEL(view);
	map_free(&first);
	if (e->code) {
		DEL(out);
		return NULL;
	}
	return out;
}
