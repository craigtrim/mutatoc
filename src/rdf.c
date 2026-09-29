/*
 * rdf.c - Turtle parsing and RDF graph operations.
 *
 * Reads RDF terms, indexes triples, and flattens graph values for queries.
 */

#include "mc.h"
#include "rdf_namespaces.h"

typedef struct {
	const char *text, *p;
	char *base;
	Graph *g;
	mc_error *e;
	unsigned blanks, depth;
	J *lexical_prefixes;
} Parser;
static Term term(const char *s, int kind)
{
	Term t = { copy(s), NULL, NULL, kind };
	return t;
}

static void term_free(Term *t)
{
	free(t->value);
	free(t->datatype);
	free(t->language);
	memset(t, 0, sizeof(*t));
}

static Term term_copy(const Term *t)
{
	Term v = { copy(t->value), t->datatype ? copy(t->datatype) : NULL,
		   t->language ? copy(t->language) : NULL, t->kind };
	return v;
}

static void error(Parser *p, const char *s)
{
	fail(p->e, 3, "%s", s);
	if (p->e) {
		p->e->line = 1;
		p->e->column = 1;
		for (const char *c = p->text; c < p->p; c++)
			if (*c == '\n') {
				p->e->line++;
				p->e->column = 1;
			} else
				p->e->column++;
	}
}

static void ws(Parser *p)
{
	for (;;) {
		while (*p->p && isspace((unsigned char)*p->p))
			p->p++;
		if (*p->p == '#') {
			while (*p->p && *p->p != '\n')
				p->p++;
		} else
			break;
	}
}

static int accept(Parser *p, char ch)
{
	ws(p);
	if (*p->p == ch) {
		p->p++;
		return 1;
	}
	return 0;
}

static int expected(Parser *p, char ch)
{
	if (accept(p, ch))
		return 1;
	char s[64];
	snprintf(s, sizeof(s), "Expected '%c' in Turtle", ch);
	error(p, s);
	return 0;
}

static int delim(char c)
{
	return !c || isspace((unsigned char)c) ||
	       strchr(";,[]()<>\"'", c) != NULL;
}

static char *word(Parser *p)
{
	ws(p);
	const char *s = p->p;
	while (!delim(*p->p)) {
		if (*p->p == '#')
			break;
		if (*p->p == '.' && (delim(p->p[1]) || p->p[1] == '#'))
			break;
		if (*p->p == '\\' && p->p[1])
			p->p++;
		p->p++;
	}
	return slice(s, (size_t)(p->p - s));
}

static uint32_t hex(Parser *p, int n)
{
	uint32_t v = 0;
	for (int i = 0; i < n; i++) {
		unsigned char c = (unsigned char)*p->p;
		if (!isxdigit(c)) {
			error(p, "Invalid Unicode escape");
			return 0;
		}
		p->p++;
		v = v * 16 +
		    (uint32_t)(c <= '9' ? c - '0' : tolower(c) - 'a' + 10);
	}
	if (v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff))
		error(p, "Invalid Unicode scalar");
	return v;
}

static void escape(Parser *p, Buf *b, int iri)
{
	char c = *p->p;
	if (!c) {
		error(p, "Incomplete escape");
		return;
	}
	p->p++;
	if (c == 'u' || c == 'U') {
		uint32_t scalar = hex(p, c == 'u' ? 4 : 8);
		if (iri &&
		    (scalar <= 32 ||
		     (scalar < 128 && strchr("<>\"{}|^`\\", (int)scalar))))
			error(p, "Invalid escaped IRI character");
		uwrite(b, scalar);
		return;
	}
	if (iri) {
		error(p, "Invalid IRI escape");
		return;
	}
	switch (c) {
	case 'n':
		c = '\n';
		break;
	case 'r':
		c = '\r';
		break;
	case 't':
		c = '\t';
		break;
	case 'b':
		c = '\b';
		break;
	case 'f':
		c = '\f';
		break;
	case '\\':
	case '\'':
	case '"':
		break;
	default:
		error(p, "Invalid string escape");
		return;
	}
	buf_add(b, &c, 1);
}

static char *iri(Parser *p)
{
	expected(p, '<');
	Buf b = { 0 };
	while (*p->p && *p->p != '>') {
		unsigned char c = (unsigned char)*p->p++;
		if (c == '\\')
			escape(p, &b, 1);
		else if (c <= 32 || strchr("<>\"{}|^`", c)) {
			error(p, "Invalid character in IRI");
			break;
		} else
			buf_add(&b, (const char *)&c, 1);
		if (p->e->code)
			break;
	}
	expected(p, '>');
	char *s = buf_take(&b);
	char *r = uri_resolve(p->base, s);
	free(s);
	return r;
}

