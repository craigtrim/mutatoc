/*
 * ontology.c - Ontology extraction and derived lookup tables.
 *
 * Builds JSON views from RDF graphs and derives synonym and hierarchy data.
 */

#include "mc.h"

static int count(const char *s, char c)
{
	int n = 0;
	for (; *s; s++)
		n += *s == c;
	return n;
}

static void push(J *j, const char *k, const char *v)
{
	unique(ensure(j, k, 1), v);
}

/*
 * Each index lives only while its JSON object is being built. JSON remains the
 * public representation, including insertion order and duplicate handling.
 */
static J *indexed_ensure(J *object, Map *index, const char *key, int array)
{
	J *value = map_get(index, key);
	if (!value) {
		value = array ? ARR() : OBJ();
		PUT(object, key, value);
		map_put(index, key, value);
	}
	return value;
}

static void indexed_push(J *object, Map *index, const char *key,
			 const char *value)
{
	unique(indexed_ensure(object, index, key, 1), value);
}

static void indexed_set(J *object, Map *index, const char *key, J *value)
{
	if (map_get(index, key))
		set(object, key, value);
	else
		PUT(object, key, value);
	map_put(index, key, value);
}

static void index_members(Map *index, J *object)
{
	EACH(value, object)
		if (!map_get(index, value->string))
			map_put(index, value->string, value);
}

static int stopword(const char *s)
{
	static const char *words =
		" a about above after again against all am an and any are as at be because been before "
		"between both but by can did do does doing don down during each few for from further had "
		"has have having he her here hers herself him himself his how i if in into is it its "
		"itself just me more most my myself no nor not now of off on once only or other our ours "
		"ourselves out over own s same she should so some such t than that the their theirs them "
		"themselves then there these they this those through to too under until up very was we "
		"were what when where which while who whom why will with you your yours yourself "
		"yourselves ";
	Buf b = { 0 };
	buf_put(&b, " ");
	buf_put(&b, s);
	buf_put(&b, " ");
	int yes = strstr(words, b.p) != NULL;
	free(b.p);
	return yes;
}

J *generate_synonyms(J *raw, int reverse)
{
	J *f = OBJ();
	Map forward = { 0 };
	EACH(k, raw) {
		char *key = lower(k->string);
		J *a = indexed_ensure(f, &forward, key, 1);
		EACH(v, k) {
			char *r = replace(S(v), "+", " "), *l = lower(r),
			     *c = norm(l, 0, 1);
			unique(a, c);
			free(c);
			free(l);
			if (strchr(r, '_')) {
				char *t = replace(r, "_", " ");
				c = norm(t, 1, 1);
				unique(a, c);
				free(c);
				free(t);
			}
			size_t n = strlen(r);
			if (n && strchr("!?.", r[n - 1])) {
				char *t = slice(r, n - 1);
				unique(a, t);
				free(t);
			}
			if (strchr(r, '.') && !strstr(r, "...")) {
				char *t = replace(r, ".", " . ");
				c = norm(t, 0, 1);
				unique(a, c);
				free(c);
				free(t);
			}
			free(r);
		}
		sort_strings(a, 1);
		free(key);
	}
	map_free(&forward);
	if (!reverse)
		return f;
	J *r = OBJ();
	Map backward = { 0 };
	EACH(k, f) {
		EACH(v, k)
			indexed_push(r, &backward, S(v), k->string);
	}
	map_free(&backward);
	DEL(f);
	return r;
}

/*
 * Lookup groups hold every synonym of an ontology, so membership is hashed on
 * the group key and value together rather than scanned per insert.
 */
static void lookup_push(J *d, Map *seen, const char *key, const char *value)
{
	Buf k = { 0 };
	buf_put(&k, key);
	buf_add(&k, "\x1f", 1);
	buf_put(&k, value);
	if (!map_get(seen, k.p)) {
		map_put(seen, k.p, (void *)1);
		ADD(ensure(d, key, 1), STR(value));
	}
	free(k.p);
}

static void lookup_add(J *d, Map *seen, const char *s)
{
	char *r = replace(s, "_", " ");
	char key[32];
	snprintf(key, sizeof(key), "%d", count(r, ' ') + 1);
	lookup_push(d, seen, key, r);
	free(r);
}

