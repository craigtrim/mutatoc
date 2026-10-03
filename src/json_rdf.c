/*
 * json_rdf.c - Record-at-a-time JSON and JSONL ontology reader.
 *
 * A JSON array and a JSONL stream carry the same records. Each temporary
 * record is released immediately after its facts enter the runtime graph.
 */
#include "source.h"

static int members(Source *s, const J *object, const char *allowed)
{
	if (!cJSON_IsObject(object)) {
		fail(s->error, 2, "Expected a JSON object");
		return 0;
	}
	Map seen = { 0 };
	EACH(field, object) {
		const char *key = field->string;
		if (map_get(&seen, key)) {
			fail(s->error, 2, "Duplicate field: %s", key);
			break;
		}
		map_put(&seen, key, (void *)1);
		if (allowed) {
			Buf needle = { 0 };
			buf_put(&needle, "|");
			buf_put(&needle, key);
			buf_put(&needle, "|");
			int valid = *key && !strchr(key, '|') &&
				    strstr(allowed, needle.p) != NULL;
			free(needle.p);
			if (!valid) {
				fail(s->error, 2, "Unknown field: %s", key);
				break;
			}
		}
	}
	map_free(&seen);
	return !s->error->code;
}

static int string(Source *s, const J *value, const char *field)
{
	if (cJSON_IsString(value))
		return 1;
	fail(s->error, 2, "%s must be a string", field);
	return 0;
}

static int language(const char *p)
{
	if (!isalpha((unsigned char)*p))
		return 0;
	while (isalpha((unsigned char)*p))
		p++;
	while (*p == '-') {
		p++;
		if (!isalnum((unsigned char)*p))
			return 0;
		while (isalnum((unsigned char)*p))
			p++;
	}
	return !*p;
}

static Term blank_node(Source *s, const char *name)
{
	Buf label = { 0 };
	if (strncmp(name, "_:", 2))
		buf_put(&label, "_:");
	buf_put(&label, name);
	Term t = source_resource(s, label.p, 0);
	free(label.p);
	return t;
}

static Term read_term(Source *s, const J *value, int default_kind)
{
	Term result = { 0 };
	if (cJSON_IsString(value))
		return default_kind == 0 ? source_resource(s, S(value), 0) :
					   rdf_term(S(value), 1);
	if (!members(s, value, "|id|iri|blank|value|kind|datatype|language|"))
		return result;
	J *id = GET(value, "id"), *iri = GET(value, "iri"),
	  *blank = GET(value, "blank"), *text = GET(value, "value"),
	  *kind = GET(value, "kind"), *datatype = GET(value, "datatype"),
	  *lang = GET(value, "language");
	if (!!id + !!iri + !!blank + !!text != 1) {
		fail(s->error, 2,
		     "A term needs exactly one of id, iri, blank, or value");
		return result;
	}
	const J *lexical = id ? id : iri ? iri : blank ? blank : text;
	if (!string(s, lexical, "Term value"))
		return result;
	if (!text && (kind || datatype || lang)) {
		fail(s->error, 2,
		     "Resource terms cannot carry literal attributes");
		return result;
	}
	if (id)
		return source_resource(s, S(id), 0);
	if (iri)
		return source_resource(s, S(iri), 1);
	if (blank)
		return blank_node(s, S(blank));
	if (kind && (!string(s, kind, "kind") ||
		     (strcmp(S(kind), "iri") && strcmp(S(kind), "blank") &&
		      strcmp(S(kind), "literal")))) {
		fail(s->error, 2, "kind must be iri, blank, or literal");
		return result;
	}
	if (kind && strcmp(S(kind), "literal")) {
		if (datatype || lang) {
			fail(s->error, 2,
			     "Resource terms cannot carry literal attributes");
			return result;
		}
		return !strcmp(S(kind), "iri") ?
			       source_resource(s, S(text), 1) :
			       blank_node(s, S(text));
	}
	result = rdf_term(S(text), 1);
	if (datatype && string(s, datatype, "datatype")) {
		Term dt = source_resource(s, S(datatype), 0);
		if (dt.kind != 0)
			fail(s->error, 2, "Literal datatype must be an IRI");
		result.datatype = dt.value;
		dt.value = NULL;
		rdf_term_free(&dt);
	}
	if (lang && string(s, lang, "language")) {
		if (!language(S(lang)))
			fail(s->error, 2, "Invalid language tag");
		if (result.datatype &&
		    strcmp(result.datatype, RDF "langString"))
			fail(s->error, 2,
			     "A language-tagged literal requires rdf:langString");
		result.language = copy(S(lang));
		if (!result.datatype)
			result.datatype = copy(RDF "langString");
	}
	return result;
}

