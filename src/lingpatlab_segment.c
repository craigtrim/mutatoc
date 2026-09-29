/*
 * lingpatlab_segment.c - Sentence segmentation and boundary cleanup.
 *
 * Prepares text for segmentation and normalizes sentence output.
 */

#include "lingpatlab.h"

static char *number_lists(const char *text, int denormalize)
{
	char *out = copy(text);
	for (int i = 1; i <= 10; i++) {
		char a[12], b[12];
		snprintf(a, sizeof a, "%d. ", i);
		snprintf(b, sizeof b, "%d_ ", i);
		out = lp_replace(out, denormalize ? b : a, denormalize ? a : b);
	}
	return out;
}

static char *delimiters(const char *text, const char *delimiter)
{
	size_t count = 0;
	const char *p = text;
	size_t bytes = strlen(delimiter);
	if (!bytes)
		count = ulen(text) + 1;
	else
		while ((p = strstr(p, delimiter)) != NULL) {
			count++;
			p += bytes;
		}
	if (!count)
		return copy(text);
	if (!*text)
		return copy(text);
	return (double)count / (double)ulen(text) > 0.04 ?
		       lp_replace(copy(text), delimiter, ".") :
		       copy(text);
}

static char *bullet_clean(const char *text)
{
	char *s = copy(*text == '-' ? text + 1 : text);
	s = lp_replace(s, "  ", " ");
	while (strstr(s, ".."))
		s = lp_replace(s, "..", ".");
	while (strstr(s, ". -"))
		s = lp_replace(s, ". -", ". ");
	while (strstr(s, ". . "))
		s = lp_replace(s, ". . ", ".");
	return lp_collapse(s);
}

static J *post_sentences(const J *sentences)
{
	static const char *from[] = { "..", ". .", "!.", "! .",
				      "?.", "? .", ":.", ": ." };
	static const char *to[] = { ". ", ". ", "! ", "! ",
				    "? ", "? ", ": ", ": " };
	J *out = ARR();
	EACH(v, sentences) {
		char *s = copy(S(v));
		for (size_t i = 0; i < sizeof(from) / sizeof(*from); i++)
			if (strstr(s, from[i]))
				s = lp_strip(lp_replace(s, from[i], to[i]));
		ADD(out, STR(s));
		free(s);
	}
	return out;
}

static J *doc_sentences(mc_engine *engine, const char *text, mc_error *err)
{
	J *q = OBJ();
	PUT(q, "op", STR("analyze"));
	PUT(q, "text", STR(text));
	J *doc = spacy_call(engine, q, err);
	DEL(q);
	if (!doc)
		return NULL;
	J *sentences = GET(doc, "sentences"), *out = ARR();
	if (!cJSON_IsArray(sentences)) {
		DEL(out);
		DEL(doc);
		fail(err, 5, "spaCy document has no sentence boundaries");
		return NULL;
	}
	EACH(v, sentences) {
		const char *input = S(v);
		if (!*input || !strcmp(input, "None"))
			continue;
		char *trim = norm(input, 0, 0), *s = copy(input);
		if (!lp_ends(trim, ".")) {
			Buf b = { 0 };
			buf_put(&b, s);
			buf_put(&b, ".");
			free(s);
			s = buf_take(&b);
		}
		free(trim);
		trim = norm(s, 0, 0);
		if (!strcmp(trim, ".")) {
			free(s);
			free(trim);
			continue;
		}
		free(s);
		s = trim;
		if (!strcmp(s, "..")) {
			free(s);
			continue;
		}
		s = lp_replace(s, "\n", " ");
		if (!strncmp(s, ".. ", 3)) {
			char *p = copy(s + 3);
			free(s);
			s = p;
		}
		if (lp_ends(s, ".  ..")) {
			char *p = slice(s, strlen(s) - 3);
			free(s);
			s = lp_strip(p);
		}
		ADD(out, STR(s));
		free(s);
	}
	DEL(doc);
	return out;
}

static J *paragraphs(const char *text)
{
	J *parts = split(text, "\n\n"), *out = ARR();
	EACH(v, parts) {
		char *s = norm(S(v), 0, 0);
		if (*s)
			ADD(out, STR(s));
		free(s);
	}
	DEL(parts);
	return out;
}

static J *sentence_segments(mc_engine *e, const char *text, mc_error *err)
{
	char *s = delimiters(text, ",");
	char *p = delimiters(s, ";");
	free(s);
	s = number_lists(p, 0);
	free(p);
	s = lp_replace(s, "\n", " . ");
	s = lp_replace(s, "   ", "  ");
	s = lp_replace(s, "  ", ". ");
	if (strchr(s, '.')) {
		p = bullet_clean(s);
		free(s);
		s = p;
	}
	if (strchr(s, '.'))
		s = lp_replace(s, ", Inc", " Inc");
	J *out = NULL;
	if (!strchr(s, '.')) {
		out = ARR();
		ADD(out, STR(s));
	} else {
		J *raw = doc_sentences(e, s, err);
		if (raw) {
			J *post = post_sentences(raw);
			DEL(raw);
			out = ARR();
			EACH(v, post) {
				p = number_lists(S(v), 1);
				ADD(out, STR(p));
				free(p);
			}
			DEL(post);
		}
	}
	free(s);
	return out;
}

J *lp_segment_method(mc_engine *e, const char *method, const J *q,
		     mc_error *err)
{
	if (!strcmp(method, "post_process_sentences"))
		return post_sentences(GET(q, "sentences"));
	if (!cJSON_IsString(GET(q, "text"))) {
		fail(err, 2, "Segmentation requires string text");
		return NULL;
	}
	const char *text = S(GET(q, "text"));
	if (!strcmp(method, "delimiters_to_periods") && GET(q, "delimiter") &&
	    !strlen(S(GET(q, "delimiter"))) && !*text) {
		fail(err, 2, "Delimiter density is undefined for empty input");
		return NULL;
	}
	char *s = NULL;
	if (!strcmp(method, "bullet_point_cleaner"))
		s = bullet_clean(text);
	else if (!strcmp(method, "delimiters_to_periods"))
		s = delimiters(text, GET(q, "delimiter") ?
					     S(GET(q, "delimiter")) :
					     ",");
	else if (!strcmp(method, "newlines_to_periods"))
		s = replace(text, "\n", " . ");
	else if (!strcmp(method, "numbered_list_normalizer"))
		s = number_lists(text, cJSON_IsTrue(GET(q, "denormalize")));
	if (s) {
		J *out = STR(s);
		free(s);
		return out;
	}
	if (!strcmp(method, "spacy_doc_segmenter"))
		return doc_sentences(e, text, err);
	if (!*text) {
		fail(err, 2, "Empty Input");
		return NULL;
	}
	if (!strcmp(method, "segment_paragraphs"))
		return paragraphs(text);
	if (!strcmp(method, "segment_sentences"))
		return sentence_segments(e, text, err);
	if (!strcmp(method, "segment_input_text")) {
		J *ps = paragraphs(text), *out = ARR();
		EACH(p, ps) {
			J *sentences = sentence_segments(e, S(p), err);
			if (!sentences)
				break;
			ADD(out, sentences);
		}
		DEL(ps);
		if (err->code) {
			DEL(out);
			return NULL;
		}
		return out;
	}
	fail(err, 2, "Unknown segmentation method: %s", method);
	return NULL;
}