J *generate_lookup(J *f)
{
	J *d = OBJ();
	Map seen = { 0 };
	EACH(k, f) {
		lookup_add(d, &seen, k->string);
		EACH(v, k) {
			char *s = norm(S(v), 1, 1);
			lookup_add(d, &seen, s);
			size_t n = strlen(s);
			if (n && strchr("!?.", s[n - 1])) {
				char key[32];
				snprintf(key, sizeof(key), "%d",
					 count(s, ' ') + 1);
				s[n - 1] = 0;
				lookup_push(d, &seen, key, s);
			}
			free(s);
		}
	}
	map_free(&seen);
	for (int n = 1; n <= 6; n++) {
		char k[8];
		snprintf(k, sizeof(k), "%d", n);
		ensure(d, k, 1);
	}
	for (int n = 5; n >= 2; n--) {
		char k[8];
		snprintf(k, sizeof(k), "%d", n + 1);
		J *values = DUP(GET(d, k)), *grams = ARR();
		EACH(v, values) {
			J *ts = split(S(v), " ");
			for (int i = 0; i + n < SIZE(ts); i++) {
				J *a = ARR();
				int valid = 1;
				for (int j = 0; j < n; j++) {
					const char *t = S(AT(ts, i + j));
					if (!strcmp(t, "and") ||
					    !strcmp(t, "for") ||
					    !strcmp(t, "of") ||
					    !strcmp(t, "to"))
						valid = 0;
					ADD(a, STR(t));
				}
				if (valid) {
					char *s = join(a, " ");
					unique(grams, s);
					free(s);
				}
				DEL(a);
			}
			DEL(ts);
		}
		EACH(v, values) {
			EACH(g, grams) {
				if (strstr(S(v), S(g))) {
					char *u = replace(S(g), " ", "_"),
					     *s = replace(S(v), S(g), u);
					int size = count(s, ' ') + 1;
					if (size >= 2 && size <= 5) {
						snprintf(k, sizeof(k), "%d",
							 size);
						push(d, k, s);
					}
					free(u);
					free(s);
				}
			}
		}
		DEL(grams);
		DEL(values);
	}
	EACH(k, d)
		sort_strings(k, -1);
	return d;
}

static J *rule(const char *canon, J *content, int distance)
{
	J *r = OBJ();
	PUT(r, "content", content);
	PUT(r, "distance", NUM(distance));
	PUT(r, "forward", BOOL(1));
	PUT(r, "reverse", BOOL(1));
	PUT(r, "canon", STR(canon));
	return r;
}

static void add_rule(J *d, const char *key, J *r, Map *seen, int include_key)
{
	char *json = cJSON_PrintUnformatted(r);
	Buf b = { 0 };
	if (include_key)
		buf_put(&b, key);
	buf_put(&b, json);
	free(json);
	if (!map_get(seen, b.p)) {
		map_put(seen, b.p, (void *)1);
		ADD(ensure(d, key, 1), r);
	} else
		DEL(r);
	free(b.p);
}

static void ordinary_rule(J *d, const char *canon, const char *term,
			  int distance, Map *seen)
{
	J *ts = split(term, "_"), *content = ARR();
	for (int i = 1; i < SIZE(ts); i++) {
		char *c = norm(S(AT(ts, i)), 0, 1);
		char *l = lower(c);
		if (!stopword(l))
			unique(content, c);
		free(l);
		free(c);
	}
	sort_strings(content, 0);
	if (SIZE(content))
		add_rule(d, S(AT(ts, 0)), rule(canon, content, distance), seen,
			 0);
	else
		DEL(content);
	DEL(ts);
}

/* Commas delimit opt-in lists, except between numeric code points (#15). */
static J *literal_parts(const char *text, int comma_lists)
{
	J *parts = ARR();
	const char *start = text, *p = text;
	uint32_t prev = 0;
	for (;;) {
		const char *at = p;
		uint32_t c = *p ? uread(&p) : 0;
		const char *next = p;
		if (!c ||
		    (comma_lists && c == ',' &&
		     !(unumeric(prev) && unumeric(*next ? uread(&next) : 0)))) {
			char *piece = slice(start, (size_t)(at - start));
			char *clean = norm(piece, 0, 0);
			if (*clean)
				ADD(parts, STR(clean));
			free(clean);
			free(piece);
			start = p;
		}
		if (!c)
			return parts;
		prev = c;
	}
}