static int pn_base(uint32_t c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       (c >= 0xc0 && c <= 0xd6) || (c >= 0xd8 && c <= 0xf6) ||
	       (c >= 0xf8 && c <= 0x2ff) || (c >= 0x370 && c <= 0x37d) ||
	       (c >= 0x37f && c <= 0x1fff) || (c >= 0x200c && c <= 0x200d) ||
	       (c >= 0x2070 && c <= 0x218f) || (c >= 0x2c00 && c <= 0x2fef) ||
	       (c >= 0x3001 && c <= 0xd7ff) || (c >= 0xf900 && c <= 0xfdcf) ||
	       (c >= 0xfdf0 && c <= 0xfffd) || (c >= 0x10000 && c <= 0xeffff);
}

static int pn_char(uint32_t c)
{
	return pn_base(c) || c == '_' || c == '-' || (c >= '0' && c <= '9') ||
	       c == 0xb7 || (c >= 0x300 && c <= 0x36f) ||
	       (c >= 0x203f && c <= 0x2040);
}

/* mode: 0 prefix, 1 local name, 2 blank-node label. */
static int valid_name(const char *s, int mode)
{
	if (!*s)
		return mode != 2;
	int first = 1;
	while (*s) {
		if (mode == 1 && *s == '%') {
			if (!s[1] || !s[2] || !isxdigit((unsigned char)s[1]) ||
			    !isxdigit((unsigned char)s[2]))
				return 0;
			s += 3;
			first = 0;
			continue;
		}
		if (mode == 1 && *s == '\\') {
			if (!s[1] || !strchr("_~.-!$&'()*+,;=/?#@%", s[1]))
				return 0;
			s += 2;
			first = 0;
			continue;
		}
		uint32_t c = uread(&s);
		if (first) {
			if (!pn_base(c) &&
			    !(mode && (c == '_' || (c >= '0' && c <= '9') ||
				       (mode == 1 && c == ':'))))
				return 0;
		} else if (!pn_char(c) && !(c == '.' && *s) &&
			   !(mode == 1 && c == ':'))
			return 0;
		first = 0;
	}
	return 1;
}

static Term resource(Parser *p)
{
	ws(p);
	if (*p->p == '<') {
		char *s = iri(p);
		Term t = term(s, 0);
		free(s);
		return t;
	}
	char *w = word(p);
	if (!strncmp(w, "_:", 2)) {
		if (!valid_name(w + 2, 2))
			error(p, "Invalid blank-node label");
		Term t = term(w, 2);
		free(w);
		return t;
	}
	char *colon = strchr(w, ':');
	if (!colon) {
		error(p, "Expected an IRI or prefixed name");
		free(w);
		return term("", 0);
	}
	*colon = 0;
	if (!valid_name(w, 0) || !valid_name(colon + 1, 1))
		error(p, "Invalid prefixed name");
	J *ns = GET(p->lexical_prefixes, w);
	if (!ns) {
		error(p, "Undefined Turtle prefix");
		free(w);
		return term("", 0);
	}
	Buf b = { 0 };
	buf_put(&b, S(ns));
	for (char *c = colon + 1; *c; c++) {
		if (*c == '\\' && c[1])
			c++;
		buf_add(&b, c, 1);
	}
	char *s = buf_take(&b);
	Term t = term(s, 0);
	free(w);
	free(s);
	return t;
}

static void add(Graph *g, const Term *s, const Term *p, const Term *o)
{
	Buf key = { 0 };
	const Term *terms[] = { s, p, o };
	for (size_t i = 0; i < 3; i++) {
		const char *parts[] = { terms[i]->value, terms[i]->datatype,
					terms[i]->language };
		char length[64];
		snprintf(length, sizeof(length), "%d:", terms[i]->kind);
		buf_put(&key, length);
		for (size_t j = 0; j < 3; j++) {
			const char *part = parts[j] ? parts[j] : "";
			snprintf(length, sizeof(length), "%zu:", strlen(part));
			buf_put(&key, length);
			buf_put(&key, part);
		}
	}
	if (map_get(&g->triple_keys, key.p)) {
		free(key.p);
		return;
	}
	map_put(&g->triple_keys, key.p, (void *)1);
	free(key.p);

	if (g->n == g->cap) {
		g->cap = g->cap ? g->cap * 2 : 256;
		g->ts = realloc(g->ts, g->cap * sizeof(*g->ts));
	}
	size_t n = g->n++;
	g->ts[n] = (Triple){ term_copy(s), term_copy(p), term_copy(o) };
	J *a = map_get(&g->subjects, s->value);
	if (!a) {
		a = ARR();
		map_put(&g->subjects, s->value, a);
	}
	ADD(a, NUM((double)n));
}

