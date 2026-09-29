/*
 * lingpatlab_parse.c - Token construction and normalization.
 *
 * Applies LingPatLab rules to spaCy tokens and prepared token input.
 */

#include "lingpatlab.h"

J *lp_tokenize(const char *text)
{
	J *enclitics = lp_data("d_enclictics"),
	  *abbreviations = lp_data("d_abbreviations");
	J *split_tokens = ARR();
	const char *start = text, *p = text;
	while (*p) {
		if (*p++ == ' ') {
			char *s = slice(start, (size_t)(p - start));
			ADD(split_tokens, STR(s));
			free(s);
			start = p;
		}
	}
	if (*start)
		ADD(split_tokens, STR(start));
	J *expanded = ARR();
	EACH(t, split_tokens) {
		J *entry = GET(enclitics, S(t));
		if (strchr(S(t), '\'') && entry) {
			EACH(x, entry)
				ADD(expanded, DUP(x));
		} else
			ADD(expanded, DUP(t));
	}
	DEL(split_tokens);
	DEL(enclitics);
	J *punct = ARR();
	EACH(t, expanded) {
		char *word = copy(S(t));
		int dots = 0;
		for (char *a = word; *a; a++)
			dots += *a == '.';
		/*
		 * Multi-period words bypass dictionary expansion, as before,
		 * but keep their actual punctuation. A textual sentinel can be
		 * split apart by this pass and can also collide with literal
		 * tildes in user input.
		 */
		J *abbr = dots < 2 ? GET(abbreviations, word) : NULL;
		if (abbr) {
			free(word);
			/*
			 * A few legacy dictionary values encode their period as
			 * ~~. Decode dictionary data only; literal user tildes
			 * stay literal.
			 */
			word = replace(S(abbr), "~~", ".");
		}
		Buf b = { 0 };
		uint32_t prev = 0;
		const char *a = word;
		while (*a) {
			uint32_t ch = uread(&a);
			const char *peek = a;
			uint32_t next = *peek ? uread(&peek) : 0;
			if (ualpha(ch) || lp_numeric(ch) || ch == ' ' ||
			    ch == '_' ||
			    ((ch == '.' || ch == ',') && lp_numeric(prev)) ||
			    (ch == '\'' && ualpha(prev)) ||
			    (ch == '&' && ualpha(prev) && ualpha(next)))
				uwrite(&b, ch);
			else {
				if (b.n) {
					ADD(punct, STR(b.p));
					free(buf_take(&b));
				}
				Buf single = { 0 };
				uwrite(&single, ch);
				ADD(punct, STR(single.p));
				free(single.p);
			}
			prev = ch;
		}
		if (b.n)
			ADD(punct, STR(b.p));
		free(b.p);
		free(word);
	}
	DEL(expanded);
	DEL(abbreviations);
	J *spaces = ARR();
	for (int i = 0; i < SIZE(punct); i++) {
		char *word = copy(S(AT(punct, i)));
		if (i + 1 < SIZE(punct) && !strcmp(S(AT(punct, i + 1)), " ")) {
			Buf b = { 0 };
			buf_put(&b, word);
			buf_put(&b, " ");
			free(word);
			word = buf_take(&b);
			i++;
		}
		ADD(spaces, STR(word));
		free(word);
	}
	DEL(punct);
	J *quotes = ARR();
	EACH(t, spaces) {
		const char *word = S(t);
		if (strchr(word, '\'') && !lp_ends(word, "s' ") &&
		    lp_ends(word, "' ")) {
			char *prefix = slice(word, strlen(word) - 2);
			Buf b = { 0 };
			buf_put(&b, prefix);
			buf_put(&b, " ");
			ADD(quotes, STR(b.p));
			ADD(quotes, STR("'"));
			free(prefix);
			free(b.p);
		} else
			ADD(quotes, DUP(t));
	}
	DEL(spaces);
	int singles = 0, last_single = -1, first_suffix = -1, i = 0;
	EACH(t, quotes) {
		const char *s = S(t);
		if (!strcmp(s, "'")) {
			singles++;
			last_single = i;
		}
		char *trim = norm(s, 0, 0);
		if (ulen(s) > 1 && lp_ends(trim, "'") && first_suffix < 0)
			first_suffix = i;
		free(trim);
		i++;
	}
	if (first_suffix >= 0 && singles % 2 == 1 &&
	    last_single <= first_suffix) {
		J *out = ARR();
		i = 0;
		EACH(t, quotes) {
			if (i++ == first_suffix) {
				char *prefix = slice(S(t), strlen(S(t)) - 1);
				Buf b = { 0 };
				buf_put(&b, prefix);
				buf_put(&b, " ");
				ADD(out, STR(b.p));
				ADD(out, STR("'"));
				free(prefix);
				free(b.p);
			} else
				ADD(out, DUP(t));
		}
		DEL(quotes);
		quotes = out;
	}
	return quotes;
}