J *generate_spans(J *raw, int distance, int plus_only, int comma_lists)
{
	/* Both the plus and ordinary passes consume the same list items. */
	if (comma_lists) {
		J *expanded = OBJ();
		EACH(k, raw) {
			J *values = ARR();
			EACH(v, k) {
				J *parts = literal_parts(S(v), 1);
				EACH(part, parts)
					unique(values, S(part));
				DEL(parts);
			}
			PUT(expanded, k->string, values);
		}
		J *out = generate_spans(expanded, distance, plus_only, 0);
		DEL(expanded);
		return out;
	}
	J *d = OBJ();
	Map seen = { 0 }, raw_index = { 0 };
	index_members(&raw_index, raw);
	EACH(k, raw) {
		J *syns = DUP(k);
		EACH(v, k) {
			J *ts = split(S(v), " ");
			char *working = copy(S(v));
			for (int i = 0; i + 1 < SIZE(ts); i++) {
				Buf b = { 0 };
				buf_put(&b, S(AT(ts, i)));
				buf_put(&b, "_");
				buf_put(&b, S(AT(ts, i + 1)));
				if (map_get(&raw_index, b.p)) {
					char *phrase = replace(b.p, "_", " ");
					buf_put(&b, " +");
					char *r = replace(working, phrase, b.p);
					free(working);
					working = r;
					free(phrase);
					if (working[strlen(working) - 1] != '+')
						unique(syns, working);
				}
				free(b.p);
			}
			free(working);
			DEL(ts);
		}
		EACH(v, syns) {
			if (!strchr(S(v), '+'))
				continue;
			J *ts = split(S(v), "+"), *content = ARR();
			for (int i = 1; i < SIZE(ts); i++) {
				char *c = norm(S(AT(ts, i)), 0, 1);
				unique(content, c);
				free(c);
			}
			sort_strings(content, 0);
			if (SIZE(content))
				add_rule(d, S(AT(ts, 0)),
					 rule(k->string, content, distance),
					 &seen, 1);
			else
				DEL(content);
			DEL(ts);
		}
		DEL(syns);
	}
	map_free(&seen);
	map_free(&raw_index);
	if (plus_only)
		return d;
	EACH(k, raw) {
		if (!strchr(k->string, ' ') && !strchr(k->string, '_'))
			continue;
		ordinary_rule(d, k->string, k->string, distance, &seen);
		J *values = ARR();
		EACH(v, k) {
			char *s = norm(S(v), 0, 0);
			unique(values, s);
			free(s);
		}
		sort_strings(values, 0);
		EACH(v, values) {
			if (!strchr(S(v), ' ') && !strchr(S(v), '_'))
				continue;
			char *l = lower(S(v)), *s = replace(l, " ", "_");
			if (strcmp(s, k->string))
				ordinary_rule(d, k->string, s, distance, &seen);
			free(l);
			free(s);
		}
		DEL(values);
	}
	map_free(&seen);
	J *trimmed = OBJ();
	EACH(k, d) {
		char *s = norm(k->string, 0, 0);
		set(trimmed, s, DUP(k));
		free(s);
	}
	DEL(d);
	return trimmed;
}

static J *trie_build(J *a)
{
	J *groups = OBJ(), *result = OBJ();
	EACH(v, a) {
		const char *s = S(v), *p = strchr(s, ' ');
		char *k = p ? slice(s, (size_t)(p - s)) : copy(s);
		push(groups, k, p ? p + 1 : "");
		free(k);
	}
	EACH(g, groups) {
		if (strchr(S(g->child), ' '))
			PUT(result, g->string, trie_build(g));
		else {
			J *x = DUP(g);
			sort_strings(x, -1);
			PUT(result, g->string, x);
		}
	}
	DEL(groups);
	return result;
}

J *generate_trie(J *entities)
{
	if (!SIZE(entities))
		return NIL();
	J *grams = OBJ(), *r = OBJ();
	EACH(v, entities) {
		char *s = replace(S(v), "_", " ");
		char k[16];
		snprintf(k, sizeof(k), "%d", count(s, ' ') + 1);
		push(grams, k, s);
		free(s);
	}
	for (int i = 1; i <= 6; i++) {
		char k[16];
		snprintf(k, sizeof(k), "%d", i);
		J *a = ensure(grams, k, 1);
		if (i == 1) {
			J *x = DUP(a);
			sort_strings(x, -1);
			PUT(r, k, x);
		} else
			PUT(r, k, trie_build(a));
	}
	EACH(k, grams) {
		if (atoi(k->string) > 6)
			PUT(r, k->string, DUP(k));
	}
	DEL(grams);
	return r;
}