static Term blank(Parser *p)
{
	char s[48];
	snprintf(s, sizeof(s), "_:!auto%u", ++p->blanks);
	return term(s, 2);
}

static Term object(Parser *);
static void properties(Parser *, Term *);
static Term collection(Parser *p)
{
	expected(p, '(');
	ws(p);
	if (accept(p, ')'))
		return term(RDF "nil", 0);
	Term head = blank(p), cell = term_copy(&head),
	     first = term(RDF "first", 0), rest = term(RDF "rest", 0);
	while (*p->p && !p->e->code) {
		Term v = object(p);
		add(p->g, &cell, &first, &v);
		term_free(&v);
		ws(p);
		int end = accept(p, ')');
		Term next = end ? term(RDF "nil", 0) : blank(p);
		add(p->g, &cell, &rest, &next);
		term_free(&cell);
		cell = next;
		if (end)
			break;
	}
	term_free(&cell);
	term_free(&first);
	term_free(&rest);
	return head;
}

static Term object(Parser *p)
{
	ws(p);
	if (++p->depth > 128) {
		error(p, "Turtle nesting exceeds 128 levels");
		p->depth--;
		return term("", 0);
	}
	Term t = { 0 };
	char c = *p->p;
	if (c == '[') {
		p->p++;
		t = blank(p);
		if (!accept(p, ']')) {
			properties(p, &t);
			expected(p, ']');
		}
	} else if (c == '(')
		t = collection(p);
	else if (c == '\'' || c == '"') {
		p->p++;
		int multi = p->p[0] == c && p->p[1] == c;
		if (multi)
			p->p += 2;
		Buf b = { 0 };
		int closed = 0;
		while (*p->p && !p->e->code) {
			if (*p->p == c &&
			    (!multi || (p->p[1] == c && p->p[2] == c))) {
				p->p += multi ? 3 : 1;
				closed = 1;
				break;
			}
			char x = *p->p++;
			if (x == '\\')
				escape(p, &b, 0);
			else {
				if (!multi && (x == '\n' || x == '\r'))
					error(p, "Newline in short string");
				buf_add(&b, &x, 1);
			}
		}
		if (!closed)
			error(p, "Unterminated string literal");
		t.value = buf_take(&b);
		t.kind = 1;
		if (*p->p == '@') {
			p->p++;
			t.language = word(p);
			const char *lang = t.language;
			if (!isalpha((unsigned char)*lang))
				error(p, "Invalid language tag");
			while (isalpha((unsigned char)*lang))
				++lang;
			while (*lang == '-') {
				++lang;
				if (!isalnum((unsigned char)*lang))
					break;
				while (isalnum((unsigned char)*lang))
					++lang;
			}
			if (*lang)
				error(p, "Invalid language tag");
			t.datatype = copy(RDF "langString");
		} else if (p->p[0] == '^' && p->p[1] == '^') {
			p->p += 2;
			Term dt = resource(p);
			t.datatype = dt.value;
			dt.value = NULL;
			term_free(&dt);
		} else
			t.datatype =
				NULL; /* RDFLib preserves plain versus explicitly typed strings. */
	} else if (isdigit((unsigned char)c) || c == '+' || c == '-' ||
		   (c == '.' && isdigit((unsigned char)p->p[1]))) {
		char *w = word(p);
		const char *end = w;
		if (*end == '+' || *end == '-')
			++end;
		size_t digits = 0;
		while (isdigit((unsigned char)*end)) {
			digits++;
			end++;
		}
		if (*end == '.') {
			++end;
			while (isdigit((unsigned char)*end)) {
				digits++;
				end++;
			}
		}
		if (*end == 'e' || *end == 'E') {
			++end;
			if (*end == '+' || *end == '-')
				++end;
			if (!isdigit((unsigned char)*end))
				error(p, "Invalid numeric exponent");
			while (isdigit((unsigned char)*end))
				++end;
		}
		if (!digits || *end)
			error(p, "Invalid numeric literal");
		t = term(w, 1);
		t.datatype = copy(strpbrk(w, "eE") ? XSD "double" :
				  strchr(w, '.')   ? XSD "decimal" :
						     XSD "integer");
		free(w);
	} else if ((!strncmp(p->p, "true", 4) &&
		    (delim(p->p[4]) || p->p[4] == '.')) ||
		   (!strncmp(p->p, "false", 5) &&
		    (delim(p->p[5]) || p->p[5] == '.'))) {
		char *w = word(p);
		t = term(w, 1);
		t.datatype = copy(XSD "boolean");
		free(w);
	} else
		t = resource(p);
	literal_normalize(&t);
	p->depth--;
	return t;
}

