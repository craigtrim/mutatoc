/*
 * views.c - Ontology query views and collection merging.
 *
 * Builds live views and combines snapshots for collection queries.
 */

#include "mc.h"

J *graph_predicate(Graph *g, const char *predicate, int lc, int rev,
		   mc_error *err)
{
	/* Hash label groups for large vocabularies (craigtrim/axiom#129). */
	J *out = OBJ();
	Map keys = { 0 };
	size_t rows = 0;
	for (size_t i = 0; i < g->n; i++)
		rows += !strcmp(g->ts[i].p.value, predicate);
	map_reserve(&keys, rows);
	for (size_t i = 0; i < g->n && !err->code; i++) {
		Triple *t = &g->ts[i];
		if (strcmp(t->p.value, predicate))
			continue;
		Term *terms[] = { rev ? &t->o : &t->s, rev ? &t->s : &t->o };
		J arrays[2] = { { 0 } }, values[2] = { { 0 } }, *flat[2];
		char *owned[2] = { NULL, NULL };
		/* Ordinary terms flatten to one string. The output copies it, so
		 * temporary JSON wrappers can stay on the stack. */
		for (int j = 0; j < 2; j++) {
			if (terms[j]->kind == 2)
				flat[j] = rdf_flatten(g, terms[j], lc, err);
			else {
				values[j].type = cJSON_String;
				values[j].valuestring = (char *)norm_view(
					terms[j]->kind == 0 ?
						      local(terms[j]->value) :
						      terms[j]->value,
					lc, 0, &owned[j]);
				arrays[j].type = cJSON_Array;
				arrays[j].child = &values[j];
				flat[j] = &arrays[j];
			}
		}
		EACH(k, flat[0]) {
			if (!*S(k) || !strcmp(S(k), "nil") ||
			    !strcmp(S(k), "class"))
				continue;
			J *a = indexed_ensure(out, &keys, S(k), 1);
			EACH(v, flat[1])
				if (*S(v) && strcmp(S(v), S(k)) &&
				    strcmp(S(v), "nil"))
					unique(a, S(v));
		}
		for (int j = 0; j < 2; j++) {
			if (terms[j]->kind == 2)
				DEL(flat[j]);
			else
				free(owned[j]);
		}
	}
	map_free(&keys);
	EACH(k, out)
		sort_strings(k, 0);
	if (!rows)
		fail(err, 4, "Predicate query returned no rows: %s", predicate);
	if (err->code) {
		DEL(out);
		return NULL;
	}
	return out;
}

static void closure(Graph *g, const char *node, J *out)
{
	if (contains(out, node))
		return;
	unique(out, node);
	J *values = rdf_values(g, node, RDFS "subClassOf");
	EACH(v, values)
		closure(g, g->ts[(size_t)v->valuedouble].o.value, out);
	DEL(values);
}

static J *reverse_strings(J *d, int chars)
{
	J *r = OBJ();
	EACH(k, d) {
		if (chars) {
			const char *p = S(k);
			while (*p) {
				const char *a = p;
				uread(&p);
				char *s = slice(a, (size_t)(p - a));
				ADD(ensure(r, s, 1), STR(k->string));
				free(s);
			}
		} else
			EACH(v, k)
				ADD(ensure(r, S(v), 1), STR(k->string));
	}
	return r;
}