static char *predicate_key(const char *p)
{
	const char *prefix = "";
	if (!strncmp(p, RDFS, strlen(RDFS)))
		prefix = "rdfs";
	else if (!strncmp(p, SKOS, strlen(SKOS)))
		prefix = "skos";
	else if (!strncmp(p, OWL, strlen(OWL)))
		prefix = "owl";
	else if (!strncmp(p, RDF, strlen(RDF)))
		prefix = "rdf";
	Buf b = { 0 };
	buf_put(&b, prefix);
	buf_put(&b, ":");
	buf_put(&b, local(p));
	return buf_take(&b);
}

J *ontology_build(Graph *g, int distance, int force_class, int comma_lists,
		  mc_error *e)
{
	J *r = OBJ(), *raw = OBJ(), *synraw = OBJ(), *by = OBJ(),
	  *labels = OBJ(), *ner = OBJ(), *parents = OBJ(), *children = OBJ(),
	  *equivs = OBJ(), *grams = OBJ(), *subentities = ARR(), *preds = ARR();
	Map label_index = { 0 }, ner_index = { 0 }, raw_index = { 0 },
	    synraw_index = { 0 }, parent_index = { 0 }, child_index = { 0 },
	    equiv_index = { 0 }, predicate_indexes = { 0 },
	    subentity_index = { 0 };
	int individuals = 0, subclass = 0, skos = 0;
	for (size_t i = 0; i < g->n; i++) {
		Triple *t = &g->ts[i];
		if (!strcmp(t->p.value, RDF "type")) {
			individuals |=
				!strcmp(t->o.value, OWL "NamedIndividual");
			skos |= !strcmp(t->o.value, SKOS "Concept");
		}
		subclass |= !strcmp(t->p.value, RDFS "subClassOf");
		if (!strcmp(t->p.value, RDFS "label")) {
			char *k = norm(local(t->s.value), 0, 0),
			     *v = norm(t->o.value, 0, 0);
			indexed_set(labels, &label_index, k, STR(v));
			indexed_set(ner, &ner_index, v, STR("NER"));
			free(k);
			free(v);
		}
	}
	int mixed = individuals && subclass && !skos && !force_class;
	for (size_t i = 0; i < g->n && !e->code; i++) {
		Triple *t = &g->ts[i];
		char *key = norm(local(t->s.value), 1, 0),
		     *pk = predicate_key(t->p.value);
		J *values = rdf_flatten(g, &t->o, 1, e);
		if (strcmp(pk, "rdfs:comment")) {
			J *pd = ensure(by, pk, 0);
			Map *subject_index = map_get(&predicate_indexes, pk);
			if (!subject_index) {
				subject_index =
					calloc(1, sizeof(*subject_index));
				map_put(&predicate_indexes, pk, subject_index);
			}
			J *subjects = rdf_flatten(g, &t->s, 1, e);
			EACH(subject, subjects) {
				const char *subject_key = S(subject);
				if (strcmp(subject_key, "class") &&
				    strcmp(subject_key, "nil")) {
					J *a = indexed_ensure(pd, subject_index,
							      subject_key, 1);
					EACH(v, values)
						if (strcmp(S(v), subject_key) &&
						    strcmp(S(v), "nil") &&
						    *S(v))
							unique(a, S(v));
				}
			}
			DEL(subjects);
			if (pk[0] == ':')
				unique(preds, pk);
		}
		int synonym = !strcmp(t->p.value, RDFS "label") ||
			      !strcmp(t->p.value, RDFS "seeAlso") ||
			      !strcmp(t->p.value, SKOS "altLabel") ||
			      !strcmp(local(t->p.value), "inflection");
		if (synonym) {
			EACH(v, values) {
				if (!*S(v) || !strcmp(S(v), "nil"))
					continue;
				indexed_push(raw, &raw_index, key, S(v));
				J *parts = literal_parts(S(v), comma_lists);
				EACH(p, parts) {
					char *s = norm(S(p), 0, 0);
					if (*s)
						indexed_push(synraw,
							     &synraw_index, key,
							     s);
					free(s);
				}
				DEL(parts);
			}
		}
		if (!strcmp(t->p.value, RDFS "subClassOf")) {
			unique_indexed(subentities, &subentity_index, key);
			const char *child = local(t->s.value);
			J *orig = rdf_flatten(g, &t->o, 0, e);
			EACH(v, orig) {
				if (!strcmp(S(v), "nil"))
					continue;
				if (map_get(&label_index, child) &&
				    (!mixed || t->o.kind == 0))
					indexed_push(parents, &parent_index,
						     child, S(v));
				if (map_get(&label_index, S(v)))
					indexed_push(children, &child_index,
						     S(v), child);
			}
			DEL(orig);
		}
		if (mixed && !strcmp(t->p.value, RDF "type") &&
		    t->o.kind == 0) {
			const char *child = local(t->s.value),
				   *parent = local(t->o.value);
			if (strcmp(parent, "NamedIndividual") &&
			    strcmp(parent, "Thing") &&
			    strcmp(parent, "Class") &&
			    strcmp(parent, "Ontology") &&
			    strcmp(parent, "ObjectProperty") &&
			    strcmp(parent, "DatatypeProperty") &&
			    strcmp(parent, "AnnotationProperty")) {
				if (map_get(&label_index, child))
					indexed_push(parents, &parent_index,
						     child, parent);
				J *types =
					rdf_values(g, t->s.value, RDF "type");
				int named = 0;
				EACH(v, types)
					named |= !strcmp(
						g->ts[(size_t)v->valuedouble]
							.o.value,
						OWL "NamedIndividual");
				if (named && map_get(&label_index, parent))
					indexed_push(children, &child_index,
						     parent, child);
				DEL(types);
			}
		}
		if (!strcmp(t->p.value, OWL "equivalentClass") &&
		    t->o.kind == 0 &&
		    map_get(&label_index, local(t->s.value)) &&
		    map_get(&label_index, local(t->o.value))) {
			char *v = lower(local(t->o.value));
			indexed_push(equivs, &equiv_index, key, v);
			indexed_push(equivs, &equiv_index, v, key);
			free(v);
		}
		DEL(values);
		free(pk);
		free(key);
	}
	map_free(&label_index);
	map_free(&ner_index);
	map_free(&raw_index);
	map_free(&synraw_index);
	map_free(&parent_index);
	map_free(&child_index);
	map_free(&equiv_index);
	map_free(&subentity_index);
	for (size_t i = 0; i < predicate_indexes.cap; i++) {
		Map *index = predicate_indexes.slots[i].value;
		if (index) {
			map_free(index);
			free(index);
		}
	}
	map_free(&predicate_indexes);
	EACH(p, by) {
		EACH(a, p)
			sort_strings(a, 0);
	}
	J *ngentities = ARR();
	if (mixed) {
		Map seen = { 0 };
		EACH(k, labels) {
			char *s = lower(k->string);
			unique_indexed(ngentities, &seen, s);
			free(s);
		}
		map_free(&seen);
	} else {
		DEL(ngentities);
		ngentities = DUP(subentities);
	}
	for (int i = 1; i <= 9; i++) {
		char k[8];
		snprintf(k, sizeof(k), "%d", i);
		J *a = ARR();
		EACH(v, ngentities)
			if (count(S(v), '_') == i - 1)
				ADD(a, STR(S(v)));
		if (!SIZE(a) && !mixed && !SIZE(ngentities)) {
			DEL(a);
			a = NIL();
		}
		PUT(grams, k, a);
	}
	DEL(ngentities);
	J *syns = OBJ(), *fwd = generate_synonyms(synraw, 0);
	PUT(syns, "lookup", generate_lookup(fwd));
	PUT(syns, "fwd", fwd);
	PUT(syns, "rev", generate_synonyms(synraw, 1));
	PUT(r, "children", children);
	PUT(r, "parents", parents);
	PUT(r, "trie", generate_trie(subentities));
	PUT(r, "ngrams", grams);
	PUT(r, "spans", generate_spans(raw, distance, 0, comma_lists));
	PUT(r, "labels", labels);
	if (!SIZE(equivs)) {
		DEL(equivs);
		equivs = NIL();
	}
	PUT(r, "equivalents", equivs);
	PUT(r, "predicates", preds);
	PUT(r, "by_predicate", by);
	PUT(r, "ner", ner);
	PUT(r, "synonyms", syns);
	DEL(raw);
	DEL(synraw);
	DEL(subentities);
	if (e->code) {
		DEL(r);
		return NULL;
	}
	return r;
}