static void properties(Parser *p, Term *s)
{
	for (;;) {
		ws(p);
		Term pred;
		if (p->p[0] == 'a' && delim(p->p[1])) {
			p->p++;
			pred = term(RDF "type", 0);
		} else
			pred = resource(p);
		if (pred.kind != 0)
			error(p, "Predicate must be an IRI");
		do {
			Term o = object(p);
			if (!p->e->code)
				add(p->g, s, &pred, &o);
			term_free(&o);
		} while (!p->e->code && accept(p, ','));
		term_free(&pred);
		if (p->e->code || !accept(p, ';'))
			break;
		while (accept(p, ';')) {
		}
		ws(p);
		if (*p->p == '.' || *p->p == ']' || !*p->p)
			break;
	}
}

static int keyword(const char *p, const char *word)
{
	for (; *word; word++, p++)
		if (!*p || toupper((unsigned char)*p) != *word)
			return 0;
	return isspace((unsigned char)*p) || *p == '<';
}

static void bind_prefix(Graph *g, const char *prefix, const char *namespace)
{
	if (!GET(g->prefixes, prefix) ||
	    !strcmp(S(GET(g->prefixes, prefix)), namespace)) {
		set(g->prefixes, prefix, STR(namespace));
		return;
	}
	for (unsigned i = 1;; i++) {
		Buf candidate = { 0 };
		char suffix[24];
		snprintf(suffix, sizeof(suffix), "%u", i);
		buf_put(&candidate, prefix);
		buf_put(&candidate, suffix);
		int available =
			!GET(g->prefixes, candidate.p) ||
			!strcmp(S(GET(g->prefixes, candidate.p)), namespace);
		if (available)
			set(g->prefixes, candidate.p, STR(namespace));
		free(candidate.p);
		if (available)
			return;
	}
}

Graph *rdf_parse(const char *text, const char *base, mc_error *e)
{
	Graph *g = calloc(1, sizeof(*g));
	if (!g) {
		fail(e, 5, "Out of memory");
		return NULL;
	}
	g->prefixes = OBJ();
	for (size_t i = 0; i < sizeof(rdf_defaults) / sizeof(*rdf_defaults);
	     i++)
		PUT(g->prefixes, rdf_defaults[i][0], STR(rdf_defaults[i][1]));
	Parser p = { text, text, copy(base ? base : ""), g, e, 0, 0, OBJ() };
	if (!strncmp(p.p, "\xef\xbb\xbf", 3))
		p.p += 3;
	while (*p.p && !e->code) {
		ws(&p);
		if (!*p.p)
			break;
		int prefix = !strncmp(p.p, "@prefix", 7),
		    b = !strncmp(p.p, "@base", 5),
		    sprefix = keyword(p.p, "PREFIX"),
		    sbase = keyword(p.p, "BASE");
		if (prefix || b || sprefix || sbase) {
			p.p += prefix ? 7 : b ? 5 : sprefix ? 6 : 4;
			ws(&p);
			char *name = NULL;
			if (prefix || sprefix) {
				name = word(&p);
				size_t n = strlen(name);
				if (!n || name[n - 1] != ':')
					error(&p,
					      "Prefix name must end in ':'");
				else {
					name[n - 1] = 0;
					if (!valid_name(name, 0))
						error(&p,
						      "Invalid prefix name");
				}
			}
			ws(&p);
			char *value = iri(&p);
			if (name) {
				set(p.lexical_prefixes, name, STR(value));
				bind_prefix(g, name, value);
				free(name);
			} else {
				free(p.base);
				p.base = copy(value);
			}
			free(value);
			if (prefix || b)
				expected(&p, '.');
			continue;
		}
		size_t before = g->n;
		int property_subject = *p.p == '[';
		Term s = object(&p);
		if (s.kind == 1)
			error(&p, "RDF subject cannot be a literal");
		if (!e->code) {
			ws(&p);
			if (*p.p != '.' || !property_subject || g->n == before)
				properties(&p, &s);
			expected(&p, '.');
		}
		term_free(&s);
	}
	free(p.base);
	DEL(p.lexical_prefixes);
	if (e->code) {
		rdf_free(g);
		return NULL;
	}
	return g;
}