static void ner_views(Graph *g, J *d, mc_error *err)
{
	J *roots = ARR(), *tags = ARR(), *depth = OBJ(), *tax = OBJ();
	for (size_t i = 0; i < g->n; i++) {
		Triple *t = &g->ts[i];
		if (!strcmp(t->p.value, OWL "backwardCompatibleWith"))
			ADD(tags, NUM((double)i));
		if (strcmp(t->p.value, RDF "type") ||
		    strcmp(t->o.value, OWL "Class"))
			continue;
		int root = 1;
		J *parents = rdf_values(g, t->s.value, RDFS "subClassOf");
		EACH(p, parents)
			if (strcmp(g->ts[(size_t)p->valuedouble].o.value,
				   t->s.value))
				root = 0;
		DEL(parents);
		if (root)
			unique(roots, t->s.value);
	}
	J *counts = OBJ();
	EACH(tag, tags) {
		Triple *t = &g->ts[(size_t)tag->valuedouble];
		J *labels = rdf_flatten(g, &t->o, 0, err), *ancestors = ARR();
		closure(g, t->s.value, ancestors);
		size_t count = 0;
		EACH(mid, ancestors) {
			J *reachable = ARR();
			closure(g, S(mid), reachable);
			EACH(root, roots)
				if (contains(reachable, S(root)))
					count++;
			DEL(reachable);
		}
		EACH(label, labels) {
			char *key = upper(S(label));
			if (count) {
				J *old = GET(counts, key);
				set(counts, key,
				    NUM((old ? old->valuedouble : 0) +
					(double)count));
			}
			EACH(ancestor, ancestors) {
				J *values = rdf_values(
					g, S(ancestor),
					OWL "backwardCompatibleWith");
				EACH(v, values) {
					J *names = rdf_flatten(
						g,
						&g->ts[(size_t)v->valuedouble].o,
						0, err);
					EACH(name, names) {
						char *s = upper(S(name));
						J *a = ARR();
						ADD(a, STR(s));
						set(tax, key, a);
						free(s);
					}
					DEL(names);
				}
				DEL(values);
			}
			free(key);
		}
		DEL(labels);
		DEL(ancestors);
	}
	if (SIZE(counts)) {
		PUT(depth, "NER", STR("0"));
		EACH(k, counts) {
			char n[32];
			snprintf(n, sizeof(n), "%.0f", k->valuedouble);
			set(depth, k->string, STR(n));
		}
	}
	if (!SIZE(depth)) {
		DEL(depth);
		depth = NIL();
	}
	if (!SIZE(tax)) {
		DEL(tax);
		tax = NIL();
	}
	set(d, "ner_depth", depth);
	set(d, "ner_taxonomy", tax);
	set(d, "ner_depth_rev",
	    cJSON_IsNull(depth) ? NIL() : reverse_strings(depth, 1));
	set(d, "ner_taxonomy_rev",
	    cJSON_IsNull(tax) ? NIL() : reverse_strings(tax, 0));
	DEL(roots);
	DEL(tags);
	DEL(counts);
}

J *external_synonyms(const char *text)
{
	J *result = OBJ(), *lines = split(text, "\n");
	EACH(line, lines) {
		char *clean = norm(S(line), 1, 0);
		char *first = strchr(clean, '~'), *last = strrchr(clean, '~');
		if (!*clean || *clean == '#' || !first) {
			free(clean);
			continue;
		}
		char *key = slice(clean, (size_t)(first - clean));
		J *values = split(last + 1, ","), *variants = ARR();
		EACH(v, values) {
			char *s = norm(S(v), 0, 0);
			if (*s)
				unique(variants, s);
			free(s);
		}
		sort_strings(variants, 1);
		J *descending = ARR();
		while (SIZE(variants))
			ADD(descending, cJSON_DetachItemFromArray(
						variants, SIZE(variants) - 1));
		set(result, key, descending);
		DEL(variants);
		DEL(values);
		free(key);
		free(clean);
	}
	DEL(lines);
	return result;
}

J *merge_views(J *out, const J *source)
{
	if (cJSON_IsObject(out) && cJSON_IsObject(source)) {
		EACH(k, source) {
			J *value = GET(out, k->string);
			if (!value || cJSON_IsNull(value))
				set(out, k->string, DUP(k));
			else
				merge_views(value, k);
		}
	} else if (cJSON_IsArray(out) && cJSON_IsArray(source)) {
		EACH(v, source) {
			int found = 0;
			EACH(old, out)
				if (cJSON_Compare(old, v, 1)) {
					found = 1;
					break;
				}
			if (!found)
				ADD(out, DUP(v));
		}
	}
	return out;
}

/* Keep independent JSON nodes while sharing snapshot string storage. Edits
 * replace the data-view nodes before releasing their snapshot counterparts. */