static char *normal_form(const char *text)
{
	static const uint32_t hyphens[] = { 0x058a, 0x1806, 0x2010, 0x2011,
					    0x2012, 0x2013, 0x2014, 0x2015,
					    0x2053, 0x207b, 0x208b, 0x2212,
					    0x2e3a, 0x2e3b, 0x301c, 0x3030,
					    0xfe58, 0xfe63, 0xff0d };
	char *s = copy(text);
	for (size_t i = 0; i < sizeof(hyphens) / sizeof(*hyphens); i++) {
		Buf b = { 0 };
		uwrite(&b, hyphens[i]);
		s = lp_replace(s, b.p, "-");
		free(b.p);
	}
	const char *dquotes[] = { "\xe2\x80\x9c",    "\xe2\x80\x9d", "\xc2\xab",
				  "\xc2\xbb",	     "\xe2\x80\x9e", "``",
				  "\xc2\xb4\xc2\xb4" };
	const char *squotes[] = { "\xe2\x80\x99", "\xe2\x80\x98",
				  "\xe2\x80\x9b", "`" };
	for (size_t i = 0; i < sizeof(dquotes) / sizeof(*dquotes); i++)
		s = lp_replace(s, dquotes[i], "\"");
	for (size_t i = 0; i < sizeof(squotes) / sizeof(*squotes); i++)
		s = lp_replace(s, squotes[i], "'");
	char *out = norm(s, 1, 0);
	free(s);
	return out;
}

J *lp_postprocess(const J *raw, mc_error *err)
{
	if (!cJSON_IsArray(raw)) {
		fail(err, 5,
		     "spaCy model interface did not return token array");
		return NULL;
	}
	J *result = ARR();
	for (int i = 0; i < SIZE(raw); i++) {
		J *token = AT(raw, i), *morph = GET(token, "morph");
		const char *required[] = { "text", "lemma", "pos",  "tag",
					   "dep",  "ent",   "shape" };
		if (!cJSON_IsObject(token) || !cJSON_IsObject(morph) ||
		    !cJSON_IsObject(GET(token, "other"))) {
			fail(err, 5, "Invalid spaCy model token");
			break;
		}
		for (size_t j = 0; j < sizeof(required) / sizeof(*required);
		     j++)
			if (!cJSON_IsString(GET(token, required[j])))
				fail(err, 5,
				     "Missing spaCy model token field: %s",
				     required[j]);
		if (err->code)
			break;
		int next = i + 1 < SIZE(raw) &&
			   !strcmp(S(GET(AT(raw, i + 1), "text")), " ");
		if (next)
			i++;
		char *text =
			lp_collapse(replace(S(GET(token, "text")), "\n", " "));
		if (next && !lp_ends(text, " ")) {
			Buf b = { 0 };
			buf_put(&b, text);
			buf_put(&b, " ");
			free(text);
			text = buf_take(&b);
		}
		J *out = OBJ(), *other = GET(token, "other");
		char *orth = cJSON_PrintUnformatted(GET(other, "orth")),
		     *headorth =
			     cJSON_PrintUnformatted(GET(other, "head_orth"));
		if (!orth || !headorth ||
		    !cJSON_IsNumber(GET(other, "head_i"))) {
			free(orth);
			free(headorth);
			free(text);
			DEL(out);
			fail(err, 5, "Missing spaCy token identifiers");
			break;
		}
		Buf id = { 0 }, head = { 0 };
		char num[40];
		snprintf(num, sizeof num, "#%d", i);
		buf_put(&id, orth);
		buf_put(&id, num);
		snprintf(num, sizeof num, "#%d",
			 GET(other, "head_i")->valueint);
		buf_put(&head, headorth);
		buf_put(&head, num);
		free(orth);
		free(headorth);
		PUT(out, "id", STR(id.p));
		PUT(out, "head", STR(head.p));
		free(id.p);
		free(head.p);
		PUT(out, "text", STR(text));
		free(text);
		const char *copy_fields[] = { "lemma", "sentiment", "pos",
					      "tag",   "dep",	    "ent",
					      "shape", "is_alpha",  "is_stop",
					      "other" };
		for (size_t j = 0;
		     j < sizeof(copy_fields) / sizeof(*copy_fields); j++) {
			J *v = GET(token, copy_fields[j]);
			if (!v) {
				fail(err, 5, "Missing spaCy token field: %s",
				     copy_fields[j]);
				break;
			}
			PUT(out, copy_fields[j], DUP(v));
		}
		char *tense = lower(S(GET(morph, "Tense"))),
		     *verb = lower(S(GET(morph, "VerbForm")));
		PUT(out, "tense", STR(tense));
		PUT(out, "verb_form", STR(verb));
		free(tense);
		free(verb);
		const char *number = S(GET(morph, "Number"));
		if (*number && strcmp(number, "Sing") && strcmp(number, "Plur"))
			fail(err, 2, "Unsupported noun number: %s", number);
		PUT(out, "noun_number",
		    STR(!strcmp(number, "Sing") ? "singular" :
			!strcmp(number, "Plur") ? "plural" :
						  ""));
		const char *punctuation[] = { "!",  "?", ":", ".",
					      "\"", "-", "(", ")" };
		int punct = 0;
		for (size_t j = 0;
		     j < sizeof(punctuation) / sizeof(*punctuation); j++)
			if (!strcmp(S(GET(out, "text")), punctuation[j]))
				punct = 1;
		PUT(out, "is_punct", BOOL(punct));
		ADD(result, out);
		if (err->code)
			break;
	}
	if (err->code) {
		DEL(result);
		return NULL;
	}
	size_t pos = 0;
	for (int i = 0; i < SIZE(result); i++) {
		J *t = AT(result, i);
		const char *next = i + 1 < SIZE(result) ?
					   S(GET(AT(result, i + 1), "text")) :
					   "";
		if (!strcmp(next, ")") || !strcmp(next, "\"") ||
		    !strcmp(next, "!") || !strcmp(next, "?")) {
			char *trim = norm(S(GET(t, "text")), 0, 0);
			set(t, "text", STR(trim));
			free(trim);
		}
		const char *text = S(GET(t, "text"));
		char *trim = norm(text, 0, 0);
		PUT(t, "x", NUM((double)pos));
		PUT(t, "y", NUM((double)(pos + ulen(trim))));
		free(trim);
		pos += ulen(text);
		PUT(t, "is_wordnet",
		    BOOL(!cJSON_IsTrue(GET(t, "is_punct")) &&
			 lp_wordnet(text)));
		char *normal = normal_form(text),
		     *stem = cJSON_IsTrue(GET(t, "is_punct")) ? copy(normal) :
								lp_stem(normal);
		PUT(t, "normal", STR(normal));
		PUT(t, "stem", STR(stem));
		free(normal);
		free(stem);
		cJSON_DeleteItemFromObjectCaseSensitive(t, "lemma");
	}
	return result;
}