static void traverse(J *edges, const char *node, J *out, Map *active, int depth,
		     mc_error *e)
{
	if (depth > 1024) {
		fail(e, 4, "Hierarchy exceeds 1024 levels");
		return;
	}
	if (map_get(active, node)) {
		fail(e, 4, "Cycle in ontology hierarchy at %s", node);
		return;
	}
	map_put(active, node, (void *)1);
	EACH(v, GET(edges, node)) {
		ADD(out, DUP(v));
		traverse(edges, S(v), out, active, depth + 1, e);
		if (e->code)
			break;
	}
	map_put(active, node, NULL);
}

static J *canonical(J *d, const char *s)
{
	J *f = GET(GET(d, "synonyms"), "fwd"),
	  *rev = GET(GET(d, "synonyms"), "rev");
	if (GET(f, s))
		return STR(s);
	J *v = GET(rev, s);
	if (v)
		return STR(cJSON_IsArray(v) ? S(v->child) : S(v));
	char *r = replace(s, "_", " ");
	v = GET(rev, r);
	free(r);
	if (v)
		return STR(cJSON_IsArray(v) ? S(v->child) : S(v));
	if (strchr(s, ' ') || strchr(s, '\'')) {
		char *a = replace(s, " ", "_"), *tmp = replace(a, "'", ""),
		     *b = norm(tmp, 1, 0);
		free(tmp);
		free(a);
		J *out = strcmp(s, b) ? canonical(d, b) : NIL();
		free(b);
		return out;
	}
	return NIL();
}

