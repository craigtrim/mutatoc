/*
 * literal.c - RDF literal normalization.
 *
 * Preserves reference formatting for numeric and Boolean values.
 */

#include "mc.h"
#include <math.h>

static char *decimal_text(const char *input, int integer)
{
	char *trimmed = norm(input, 0, 0);
	const char *p = trimmed;
	int negative = *p == '-';
	if (*p == '+' || *p == '-')
		++p;
	Buf digits = { 0 };
	long point = 0;
	while (isdigit((unsigned char)*p)) {
		buf_add(&digits, p++, 1);
		point++;
	}
	if (!integer && *p == '.') {
		p++;
		while (isdigit((unsigned char)*p))
			buf_add(&digits, p++, 1);
	}
	if (!digits.n) {
		free(digits.p);
		free(trimmed);
		return NULL;
	}
	if (!integer && (*p == 'e' || *p == 'E')) {
		char *end;
		long exponent = strtol(++p, &end, 10);
		if (end == p || exponent > 100000 || exponent < -100000) {
			free(digits.p);
			free(trimmed);
			return NULL;
		}
		point += exponent;
		p = end;
	}
	if (*p) {
		free(digits.p);
		free(trimmed);
		return NULL;
	}
	size_t start = 0;
	while (start + 1 < digits.n && digits.p[start] == '0') {
		start++;
		point--;
	}
	const char *value = digits.p + start;
	size_t length = digits.n - start;
	Buf out = { 0 };
	if (negative && (!integer || strcmp(value, "0")))
		buf_put(&out, "-");
	if (point <= 0) {
		buf_put(&out, "0.");
		for (long i = 0; i < -point; i++)
			buf_put(&out, "0");
		buf_put(&out, value);
	} else if ((size_t)point >= length) {
		buf_put(&out, value);
		for (size_t i = length; i < (size_t)point; i++)
			buf_put(&out, "0");
	} else {
		buf_add(&out, value, (size_t)point);
		buf_put(&out, ".");
		buf_put(&out, value + point);
	}
	free(digits.p);
	free(trimmed);
	return buf_take(&out);
}

static char *float_text(const char *input)
{
	char *trimmed = norm(input, 0, 0), *end;
	double value = strtod(trimmed, &end);
	if (end == trimmed || *end) {
		free(trimmed);
		return NULL;
	}
	free(trimmed);
	if (isnan(value))
		return copy("nan");
	if (isinf(value))
		return copy(value < 0 ? "-inf" : "inf");
	if (value == 0)
		return copy(signbit(value) ? "-0.0" : "0.0");
	char candidate[96];
	int precision;
	for (precision = 1; precision <= 17; precision++) {
		snprintf(candidate, sizeof(candidate), "%.*g", precision,
			 value);
		if (strtod(candidate, NULL) == value)
			break;
	}
	char scientific[96];
	snprintf(scientific, sizeof(scientific), "%.*e", precision - 1, value);
	int exponent = atoi(strchr(scientific, 'e') + 1);
	if (exponent >= -4 && exponent < 16) {
		int decimals = precision - 1 - exponent;
		if (decimals < 1)
			decimals = 1;
		snprintf(candidate, sizeof(candidate), "%.*f", decimals, value);
	} else {
		/* Python uses a lowercase exponent and at least two exponent digits. */
		const char *ep = strchr(scientific, 'e');
		size_t n = (size_t)(ep - scientific);
		while (n && scientific[n - 1] == '0')
			n--;
		if (n && scientific[n - 1] == '.')
			n--;
		scientific[n] = 0;
		snprintf(candidate, sizeof(candidate), "%.32se%+03d",
			 scientific, exponent);
	}
	return copy(candidate);
}

void literal_normalize(Term *t)
{
	if (t->kind != 1 || !t->datatype ||
	    strncmp(t->datatype, XSD, strlen(XSD)))
		return;
	const char *type = t->datatype + strlen(XSD);
	char *value = NULL;
	const char *integers[] = { "integer",
				   "nonPositiveInteger",
				   "negativeInteger",
				   "long",
				   "int",
				   "short",
				   "byte",
				   "nonNegativeInteger",
				   "unsignedLong",
				   "unsignedInt",
				   "unsignedShort",
				   "unsignedByte",
				   "positiveInteger" };
	for (size_t i = 0; i < sizeof(integers) / sizeof(*integers); i++)
		if (!strcmp(type, integers[i]))
			value = decimal_text(t->value, 1);
	if (!strcmp(type, "decimal"))
		value = decimal_text(t->value, 0);
	else if (!strcmp(type, "double") || !strcmp(type, "float"))
		value = float_text(t->value);
	else if (!strcmp(type, "boolean")) {
		char *lc = lower(t->value);
		value = copy(!strcmp(lc, "true") || !strcmp(lc, "1") ? "true" :
								       "false");
		free(lc);
	} else if (!strcmp(type, "normalizedString") ||
		   !strcmp(type, "token")) {
		value = copy(t->value);
		for (char *c = value; *c; c++)
			if (*c == '\t' || *c == '\r' || *c == '\n')
				*c = ' ';
		if (!strcmp(type, "token")) {
			char *collapsed = norm(value, 0, 1);
			free(value);
			value = collapsed;
		}
	} else if (!strcmp(type, "hexBinary")) {
		int valid = strlen(t->value) % 2 == 0;
		for (const char *p = t->value; *p; p++)
			if (!isxdigit((unsigned char)*p))
				valid = 0;
		if (valid)
			value = lower(t->value);
	}
	if (value) {
		free(t->value);
		t->value = value;
	}
}