J *lp_parse_tokens(mc_engine *engine, const J *tokens, mc_error *err)
{
	if (!cJSON_IsArray(tokens)) {
		fail(err, 2, "parse_input_tokens requires a string array");
		return NULL;
	}
	J *squots = ARR();
	EACH(t, tokens) {
		if (!cJSON_IsString(t)) {
			DEL(squots);
			fail(err, 2, "parse_input_tokens requires strings");
			return NULL;
		}
		ADD(squots, STR(!strcmp(S(t), "'") ? "\"" : S(t)));
	}
	if (!SIZE(squots)) {
		DEL(squots);
		return ARR();
	}
	char *text = join(squots, " ");
	DEL(squots);
	J *q = OBJ();
	PUT(q, "op", STR("analyze"));
	PUT(q, "text", STR(text));
	free(text);
	J *doc = spacy_call(engine, q, err);
	DEL(q);
	if (!doc)
		return NULL;
	J *raw = GET(doc, "tokens"), *spans = ARR();
	if (!cJSON_IsArray(raw)) {
		DEL(spans);
		DEL(doc);
		fail(err, 5, "Invalid spaCy model document");
		return NULL;
	}
	for (int i = 1; i < SIZE(raw); i++)
		if (strchr(S(GET(AT(raw, i), "text")), '\'')) {
			J *span = ARR();
			ADD(span, NUM(i - 1));
			ADD(span, NUM(i + 1));
			ADD(spans, span);
		}
	if (SIZE(spans)) {
		q = OBJ();
		PUT(q, "op", STR("retokenize"));
		PUT(q, "spans", spans);
		DEL(doc);
		doc = spacy_call(engine, q, err);
		DEL(q);
		if (!doc)
			return NULL;
		raw = GET(doc, "tokens");
	} else
		DEL(spans);
	J *result = lp_postprocess(raw, err);
	DEL(doc);
	return result;
}