J *ontology_query(J *d, const char *method, const J *args, mc_error *e)
{
	const char *s = S(AT(args, 0));
	if (strstr(method, "_by_entity")) {
		const char *base =
			!strcmp(method, "requires_by_entity") ? "requires" :
			!strcmp(method, "required_by_entity") ? "required_by" :
			!strcmp(method, "similar_by_entity")  ? "similar" :
			!strcmp(method, "implies_by_entity")  ? "implies" :
			!strcmp(method, "implied_by_entity")  ? "implied_by" :
								NULL;
		if (base) {
			char *a = norm(s, 1, 0), *name = replace(a, " ", "_");
			free(a);
			J *view = ontology_query(d, base, NULL, e),
			  *v = GET(view, name);
			J *result =
				v ? DUP(v) :
				    (!strcmp(base, "similar") ? ARR() : NIL());
			if (!strcmp(base, "similar") && v)
				EACH(x, v)
					ADD(result, DUP(x));
			DEL(view);
			free(name);
			return result;
		}
	}
	if (!strcmp(method, "types_rev"))
		return ontology_query(d, "types", args, e);

	J *v = NULL;
	const char *key = method;
	if (!strcmp(method, "synonyms"))
		return DUP(GET(GET(d, "synonyms"), "fwd"));
	if (!strcmp(method, "synonyms_rev"))
		return DUP(GET(GET(d, "synonyms"), "rev"));
	if (!strcmp(method, "lookup") || !strcmp(method, "synonyms_lookup"))
		return DUP(GET(GET(d, "synonyms"), "lookup"));
	if (!strcmp(method, "find_canon"))
		return canonical(d, s);
	if (!strcmp(method, "is_canon"))
		return BOOL(GET(GET(GET(d, "synonyms"), "fwd"), s) != NULL);
	if (!strcmp(method, "is_variant"))
		return BOOL(GET(GET(GET(d, "synonyms"), "rev"), s) != NULL);
	if (!strcmp(method, "find_variants"))
		v = GET(GET(GET(d, "synonyms"), "fwd"), s);
	else if (!strcmp(method, "find_ner"))
		v = GET(GET(d, "ner"), s);
	else if (!strcmp(method, "label_by_entity")) {
		char *a = norm(s, 1, 0), *b = replace(a, " ", "_");
		v = GET(GET(d, "labels"), b);
		free(a);
		free(b);
	} else if (!strcmp(method, "by_predicate") ||
		   !strcmp(method, "by_predicate_rev")) {
		v = GET(GET(d, "by_predicate"), s);
		if (!strcmp(method, "by_predicate_rev")) {
			J *out = OBJ();
			EACH(k, v)
				if (SIZE(k))
					PUT(out, k->string, DUP(k));
			return out;
		}
		return v ? DUP(v) : ARR();
	} else if (!strcmp(method, "children") || !strcmp(method, "parents")) {
		v = GET(GET(d, method), s);
		return v ? DUP(v) : ARR();
	} else if (!strcmp(method, "ancestors") ||
		   !strcmp(method, "descendants") ||
		   strstr(method, "_and_self") ||
		   !strcmp(method, "has_ancestor") ||
		   !strcmp(method, "has_parent")) {
		int ancestor = !strncmp(method, "ancestor", 8) ||
			       !strncmp(method, "parent", 6) ||
			       strstr(method, "ancestor") ||
			       !strcmp(method, "has_parent");
		int direct = !strncmp(method, "children", 8) ||
			     !strncmp(method, "parents", 7) ||
			     !strcmp(method, "has_parent");
		J *out = ARR(),
		  *edges = GET(d, ancestor ? "parents" : "children");
		if (strstr(method, "_and_self"))
			ADD(out, STR(s));
		if (direct) {
			EACH(x, GET(edges, s))
				ADD(out, DUP(x));
		} else {
			Map active = { 0 };
			traverse(edges, s, out, &active, 0, e);
			map_free(&active);
		}
		if (!strncmp(method, "has_", 4)) {
			int yes = contains(out, S(AT(args, 1)));
			DEL(out);
			return BOOL(yes);
		}
		return out;
	} else if (!strcmp(method, "has_spans") ||
		   !strcmp(method, "has_data")) {
		J *value = !strcmp(method, "has_spans") ?
				   GET(d, "spans") :
				   GET(GET(d, "synonyms"), "fwd");
		return SIZE(value) ? NUM(SIZE(value)) :
		       value	   ? DUP(value) :
				     NIL();
	} else if (!strcmp(method, "entity_exists"))
		return BOOL(contains(GET(d, "entities"), s));
	else if (!strcmp(method, "span_keys")) {
		J *out = ARR();
		EACH(x, GET(d, "spans"))
			ADD(out, STR(x->string));
		sort_strings(out, 1);
		if (!SIZE(out)) {
			DEL(out);
			return NIL();
		}
		return out;
	} else if (!strcmp(method,
			   "ngrams")) { /* AskJsonAPI reads this location in the reference. */
		v = GET(GET(d, "synonyms"), s);
		return v ? DUP(v) : ARR();
	} else if (!strcmp(method, "types")) {
		v = GET(GET(d, "by_predicate"), "rdfs:subClassOf");
		return v ? DUP(v) : ARR();
	} else if (!strcmp(method, "labels_rev")) {
		J *out = OBJ();
		EACH(k, GET(d, "labels")) {
			const char *p = S(k);
			while (*p) {
				const char *start = p;
				uread(&p);
				char *c = slice(start, (size_t)(p - start));
				set(out, c, STR(k->string));
				free(c);
			}
		}
		return out;
	} else {
		if (!strcmp(method, "effects") ||
		    !strcmp(method, "effects_rev"))
			key = "effects";
		else if (!strcmp(method, "requires") ||
			 !strcmp(method, "required_by"))
			key = "requires";
		else if (!strcmp(method, "similar") ||
			 !strcmp(method, "similar_rev"))
			key = "similarTo";
		else if (!strcmp(method, "implies") ||
			 !strcmp(method, "implied_by"))
			key = "implies";
		else if (!strcmp(method, "uses") || !strcmp(method, "uses_rev"))
			key = "uses";
		if (strcmp(key, method) || !strcmp(method, "effects") ||
		    !strcmp(method, "requires") || !strcmp(method, "implies") ||
		    !strcmp(method, "uses")) {
			v = GET(GET(d, "by_predicate"), key);
			if (strstr(method, "_rev") ||
			    !strcmp(method, "required_by") ||
			    !strcmp(method, "implied_by")) {
				J *out = OBJ();
				EACH(k, v)
					if (SIZE(k))
						PUT(out, k->string, DUP(k));
				return out;
			}
			return v ? DUP(v) : ARR();
		}
		v = GET(d, method);
		if (!v) {
			fail(e, 4, "Unsupported ontology method: %s", method);
			return NULL;
		}
	}
	return v ? DUP(v) : NIL();
}