static J *live_value_copy(J *source)
{
	/* Allocate through cJSON's hooks, preserving scalar fields exactly. */
	J *out = NIL();
	if (!out)
		return NULL;
	*out = *source;
	out->next = out->prev = out->child = NULL;
	out->type &= ~(cJSON_IsReference | cJSON_StringIsConst);
	if (out->valuestring)
		out->type |= cJSON_IsReference;
	if (out->string)
		out->type |= cJSON_StringIsConst;
	EACH(child, source) {
		J *part = live_value_copy(child);
		if (!part || !cJSON_AddItemToArray(out, part)) {
			DEL(part);
			DEL(out);
			return NULL;
		}
	}
	return out;
}

/* Vocabulary edits leave structural predicate views unchanged. The data view
 * can reference those objects until a transactional rebuild replaces both views. */
static J *live_predicate_copy(J *predicates)
{
	J *out = cJSON_Duplicate(predicates, 0);
	if (!out)
		return NULL;
	EACH(member, predicates) {
		const char *colon = strrchr(member->string, ':');
		int mutable = !strcmp(member->string, "rdfs:label") ||
			      !strcmp(member->string, "rdfs:seeAlso") ||
			      !strcmp(member->string, "skos:altLabel") ||
			      (colon && !strcmp(colon + 1, "inflection"));
		J *part = !mutable && cJSON_IsObject(member) ?
					cJSON_CreateObjectReference(member->child) :
					live_value_copy(member);
		if (!part ||
		    !cJSON_AddItemToObject(out, member->string, part)) {
			DEL(part);
			DEL(out);
			return NULL;
		}
	}
	return out;
}

/* Already lexical synonym buckets need independent wrappers only. Copy an
 * affected bucket before appending external forms or applying an edit. */
static J *live_synonyms_copy(J *source)
{
	J *out = cJSON_Duplicate(source, 0);
	if (!out)
		return NULL;
	EACH(member, source) {
		int sorted = cJSON_IsArray(member);
		for (J *v = member->child; sorted && v && v->next; v = v->next)
			sorted = strcmp(S(v), S(v->next)) < 0;
		J *part = sorted ? cJSON_CreateArrayReference(member->child) :
					 live_value_copy(member);
		if (!part ||
		    !cJSON_AddItemToObjectCS(out, member->string, part)) {
			DEL(part);
			DEL(out);
			return NULL;
		}
	}
	return out;
}
static J *live_synonym_array(J *object, const char *key)
{
	J *array = ensure(object, key, 1);
	if (array->type & cJSON_IsReference) {
		J *owned = DUP(array);
		set(object, key, owned);
		array = owned;
	}
	return array;
}

/* The data view rebuilds lookup after sorting its forward synonyms (#129).
 * Keep the original member position without copying entries that are discarded. */
static J *live_snapshot_copy(J *snapshot)
{
	if (!cJSON_IsObject(snapshot))
		return DUP(snapshot);
	J *copy = cJSON_Duplicate(snapshot, 0),
	  *synonyms = GET(snapshot, "synonyms");
	if (!copy)
		return NULL;
	EACH(field, snapshot) {
		J *value = NULL;
		if (field == synonyms && cJSON_IsObject(field)) {
			value = cJSON_Duplicate(field, 0);
			J *lookup = GET(field, "lookup");
			if (value)
				EACH(member, field) {
					J *part;
					if (member == lookup)
						part = NIL();
					else if (cJSON_IsObject(member) &&
						 (!strcmp(member->string,
							  "fwd") ||
						  !strcmp(member->string,
							  "rev")))
						part = live_synonyms_copy(
							member);
					else
						part = live_value_copy(member);
					if (!part ||
					    !cJSON_AddItemToObject(
						    value, member->string,
						    part)) {
						DEL(part);
						DEL(value);
						value = NULL;
						break;
					}
				}
		} else if (!strcmp(field->string, "by_predicate") &&
			   cJSON_IsObject(field))
			value = live_predicate_copy(field);
		else
			value = live_value_copy(field);
		if (!value ||
		    !cJSON_AddItemToObject(copy, field->string, value)) {
			DEL(value);
			DEL(copy);
			return NULL;
		}
	}
	return copy;
}