static void values(Source *s, const Term *subject, const Term *predicate,
		   const J *data, int kind)
{
	if (!data) {
		fail(s->error, 2, "Missing fact value");
		return;
	}
	const J *value = cJSON_IsArray(data) ? data->child : data;
	for (; value && !s->error->code; value = value->next) {
		Term object = read_term(s, value, kind);
		source_emit(s, subject, predicate, &object);
		rdf_term_free(&object);
		if (!cJSON_IsArray(data))
			break;
	}
}

static void facts(Source *s, const Term *subject, const J *data)
{
	if (!cJSON_IsArray(data)) {
		fail(s->error, 2, "facts must be an array");
		return;
	}
	EACH(fact, data) {
		if (!members(s, fact, "|predicate|value|"))
			return;
		Term predicate = read_term(s, GET(fact, "predicate"), 0);
		values(s, subject, &predicate, GET(fact, "value"), 1);
		rdf_term_free(&predicate);
		if (s->error->code)
			return;
	}
}

static void entity(Source *s, const J *record)
{
	Term subject = read_term(s, GET(record, "id"), 0);
	EACH(field, record) {
		if (s->error->code)
			break;
		if (!strcmp(field->string, "id"))
			continue;
		if (!strcmp(field->string, "facts")) {
			facts(s, &subject, field);
			continue;
		}
		if (!strcmp(field->string, "properties")) {
			if (!members(s, field, NULL))
				break;
			EACH(property, field) {
				Term predicate =
					source_resource(s, property->string, 0);
				values(s, &subject, &predicate, property, 1);
				rdf_term_free(&predicate);
				if (s->error->code)
					break;
			}
			continue;
		}
		int kind;
		const char *name = source_field(field->string, &kind);
		if (!name) {
			fail(s->error, 2,
			     "Unknown entity field: %s; use properties for RDF predicates",
			     field->string);
			break;
		}
		if (*name == ':' && !GET(s->prefixes, "")) {
			fail(s->error, 2, "%s requires a default namespace",
			     field->string);
			break;
		}
		Term predicate = *name == ':' ? source_resource(s, name, 0) :
						rdf_term(name, 0);
		values(s, &subject, &predicate, field, kind);
		rdf_term_free(&predicate);
	}
	if (subject.kind == 1)
		fail(s->error, 2, "Entity id must be a resource");
	rdf_term_free(&subject);
}

static void metadata(Source *s, const J *record)
{
	if (!members(s, record, "|format|base|namespace|prefixes|"))
		return;
	if (!SIZE(record)) {
		fail(s->error, 2, "Empty source record");
		return;
	}
	J *format = GET(record, "format"), *base = GET(record, "base"),
	  *ns = GET(record, "namespace"), *prefixes = GET(record, "prefixes");
	if (format &&
	    (!string(s, format, "format") || strcmp(S(format), "mutatoc/1")))
		fail(s->error, 2,
		     "Unsupported ontology schema; expected mutatoc/1");
	if (base && string(s, base, "base"))
		source_base(s, S(base));
	if (ns && string(s, ns, "namespace"))
		source_prefix(s, "", S(ns));
	if (prefixes && members(s, prefixes, NULL)) {
		if (ns && GET(prefixes, ""))
			fail(s->error, 2,
			     "Declare the default namespace only once per record");
		EACH(prefix, prefixes) {
			if (s->error->code ||
			    !string(s, prefix, "Namespace IRI"))
				break;
			source_prefix(s, prefix->string, S(prefix));
		}
	}
}

