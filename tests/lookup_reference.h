/* Original lookup ordering for craigtrim/axiom#129, independent of production. */
static int reference_lex(const void *a, const void *b)
{
	return strcmp(S(*(J *const *)a), S(*(J *const *)b));
}
static int reference_length(const void *a, const void *b)
{
	size_t x = ulen(S(*(J *const *)a)), y = ulen(S(*(J *const *)b));
	return x < y ? -1 : x > y ? 1 : reference_lex(a, b);
}
static void reference_sort(J *array, int mode)
{
	int n = SIZE(array);
	J **values = malloc((size_t)(n ? n : 1) * sizeof(*values));
	for (int i = 0; i < n; i++)
		values[i] = cJSON_DetachItemFromArray(array, 0);
	qsort(values, (size_t)n, sizeof(*values),
	      mode ? reference_length : reference_lex);
	for (int i = 0; i < n; i++)
		ADD(array, values[mode < 0 ? n - 1 - i : i]);
	free(values);
}
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
/* Original v0.5.2 algorithm for craigtrim/axiom#129 differential tests. */
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

J *reference_lookup(J *f)
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
		reference_sort(k, -1);
	return d;
}