typedef struct {
	mc_engine *engine;
	J *view, *metadata;
	mc_error metadata_error, lookup_error;
} LiveBuild;
static void live_metadata(void *context)
{
	LiveBuild *job = context;
	mc_engine *e = job->engine;
	J *d = job->metadata;
	mc_error *err = &job->metadata_error;
	mc_error local_error = { 0 };
	J *labels = graph_predicate(e->graph, RDFS "label", 0, 0, &local_error);
	set(d, "_labels", labels ? labels : NIL());
	const char *ns = S(GET(e->graph->prefixes, e->name));
	set(d, "_namespace_bound", BOOL(*ns != 0));
	J *custom = OBJ();
	if (*ns) {
		const char *names[] = { "effects", "requires", "similarTo",
					"implies", "uses" };
		for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++) {
			Buf full = { 0 };
			buf_put(&full, ns);
			buf_put(&full, names[i]);
			mc_error ignored = { 0 };
			J *view = graph_predicate(e->graph, full.p, 1, 0,
						  &ignored);
			free(full.p);
			if (view)
				PUT(custom, names[i], view);
		}
	}
	set(d, "_custom_predicates", custom);
	set(d, "_live", BOOL(1));
	J *entities = ARR(),
	  *types = GET(GET(job->view, "by_predicate"), "rdfs:subClassOf");
	Map seen = { 0 };
	EACH(k, types) {
		unique_indexed(entities, &seen, k->string);
		EACH(v, k)
			unique_indexed(entities, &seen, S(v));
	}
	map_free(&seen);
	set(d, "entities", entities);
	ner_views(e->graph, d, err);
}

static void live_lookup(void *context)
{
	LiveBuild *job = context;
	mc_engine *e = job->engine;
	J *d = job->view;
	mc_error *err = &job->lookup_error;
	J *fwd = GET(GET(d, "synonyms"), "fwd"),
	  *rev = GET(GET(d, "synonyms"), "rev");
	if (e->source_path && *e->source_path) {
		Buf path = { 0 };
		buf_put(&path, e->source_path);
		buf_put(&path, ".txt");
		FILE *f = mc_fopen(path.p, "rb");
		if (f) {
			fclose(f);
			char *text = read_file(path.p, err);
			J *external = text ? external_synonyms(text) : NULL;
			free(text);
			EACH(k, external) {
				EACH(v, k) {
					unique(live_synonym_array(fwd,
								  k->string),
					       S(v));
					unique(live_synonym_array(rev, S(v)),
					       k->string);
				}
			}
			DEL(external);
		}
		free(path.p);
	}
	EACH(k, fwd)
		if (!(k->type & cJSON_IsReference))
			sort_strings(k, 0);
	EACH(k, rev)
		if (!(k->type & cJSON_IsReference))
			sort_strings(k, 0);
	set(GET(d, "synonyms"), "lookup",
	    generate_lookup_shared(fwd, GET(GET(e->snapshot, "synonyms"),
					    "lookup")));
}

J *live_view(mc_engine *e, mc_error *err)
{
	if (e->live)
		return e->live;
	if (e->part_count) {
		J *merged = OBJ();
		int bound = 0;
		for (size_t i = 0; i < e->part_count; i++) {
			J *part = live_view(e->parts[i], err);
			if (!part) {
				DEL(merged);
				return NULL;
			}
			merge_views(merged, part);
			bound |= cJSON_IsTrue(GET(part, "_namespace_bound"));
		}
		set(merged, "_namespace_bound", BOOL(bound));
		J *syns = GET(merged, "synonyms");
		set(syns, "lookup", generate_lookup(GET(syns, "fwd")));
		e->live = merged;
		return merged;
	}
	J *d = live_snapshot_copy(e->snapshot);
	if (!d || !e->graph) {
		DEL(d);
		fail(err, 4, "Live finder requires a loaded RDF graph");
		return NULL;
	}

	LiveBuild job = { e, d, OBJ(), { 0 }, { 0 } };
	parallel_three(live_metadata, live_lookup, NULL, &job,
		       e->graph->subjects.size >= 4096);
	while (job.metadata->child) {
		J *member = cJSON_DetachItemFromArray(job.metadata, 0);
		char *key = copy(member->string);
		set(d, key, member);
		free(key);
	}
	DEL(job.metadata);
	if (!err->code && job.metadata_error.code)
		*err = job.metadata_error;
	else if (!err->code && job.lookup_error.code)
		*err = job.lookup_error;
	if (err->code) {
		DEL(d);
		return NULL;
	}
	e->live = d;
	return d;
}