static void record(Source *s, const J *value)
{
	if (!members(s, value, NULL))
		return;
	if (GET(value, "id")) {
		entity(s, value);
		return;
	}
	if (!GET(value, "subject")) {
		metadata(s, value);
		return;
	}
	if (!members(s, value, "|subject|predicate|object|"))
		return;
	Term subject = read_term(s, GET(value, "subject"), 0),
	     predicate = read_term(s, GET(value, "predicate"), 0),
	     object = read_term(s, GET(value, "object"), 1);
	source_emit(s, &subject, &predicate, &object);
	rdf_term_free(&subject);
	rdf_term_free(&predicate);
	rdf_term_free(&object);
}

static const char *whitespace(const char *p, const char *end)
{
	while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
		p++;
	return p;
}

static void location(mc_error *error, const char *text, const char *at)
{
	error->line = error->column = 1;
	for (const char *p = text; p < at; p++)
		if (*p == '\n') {
			error->line++;
			error->column = 1;
		} else
			error->column++;
}

/* framing: 0 autodetection/legacy snapshots, 1 JSON array, 2 JSON Lines. */
Graph *json_graph_read(const char *text, const char *base, int framing,
		       J **snapshot, mc_error *error)
{
	Source source;
	if (!source_init(&source, base, error))
		return NULL;
	const char *end = text + strlen(text), *p = whitespace(text, end);
	int array = p < end && *p == '[';
	if ((framing == 1 && !array) || (framing == 2 && array)) {
		fail(error, 2,
		     framing == 1 ?
			     "JSON ontology must be an array of records" :
			     "JSONL requires one object per line");
		goto done;
	}
	if (array)
		p = whitespace(p + 1, end);
	int first = 1;
	while (p < end && !error->code && !(array && *p == ']')) {
		const char *start = p, *parsed = p;
		const char *limit = array ? end :
					    memchr(p, '\n', (size_t)(end - p));
		if (!limit)
			limit = end;
		/* Auto snapshots can span lines; graph records cannot. */
		if (!framing && !array && first)
			limit = end;
		J *item = json_parse_record(p, (size_t)(limit - p), &parsed,
					    error);
		if (!item) {
			p = parsed ? parsed : start;
			break;
		}
		if (!array && !framing && first && !GET(item, "id") &&
		    !GET(item, "subject") &&
		    cJSON_IsObject(GET(item, "synonyms"))) {
			p = whitespace(parsed, end);
			if (p != end)
				fail(error, 2,
				     "Trailing data after JSON snapshot");
			else
				*snapshot = item;
			if (error->code)
				DEL(item);
			goto done;
		}
		if (!array && memchr(start, '\n', (size_t)(parsed - start)))
			fail(error, 2, "JSONL requires one object per line");
		if (!array && first &&
		    memchr(text, '\n', (size_t)(start - text)))
			fail(error, 2, "JSONL does not permit blank lines");
		if (!error->code)
			record(&source, item);
		DEL(item);
		first = 0;
		if (error->code) {
			p = start;
			break;
		}
		p = whitespace(parsed, end);
		if (array) {
			if (p == end || (*p != ',' && *p != ']')) {
				fail(error, 2,
				     "Expected comma or closing bracket after source record");
				break;
			}
			if (*p == ',') {
				p = whitespace(p + 1, end);
				if (p == end || *p == ']')
					fail(error, 2,
					     "Missing source record after comma");
			}
		} else {
			const char *line =
				memchr(parsed, '\n', (size_t)(p - parsed));
			if (p < end && !line)
				fail(error, 2,
				     "JSONL requires a newline between records");
			if (line &&
			    (memchr(line + 1, '\n', (size_t)(p - line - 1)) ||
			     (p == end && line + 1 != end)))
				fail(error, 2,
				     "JSONL does not permit blank lines");
		}
	}
	if (!error->code && array) {
		if (p == end || *p != ']')
			fail(error, 2, "Unterminated JSON ontology array");
		else if ((p = whitespace(p + 1, end)) != end)
			fail(error, 2, "Trailing data after JSON ontology");
	}
	if (!error->code && !array && first)
		fail(error, 2, "Empty JSONL ontology");
done:
	if (error->code)
		location(error, text, p);
	Graph *graph = NULL;
	if (!error->code && !*snapshot) {
		graph = source.graph;
		source.graph = NULL;
	}
	source_clear(&source);
	return graph;
}
