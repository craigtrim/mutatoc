/* Atomic subject replacement through the protocol (craigtrim/axiom#129). */
#include "mc.h"

static int entity_iri(const char *s)
{
	if (!s || !*s || !strchr(s, ':') || !strncmp(s, "_:", 2))
		return 0;
	if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z')))
		return 0;
	for (const char *p = s + 1; *p != ':'; p++)
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
		      (*p >= '0' && *p <= '9') || strchr("+.-", *p)))
			return 0;
	for (; *s; s++)
		if ((unsigned char)*s <= 32 || strchr("<>\"{}|^`\\", *s))
			return 0;
	return 1;
}

static int select_entity(Map *selected, const char *iri, mc_error *err)
{
	if (!entity_iri(iri)) {
		fail(err, 2, "update requires absolute entity IRIs");
		return 0;
	}
	if (map_get(selected, iri)) {
		fail(err, 2, "Entity occurs more than once in update: %s", iri);
		return 0;
	}
	map_put(selected, iri, (void *)1);
	return 1;
}

J *ontology_update(mc_engine *e, J *q, mc_error *err)
{
	if (!e->graph || e->part_count) {
		fail(err, 4, "update requires a single loaded RDF graph");
		return NULL;
	}
	J *replace = GET(q, "replace"), *remove = GET(q, "remove");
	if ((!replace && !remove) || (replace && !cJSON_IsObject(replace)) ||
	    (remove && !cJSON_IsArray(remove))) {
		fail(err, 2,
		     "update requires a replace object and/or a remove array");
		return NULL;
	}
	Map selected = { 0 };
	Graph *added = rdf_create(err), *next = NULL;
	int replaced = 0, removed = 0;
	EACH(v, remove) {
		if (!cJSON_IsString(v) ||
		    !select_entity(&selected, S(v), err)) {
			if (!err->code)
				fail(err, 2,
				     "remove entries must be entity IRIs");
			goto done;
		}
		removed++;
	}
	EACH(v, replace) {
		if (!cJSON_IsString(v) ||
		    !select_entity(&selected, v->string, err)) {
			if (!err->code)
				fail(err, 2,
				     "replace values must be Turtle strings");
			goto done;
		}
		/* Prefixes already loaded are available in each independent fragment. */
		Buf text = { 0 };
		EACH(prefix, e->graph->prefixes) {
			buf_put(&text, "@prefix ");
			buf_put(&text, prefix->string);
			buf_put(&text, ": <");
			buf_put(&text, S(prefix));
			buf_put(&text, "> .\n");
		}
		buf_put(&text, S(v));
		Graph *part = rdf_parse(text.p, v->string, err);
		free(text.p);
		if (!part)
			goto done;
		for (size_t i = 0; i < part->n; i++) {
			Triple *t = &part->ts[i];
			if (t->s.kind != 0 || strcmp(t->s.value, v->string) ||
			    t->o.kind == 2) {
				fail(err, 2,
				     "Replacement statements must have the specified IRI as subject and no blank nodes");
				break;
			}
			rdf_add(added, &t->s, &t->p, &t->o);
		}
		rdf_free(part);
		if (err->code)
			goto done;
		replaced++;
	}
	if (!selected.size) {
		rdf_free(added);
		map_free(&selected);
		J *result = OBJ();
		PUT(result, "replaced", NUM(0));
		PUT(result, "removed", NUM(0));
		return result;
	}
	if (update_fast(e, added, &selected))
		goto done;
	next = rdf_create(err);
	if (!next)
		goto done;
	DEL(next->prefixes);
	next->prefixes = DUP(e->graph->prefixes);
	Map emitted = { 0 };
	for (size_t i = 0; i < e->graph->n; i++) {
		Triple *t = &e->graph->ts[i];
		if (!map_get(&selected, t->s.value))
			rdf_add(next, &t->s, &t->p, &t->o);
		else if (!map_get(&emitted, t->s.value)) {
			J *ids = map_get(&added->subjects, t->s.value);
			EACH(id, ids) {
				Triple *v = &added->ts[(size_t)id->valuedouble];
				rdf_add(next, &v->s, &v->p, &v->o);
			}
			map_put(&emitted, t->s.value, (void *)1);
		}
	}
	for (size_t i = 0; i < added->n; i++) {
		Triple *t = &added->ts[i];
		if (!map_get(&emitted, t->s.value))
			rdf_add(next, &t->s, &t->p, &t->o);
	}
	map_free(&emitted);
	mc_engine staged = *e;
	staged.graph = next;
	staged.live = NULL;
	staged.index = NULL;
	staged.updates = NULL;
	staged.snapshot = e->graph_only ? OBJ() :
						ontology_build(next, e->distance,
							 e->force_class,
							 e->comma_lists, err);
	if (e->graph_only)
		PUT(staged.snapshot, "synonyms", OBJ());
	if (!err->code && !e->graph_only) {
		J *view = e->live_mode ? live_view(&staged, err) :
					       staged.snapshot;
		if (view && !err->code)
			staged.index = match_index_build(view);
		if (!err->code && !staged.index)
			fail(err, 5, "Cannot allocate updated match index");
	}
	if (err->code) {
		match_index_free(staged.index);
		DEL(staged.live);
		DEL(staged.snapshot);
		goto done;
	}
	update_state_free(e->updates);
	e->updates = NULL;
	match_index_free(e->index);
	DEL(e->live);
	DEL(e->snapshot);
	rdf_free(e->graph);
	e->graph = next;
	next = NULL;
	e->snapshot = staged.snapshot;
	e->live = staged.live;
	e->index = staged.index;
	update_state_build(e);
done:
	rdf_free(added);
	rdf_free(next);
	map_free(&selected);
	if (err->code)
		return NULL;
	J *result = OBJ();
	PUT(result, "replaced", NUM(replaced));
	PUT(result, "removed", NUM(removed));
	return result;
}