void rdf_free(Graph *g)
{
	if (!g)
		return;
	for (size_t i = 0; i < g->n; i++) {
		term_free(&g->ts[i].s);
		term_free(&g->ts[i].p);
		term_free(&g->ts[i].o);
	}
	for (size_t i = 0; i < g->subjects.cap; i++)
		if (g->subjects.slots[i].key)
			DEL(g->subjects.slots[i].value);
	map_free(&g->subjects);
	map_free(&g->triple_keys);
	free(g->ts);
	DEL(g->prefixes);
	free(g);
}

J *rdf_values(Graph *g, const char *s, const char *p)
{
	J *r = ARR(), *ids = map_get(&g->subjects, s);
	EACH(v, ids) {
		Triple *t = &g->ts[(size_t)v->valuedouble];
		if (!p || !strcmp(t->p.value, p))
			ADD(r, NUM(v->valuedouble));
	}
	return r;
}

static void flatten(Graph *g, Term *t, int lc, J *r, Map *active, int depth,
		    mc_error *e)
{
	if (depth > 128) {
		fail(e, 3, "Blank-node expansion exceeds 128 levels");
		return;
	}
	if (t->kind != 2) {
		char *s =
			norm(t->kind == 0 ? local(t->value) : t->value, lc, 0);
		ADD(r, STR(s));
		free(s);
		return;
	}
	if (map_get(active, t->value)) {
		fail(e, 3, "Cycle in blank-node expansion");
		return;
	}
	map_put(active, t->value, (void *)1);
	J *ids = map_get(&g->subjects, t->value);
	EACH(v, ids)
		flatten(g, &g->ts[(size_t)v->valuedouble].o, lc, r, active,
			depth + 1, e);
	map_put(active, t->value, NULL);
}

J *rdf_flatten(Graph *g, Term *t, int lc, mc_error *e)
{
	J *r = ARR();
	Map active = { 0 };
	flatten(g, t, lc, r, &active, 0, e);
	map_free(&active);
	return r;
}

static J *term_json(Term *t)
{
	J *j = OBJ();
	PUT(j, "value", STR(t->value));
	PUT(j, "kind",
	    STR(t->kind == 0 ? "iri" :
		t->kind == 1 ? "literal" :
			       "blank"));
	if (t->datatype)
		PUT(j, "datatype", STR(t->datatype));
	if (t->language)
		PUT(j, "language", STR(t->language));
	return j;
}

J *rdf_json(Graph *g)
{
	J *a = ARR();
	for (size_t i = 0; i < g->n; i++) {
		J *t = OBJ();
		PUT(t, "subject", term_json(&g->ts[i].s));
		PUT(t, "predicate", term_json(&g->ts[i].p));
		PUT(t, "object", term_json(&g->ts[i].o));
		ADD(a, t);
	}
	return a;
}

Graph *rdf_merge(Graph **graphs, size_t count)
{
	Graph *out = calloc(1, sizeof(*out));
	if (!out)
		return NULL;
	out->prefixes = OBJ();
	for (size_t i = 0; i < count; i++) {
		Graph *g = graphs[i];
		if (!g) {
			rdf_free(out);
			return NULL;
		}
		EACH(prefix, g->prefixes)
			if (!GET(out->prefixes, prefix->string))
				PUT(out->prefixes, prefix->string, DUP(prefix));
		for (size_t j = 0; j < g->n; j++) {
			Term terms[3] = { term_copy(&g->ts[j].s),
					  term_copy(&g->ts[j].p),
					  term_copy(&g->ts[j].o) };
			for (int k = 0; k < 3; k++)
				if (terms[k].kind == 2) {
					Buf b = { 0 };
					char prefix[40];
					snprintf(prefix, sizeof(prefix),
						 "_:source%zu_", i);
					buf_put(&b, prefix);
					buf_put(&b, terms[k].value);
					free(terms[k].value);
					terms[k].value = buf_take(&b);
				}
			add(out, &terms[0], &terms[1], &terms[2]);
			for (int k = 0; k < 3; k++)
				term_free(&terms[k]);
		}
	}
	return out;
}
