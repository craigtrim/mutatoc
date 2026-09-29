/*
 * sparql.c - SPARQL result conversion.
 *
 * Converts worker replies to the requested list and dictionary forms.
 */

#include "mc.h"

static J *flatten_result(mc_engine *e, const J *value, int lc, mc_error *err)
{
	const char *kind = S(GET(value, "kind"));
	if (!*kind) {
		fail(err, 4, "Unbound SPARQL value cannot be transformed");
		return NULL;
	}
	Term term = { (char *)S(GET(value, "value")), NULL, NULL,
		      !strcmp(kind, "iri")   ? 0 :
		      !strcmp(kind, "blank") ? 2 :
					       1 };
	return rdf_flatten(e->graph, &term, lc, err);
}

J *sparql_transform(mc_engine *e, const J *q, J *reply, mc_error *err)
{
	const J *args = GET(q, "args"), *kw = GET(q, "kwargs");
	const J *type = GET(q, "result_type");
	if (!type)
		type = GET(kw, "result_type");
	if (!type)
		type = AT(args, 1);
	int transform = type ? type->valueint : 0;
	if (cJSON_IsString(type)) {
		const char *names[] = { "DO_NOT_TRANSFORM", "LIST_OF_STRINGS",
					"DICT_OF_STR2STR", "DICT_OF_STR2LIST",
					"DICT_OF_STR2DICT" };
		const int values[] = { 0, 10, 20, 21, 22 };
		transform = -1;
		for (size_t i = 0; i < 5; i++)
			if (!strcmp(S(type), names[i]))
				transform = values[i];
	}
	const J *lowercase = GET(q, "to_lowercase"),
		*reverse = GET(q, "reverse");
	if (!lowercase)
		lowercase = GET(kw, "to_lowercase");
	if (!reverse)
		reverse = GET(kw, "reverse");
	if (!lowercase)
		lowercase = AT(args, 2);
	if (!reverse)
		reverse = AT(args, 3);
	int lc = !cJSON_IsFalse(lowercase), rev = cJSON_IsTrue(reverse);
	if (!reply)
		return NULL;
	if (!transform) {
		if (cJSON_IsTrue(GET(reply, "empty")))
			return NIL();
		if (rev) {
			fail(err, 4, "Reverse requires DICT_OF_STR2LIST");
			return NULL;
		}
		return DUP(GET(reply, "raw"));
	}
	if (transform != 10 && transform != 20 && transform != 21) {
		fail(err, 4, "Unsupported SPARQL result transformation");
		return NULL;
	}
	if (!strcmp(S(GET(reply, "type")), "ASK")) {
		fail(err, 4, "ASK cannot use a row transformation");
		return NULL;
	}
	J *out = transform == 10 ? ARR() : OBJ();
	int rows = 0;
	EACH(row, GET(reply, "rows")) {
		if (SIZE(row) < (transform == 10 ? 1 : 2))
			continue;
		rows++;
		J *keys = flatten_result(e, AT(row, 0), lc, err);
		if (transform == 10) {
			EACH(k, keys)
				ADD(out, DUP(k));
		} else {
			J *values = flatten_result(e, AT(row, 1), lc, err);
			if (transform == 20 && SIZE(values) &&
			    !strcmp(S(GET(AT(row, 1), "kind")), "blank"))
				fail(err, 4,
				     "Reference DICT_OF_STR2STR cannot transform a blank-node value");
			EACH(k, keys) {
				if (transform == 20) {
					if (SIZE(values))
						set(out, S(k),
						    DUP(AT(values, 0)));
				} else {
					EACH(v, values)
						ADD(ensure(out, S(k), 1),
						    DUP(v));
				}
			}
			DEL(values);
		}
		DEL(keys);
		if (err->code) {
			DEL(out);
			return NULL;
		}
	}
	if (!rows || !SIZE(out)) {
		DEL(out);
		return NIL();
	}
	J *filtered = transform == 10 ? ARR() : OBJ();
	EACH(k, out) {
		const char *key = transform == 10 ? S(k) : k->string;
		if (!*key || !strcmp(key, "nil"))
			continue;
		if (transform == 10)
			ADD(filtered, DUP(k));
		else if (transform == 20) {
			if (*S(k) && strcmp(S(k), "nil"))
				PUT(filtered, key, DUP(k));
		} else {
			J *values = ARR();
			EACH(v, k)
				if (*S(v) && strcmp(S(v), "nil"))
					ADD(values, DUP(v));
			PUT(filtered, key, values);
		}
	}
	DEL(out);
	out = filtered;
	if (rev) {
		if (transform != 21) {
			DEL(out);
			fail(err, 4, "Reverse requires DICT_OF_STR2LIST");
			return NULL;
		}
		J *reversed = OBJ();
		EACH(k, out)
			EACH(v, k)
				ADD(ensure(reversed, S(v), 1), STR(k->string));
		DEL(out);
		out = reversed;
	}
	return out;
}
