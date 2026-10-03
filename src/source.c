/*
 * source.c - Native source selection and shared structured-source semantics.
 *
 * Format readers populate the same runtime graph. Matching and indexing stay
 * outside this layer; no reader serializes its input into another format.
 */
#include "source.h"

static int valid_iri(const char *text)
{
	while (*text) {
		uint32_t c = uread(&text);
		if (c <= 32 || (c < 128 && strchr("<>\"{}|^`\\", (int)c)))
			return 0;
	}
	return 1;
}

int source_init(Source *s, const char *base, mc_error *error)
{
	memset(s, 0, sizeof(*s));
	s->error = error;
	s->graph = rdf_create(error);
	s->base = copy(base);
	s->prefixes = s->graph ? DUP(s->graph->prefixes) : NULL;
	if (!s->graph || !s->base || !s->prefixes) {
		fail(error, 1, "Out of memory");
		source_clear(s);
		return 0;
	}
	return 1;
}

void source_clear(Source *s)
{
	rdf_free(s->graph);
	free(s->base);
	DEL(s->prefixes);
	memset(s, 0, sizeof(*s));
}

void source_base(Source *s, const char *value)
{
	if (!valid_iri(value)) {
		fail(s->error, 2, "Invalid base IRI");
		return;
	}
	char *resolved = uri_resolve(s->base, value);
	free(s->base);
	s->base = resolved;
}

void source_prefix(Source *s, const char *prefix, const char *value)
{
	if (!valid_iri(value) || !valid_iri(prefix) || strchr(prefix, ':')) {
		fail(s->error, 2, "Invalid namespace declaration: %s", prefix);
		return;
	}
	char *resolved = uri_resolve(s->base, value);
	set(s->prefixes, prefix, STR(resolved));
	rdf_bind_prefix(s->graph, prefix, resolved);
	free(resolved);
}

/* Explicit IRIs bypass compact names and the default namespace. */
Term source_resource(Source *s, const char *value, int explicit_iri)
{
	if (!valid_iri(value)) {
		fail(s->error, 2, "Invalid resource identifier");
		return rdf_term("", 0);
	}
	if (!explicit_iri && !strncmp(value, "_:", 2)) {
		if (!value[2])
			fail(s->error, 2,
			     "Blank node identifier must not be empty");
		return rdf_term(value, 2);
	}
	const char *colon = strchr(value, ':');
	char *prefix = colon ? slice(value, (size_t)(colon - value)) : copy("");
	J *namespace = explicit_iri ? NULL : GET(s->prefixes, prefix);
	free(prefix);
	char *resolved;
	if (namespace) {
		Buf iri = { 0 };
		buf_put(&iri, S(namespace));
		buf_put(&iri, colon ? colon + 1 : value);
		resolved = buf_take(&iri);
	} else
		resolved = uri_resolve(s->base, value);
	Term result = { resolved, NULL, NULL, 0 };
	return result;
}

const char *source_field(const char *key, int *kind)
{
	static const struct {
		const char *key, *predicate;
		int kind;
	} fields[] = {
		{ "type", RDF "type", 0 },
		{ "types", RDF "type", 0 },
		{ "label", RDFS "label", 1 },
		{ "synonyms", SKOS "altLabel", 1 },
		{ "see_also", RDFS "seeAlso", 1 },
		{ "spans", RDFS "seeAlso", 1 },
		{ "parents", RDFS "subClassOf", 0 },
		{ "equivalents", OWL "equivalentClass", 0 },
		{ "ner", OWL "backwardCompatibleWith", 1 },
		{ "inflections", ":inflection", 1 },
		{ "requires", ":requires", 0 },
		{ "implies", ":implies", 0 },
		{ "similar_to", ":similarTo", 0 },
		{ "uses", ":uses", 0 },
		{ "effects", ":effects", 0 },
	};
	for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); i++)
		if (!strcmp(key, fields[i].key)) {
			*kind = fields[i].kind;
			return fields[i].predicate;
		}
	return NULL;
}

void source_emit(Source *s, const Term *subject, const Term *predicate,
		 Term *object)
{
	if (s->error->code)
		return;
	if (subject->kind == 1 || predicate->kind != 0) {
		fail(s->error, 2,
		     "A subject must be a resource; a predicate must be an IRI");
		return;
	}
	literal_normalize(object);
	rdf_add(s->graph, subject, predicate, object);
}

Graph *source_read(const char *text, const char *format, const char *base,
		   J **snapshot, mc_error *error)
{
	*snapshot = NULL;
	if (strlen(text) > 256u * 1024u * 1024u) {
		fail(error, 2, "Source exceeds 256 MiB input limit");
		return NULL;
	}
	if (!format || !*format || !strcmp(format, "auto")) {
		const char *p = text;
		while (*p && isspace((unsigned char)*p))
			p++;
		/* A Turtle blank-node subject can also begin with '['. */
		int array = 0;
		if (*p == '[') {
			const char *item = p + 1;
			while (*item && isspace((unsigned char)*item))
				item++;
			array = !*item || *item == '{';
			if (*item == ']') {
				item++;
				while (*item && isspace((unsigned char)*item))
					item++;
				array = !*item;
			}
		}
		if (array || *p == '{')
			return json_graph_read(text, base, 0, snapshot, error);
		return rdf_parse(text, base, error);
	}
	if (!strcmp(format, "turtle") || !strcmp(format, "ttl"))
		return rdf_parse(text, base, error);
	if (!strcmp(format, "json"))
		return json_graph_read(text, base, 1, snapshot, error);
	if (!strcmp(format, "jsonl"))
		return json_graph_read(text, base, 2, snapshot, error);
	if (!strcmp(format, "snapshot")) {
		*snapshot = json_parse(text, error);
		return NULL;
	}
	fail(error, 2, "Unsupported source format: %s", format);
	return NULL;
}
