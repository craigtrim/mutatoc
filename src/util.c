/*
 * util.c - Shared buffers, strings, maps, and JSON helpers.
 *
 * Provides UTF-8 handling, file access, and error reporting.
 */

#ifdef _WIN32
#include <windows.h>
#endif
#include "mc.h"
#include "unicode_data.h"
#include "uppercase_data.h"
#include "case_data.h"

void buf_add(Buf *b, const char *s, size_t n)
{
	if (b->n + n + 1 > b->cap) {
		size_t c = b->cap ? b->cap : 64;
		while (c < b->n + n + 1)
			c *= 2;
		char *p = realloc(b->p, c);
		if (!p)
			return;
		b->p = p;
		b->cap = c;
	}
	memcpy(b->p + b->n, s, n);
	b->n += n;
	b->p[b->n] = 0;
}

void buf_put(Buf *b, const char *s)
{
	buf_add(b, s, strlen(s));
}

char *buf_take(Buf *b)
{
	char *p = b->p ? b->p : copy("");
	memset(b, 0, sizeof(*b));
	return p;
}

char *slice(const char *s, size_t n)
{
	char *p = malloc(n + 1);
	if (p) {
		memcpy(p, s, n);
		p[n] = 0;
	}
	return p;
}

char *copy(const char *s)
{
	return slice(s ? s : "", s ? strlen(s) : 0);
}

char *replace(const char *s, const char *old, const char *rep)
{
	Buf b = { 0 };
	size_t n = strlen(old);
	if (!n)
		return copy(s);
	const char *p;
	while ((p = strstr(s, old)) != NULL) {
		buf_add(&b, s, (size_t)(p - s));
		buf_put(&b, rep);
		s = p + n;
	}
	buf_put(&b, s);
	return buf_take(&b);
}

uint32_t uread(const char **p)
{
	const unsigned char *s = (const unsigned char *)*p;
	if (s[0] == 0xc0 && s[1] == 0x80) {
		*p += 2;
		return 0;
	}
	uint32_t c = *s++;
	int n = 0;
	if (c >= 0xc2 && c <= 0xdf) {
		c &= 31;
		n = 1;
	} else if (c >= 0xe0 && c <= 0xef) {
		c &= 15;
		n = 2;
	} else if (c >= 0xf0 && c <= 0xf4) {
		c &= 7;
		n = 3;
	} else if (c >= 128) {
		(*p)++;
		return 0xfffd;
	}
	for (int i = 0; i < n; i++) {
		if ((*s & 0xc0) != 0x80) {
			(*p)++;
			return 0xfffd;
		}
		c = (c << 6) | (*s++ & 63);
	}
	*p = (const char *)s;
	return c;
}

void uwrite(Buf *b, uint32_t c)
{
	char s[4];
	size_t n = 0;
	if (!c) {
		buf_put(b, "\xc0\x80");
		return;
	} else if (c < 128)
		s[n++] = (char)c;
	else if (c < 2048) {
		s[n++] = (char)(0xc0 | (c >> 6));
		s[n++] = (char)(0x80 | (c & 63));
	} else if (c < 65536) {
		s[n++] = (char)(0xe0 | (c >> 12));
		s[n++] = (char)(0x80 | ((c >> 6) & 63));
		s[n++] = (char)(0x80 | (c & 63));
	} else {
		s[n++] = (char)(0xf0 | (c >> 18));
		s[n++] = (char)(0x80 | ((c >> 12) & 63));
		s[n++] = (char)(0x80 | ((c >> 6) & 63));
		s[n++] = (char)(0x80 | (c & 63));
	}
	buf_add(b, s, n);
}

size_t ulen(const char *s)
{
	size_t n = 0;
	while (*s) {
		uread(&s);
		n++;
	}
	return n;
}

int uspace(uint32_t c)
{
	return c == 32 || (c >= 9 && c <= 13) || (c >= 28 && c <= 31) ||
	       c == 0x85 || c == 0xa0 || c == 0x1680 ||
	       (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 ||
	       c == 0x202f || c == 0x205f || c == 0x3000;
}

int ualpha(uint32_t c)
{
	size_t l = 0, r = sizeof(alpha_ranges) / sizeof(*alpha_ranges);
	while (l < r) {
		size_t m = (l + r) / 2;
		if (c < alpha_ranges[m][0])
			r = m;
		else if (c > alpha_ranges[m][1])
			l = m + 1;
		else
			return 1;
	}
	return 0;
}

static int in_ranges(uint32_t c, const uint32_t (*ranges)[2], size_t n)
{
	size_t l = 0;
	while (l < n) {
		size_t m = (l + n) / 2;
		if (c < ranges[m][0])
			n = m;
		else if (c > ranges[m][1])
			l = m + 1;
		else
			return 1;
	}
	return 0;
}

static int is_cased(uint32_t c)
{
	return in_ranges(c, cased_ranges,
			 sizeof(cased_ranges) / sizeof(*cased_ranges));
}

static int is_ignorable(uint32_t c)
{
	return in_ranges(c, ignorable_ranges,
			 sizeof(ignorable_ranges) / sizeof(*ignorable_ranges));
}

char *lower(const char *s)
{
	/* ASCII needs no contextual Unicode mappings. Keep mixed and non-ASCII
	 * strings on the existing path, including final-sigma handling. */
	const unsigned char *ascii = (const unsigned char *)s;
	while (*ascii && *ascii < 128)
		ascii++;
	if (!*ascii) {
		char *out = copy(s);
		for (char *p = out; *p; p++)
			if (*p >= 'A' && *p <= 'Z')
				*p += 'a' - 'A';
		return out;
	}
	Buf b = { 0 };
	int preceding_cased = 0;
	while (*s) {
		uint32_t c = uread(&s);
		if (c == 0x3a3 && preceding_cased) {
			const char *after = s;
			uint32_t next = 0;
			while (*after) {
				next = uread(&after);
				if (!is_ignorable(next))
					break;
				next = 0;
			}
			if (!is_cased(next)) {
				uwrite(&b, 0x3c2);
				preceding_cased = 1;
				continue;
			}
		}
		if (!is_ignorable(c))
			preceding_cased = is_cased(c);
		size_t l = 0, r = sizeof(lower_map) / sizeof(*lower_map);
		while (l < r) {
			size_t m = (l + r) / 2;
			if (c < lower_map[m][0])
				r = m;
			else if (c > lower_map[m][0])
				l = m + 1;
			else {
				c = lower_map[m][1];
				if (lower_map[m][2]) {
					uwrite(&b, c);
					c = lower_map[m][2];
				}
				break;
			}
		}
		uwrite(&b, c);
	}
	return buf_take(&b);
}

char *upper(const char *s)
{
	Buf b = { 0 };
	while (*s) {
		uint32_t c = uread(&s);
		size_t lo = 0, hi = sizeof(upper_map) / sizeof(*upper_map);
		int found = 0;
		while (lo < hi) {
			size_t m = (lo + hi) / 2;
			if (c < upper_map[m][0])
				hi = m;
			else if (c > upper_map[m][0])
				lo = m + 1;
			else {
				for (int j = 1; j < 4 && upper_map[m][j]; j++)
					uwrite(&b, upper_map[m][j]);
				found = 1;
				break;
			}
		}
		if (!found)
			uwrite(&b, c);
	}
	return buf_take(&b);
}

char *norm(const char *s, int lc, int spaces)
{
	char *v = lc ? lower(s) : copy(s);
	const char *p = v, *begin = NULL, *end = NULL;
	while (*p) {
		const char *start = p;
		uint32_t c = uread(&p);
		if (!uspace(c)) {
			if (!begin)
				begin = start;
			end = p;
		}
	}
	if (!begin) {
		free(v);
		return copy("");
	}
	if (!spaces) {
		char *r = slice(begin, (size_t)(end - begin));
		free(v);
		return r;
	}
	Buf b = { 0 };
	int pending = 0;
	p = begin;
	while (p < end) {
		const char *start = p;
		uint32_t c = uread(&p);
		if (uspace(c))
			pending = 1;
		else {
			if (pending)
				buf_put(&b, " ");
			pending = 0;
			buf_add(&b, start, (size_t)(p - start));
		}
	}
	free(v);
	return buf_take(&b);
}

void fail(mc_error *e, int code, const char *fmt, ...)
{
	if (!e || e->code)
		return;
	e->code = code;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(e->message, sizeof(e->message), fmt, ap);
	va_end(ap);
}

FILE *mc_fopen(const char *path, const char *mode)
{
#ifdef _WIN32
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
				    NULL, 0);
	if (!n)
		return NULL;
	wchar_t *w = malloc((size_t)n * sizeof(wchar_t));
	if (!w)
		return NULL;
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, w, n);
	wchar_t wm[8] = { 0 };
	for (size_t i = 0; mode[i] && i < 7; i++)
		wm[i] = (wchar_t)mode[i];
	FILE *f = _wfopen(w, wm);
	free(w);
	return f;
#else
	return fopen(path, mode);
#endif
}

static int valid_utf8(const unsigned char *p, size_t n)
{
	for (size_t i = 0; i < n;) {
		uint32_t c = p[i++];
		size_t extra = 0;
		uint32_t minimum = 0;
		if (c < 0x80)
			continue;
		if (c >= 0xc2 && c <= 0xdf) {
			extra = 1;
			minimum = 0x80;
			c &= 31;
		} else if (c >= 0xe0 && c <= 0xef) {
			extra = 2;
			minimum = 0x800;
			c &= 15;
		} else if (c >= 0xf0 && c <= 0xf4) {
			extra = 3;
			minimum = 0x10000;
			c &= 7;
		} else
			return 0;
		if (extra > n - i)
			return 0;
		while (extra--) {
			if ((p[i] & 0xc0) != 0x80)
				return 0;
			c = (c << 6) | (p[i++] & 63);
		}
		if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
			return 0;
	}
	return 1;
}

char *read_file(const char *path, mc_error *e)
{
	FILE *f = mc_fopen(path, "rb");
	if (!f) {
		fail(e, 1, "Cannot open file: %s", path);
		return NULL;
	}
	Buf b = { 0 };
	char block[16384];
	size_t n;
	while ((n = fread(block, 1, sizeof(block), f)) != 0) {
		if (b.n + n > 256u * 1024u * 1024u) {
			fail(e, 1, "File exceeds 256 MiB input limit");
			break;
		}
		buf_add(&b, block, n);
	}
	if (ferror(f))
		fail(e, 1, "Cannot read file: %s", path);
	fclose(f);
	if (!valid_utf8((const unsigned char *)b.p, b.n))
		fail(e, 1, "File is not valid UTF-8");
	if (e && e->code) {
		free(b.p);
		return NULL;
	}
	if (b.n && memchr(b.p, 0, b.n)) {
		Buf encoded = { 0 };
		for (size_t i = 0; i < b.n; i++) {
			if (!b.p[i])
				uwrite(&encoded, 0);
			else
				buf_add(&encoded, b.p + i, 1);
		}
		free(b.p);
		return buf_take(&encoded);
	}
	return buf_take(&b);
}

J *json_parse(const char *s, mc_error *e)
{
	if (!valid_utf8((const unsigned char *)s, strlen(s))) {
		fail(e, 2, "JSON is not valid UTF-8");
		return NULL;
	}
	const char *end = NULL;
	J *j = cJSON_ParseWithOpts(s, &end, 1);
	if (!j)
		fail(e, 2, "Invalid JSON at byte %zu",
		     end ? (size_t)(end - s) : 0);
	return j;
}

/* Parse one value without copying or building a tree for the source document. */
J *json_parse_record(const char *s, size_t length, const char **end,
		     mc_error *e)
{
	J *value = cJSON_ParseWithLengthOpts(s, length, end, 0);
	if (!value)
		fail(e, 2, "Invalid JSON record at byte %zu",
		     *end ? (size_t)(*end - s) : 0);
	else if (!valid_utf8((const unsigned char *)s, (size_t)(*end - s))) {
		fail(e, 2, "JSON record is not valid UTF-8");
		DEL(value);
		value = NULL;
	}
	/* cJSON's whitespace scanner also accepts non-JSON control bytes. */
	int quoted = 0, escaped = 0;
	for (const char *p = s; value && p < *end; p++) {
		unsigned char c = (unsigned char)*p;
		if (c < 32 &&
		    (quoted || (c != '\t' && c != '\r' && c != '\n'))) {
			fail(e, 2, "Invalid control character in JSON record");
			*end = p;
			DEL(value);
			value = NULL;
		} else if (escaped)
			escaped = 0;
		else if (quoted && c == '\\')
			escaped = 1;
		else if (c == '"')
			quoted = !quoted;
	}
	return value;
}

void set(J *j, const char *k, J *v)
{
	if (!v)
		v = NIL();
	if (GET(j, k))
		cJSON_ReplaceItemInObjectCaseSensitive(j, k, v);
	else
		PUT(j, k, v);
}

J *ensure(J *j, const char *k, int array)
{
	J *v = GET(j, k);
	if (!v) {
		v = array ? ARR() : OBJ();
		PUT(j, k, v);
	}
	return v;
}

int contains(const J *a, const char *s)
{
	EACH(v, a) {
		if (!strcmp(S(v), s))
			return 1;
	}
	return 0;
}

void unique(J *a, const char *s)
{
	if (!contains(a, s))
		ADD(a, STR(s));
}

/*
 * unique() for arrays that only this set's owner appends to: same order and
 * contents, with a hash check instead of a scan of the whole array.
 */
void unique_indexed(J *a, Map *seen, const char *s)
{
	if (map_get(seen, s))
		return;
	map_put(seen, s, (void *)1);
	ADD(a, STR(s));
}

static int cmp_lex(const void *a, const void *b)
{
	return strcmp(S(*(J *const *)a), S(*(J *const *)b));
}

typedef struct {
	J *value;
	size_t length;
} SortString;
static int cmp_len(const void *a, const void *b)
{
	const SortString *x = a, *y = b;
	return x->length < y->length ? -1 :
	       x->length > y->length ? 1 :
					     strcmp(S(x->value), S(y->value));
}

void sort_strings(J *a, int mode)
{
	int n = SIZE(a);
	if (n < 2)
		return;
	if (mode) {
		/* Count UTF-8 characters once, not on every comparison. */
		SortString *vs = malloc((size_t)n * sizeof(*vs));
		if (!vs)
			return;
		for (int i = 0; i < n; i++) {
			vs[i].value = cJSON_DetachItemFromArray(a, 0);
			vs[i].length = ulen(S(vs[i].value));
		}
		/* Keep qsort's tie handling; skip it only for strict input order. */
		int order = cmp_len(&vs[0], &vs[1]), ordered = order != 0;
		for (int i = 2; i < n && ordered; i++) {
			int next = cmp_len(&vs[i - 1], &vs[i]);
			ordered = next != 0 && (next > 0) == (order > 0);
		}
		if (!ordered) {
			qsort(vs, (size_t)n, sizeof(*vs), cmp_len);
			order = -1;
		}
		int reverse = (mode < 0) != (order > 0);
		for (int i = 0; i < n; i++)
			ADD(a, vs[reverse ? n - 1 - i : i].value);
		free(vs);
		return;
	}
	J **vs = malloc((size_t)n * sizeof(*vs));
	if (!vs)
		return;
	for (int i = 0; i < n; i++)
		vs[i] = cJSON_DetachItemFromArray(a, 0);
	int order = cmp_lex(&vs[0], &vs[1]), ordered = order != 0;
	for (int i = 2; i < n && ordered; i++) {
		int next = cmp_lex(&vs[i - 1], &vs[i]);
		ordered = next != 0 && (next > 0) == (order > 0);
	}
	if (!ordered) {
		qsort(vs, (size_t)n, sizeof(*vs), cmp_lex);
		order = -1;
	}
	for (int i = 0; i < n; i++)
		ADD(a, vs[order > 0 ? n - 1 - i : i]);
	free(vs);
}

char *join(const J *a, const char *sep)
{
	Buf b = { 0 };
	EACH(v, a) {
		if (v != a->child)
			buf_put(&b, sep);
		buf_put(&b, S(v));
	}
	return buf_take(&b);
}

J *split(const char *s, const char *sep)
{
	J *a = ARR();
	size_t n = strlen(sep);
	const char *p;
	while (n && (p = strstr(s, sep)) != NULL) {
		char *v = slice(s, (size_t)(p - s));
		ADD(a, STR(v));
		free(v);
		s = p + n;
	}
	ADD(a, STR(s));
	return a;
}

const char *local(const char *s)
{
	const char *p = strrchr(s, '#');
	return p ? p + 1 : s;
}

/* MurmurHash64A (Austin Appleby, public domain), with aligned-safe reads.
 * https://github.com/aappleby/smhasher/blob/master/src/MurmurHash2.cpp
 * Hash long vocabulary forms eight bytes at a time. Hashes stay internal;
 * native byte order does not affect lookup equality or JSON ordering. */
static uint64_t hash(const char *s)
{
	const uint64_t mix = UINT64_C(0xc6a4a7935bd1e995);
	size_t length = strlen(s);
	uint64_t h = UINT64_C(129) ^ ((uint64_t)length * mix);
	const unsigned char *p = (const unsigned char *)s;
	while (length >= 8) {
		uint64_t word;
		memcpy(&word, p, sizeof(word));
		word *= mix;
		word ^= word >> 47;
		word *= mix;
		h = (h ^ word) * mix;
		p += 8;
		length -= 8;
	}
	if (length) {
		uint64_t tail = 0;
		for (size_t i = 0; i < length; i++)
			tail |= (uint64_t)p[i] << (i * 8);
		h = (h ^ tail) * mix;
	}
	h ^= h >> 47;
	h *= mix;
	return h ^ (h >> 47);
}

/* Compact metadata stores an eight-bit fingerprint and key ownership.
 * Borrowed keys remain valid until their entry is removed or replaced. */
enum { MAP_FINGERPRINT = 255, MAP_OWNED = 256 };
static uint16_t *map_tags(Map *m)
{
	return (uint16_t *)(m->slots + m->cap);
}
static unsigned char map_tag(uint64_t key_hash)
{
	return (unsigned char)((key_hash >> 57) + 1);
}
void *map_get(Map *m, const char *k)
{
	if (!m->cap)
		return NULL;
	uint64_t key_hash = hash(k);
	uint16_t tag = map_tag(key_hash), *tags = map_tags(m);
	size_t i = (size_t)key_hash & (m->cap - 1);
	while (tags[i]) {
		if ((tags[i] & MAP_FINGERPRINT) == tag &&
		    !strcmp(m->slots[i].key, k))
			return m->slots[i].value;
		i = (i + 1) & (m->cap - 1);
	}
	return NULL;
}

static void map_resize(Map *m, size_t capacity)
{
	Map old = *m;
	m->cap = capacity;
	m->slots = calloc(m->cap, sizeof(*m->slots) + sizeof(uint16_t));
	uint16_t *tags = map_tags(m);
	/* Transfer keys and their ownership in the same probe order. */
	for (size_t i = 0; i < old.cap; i++)
		if (old.slots[i].key) {
			uint64_t key_hash = hash(old.slots[i].key);
			size_t at = (size_t)key_hash & (m->cap - 1);
			while (tags[at])
				at = (at + 1) & (m->cap - 1);
			m->slots[at] = old.slots[i];
			tags[at] = map_tags(&old)[i];
		}
	free(old.slots);
}
void map_reserve(Map *m, size_t entries)
{
	if (!entries)
		return;
	size_t capacity = m->cap ? m->cap : 8;
	while (capacity / 2 < entries)
		capacity *= 2;
	if (capacity > m->cap)
		map_resize(m, capacity);
}
static void map_store(Map *m, const char *k, void *v, int borrowed)
{
	uint64_t key_hash = hash(k);
	unsigned char tag = map_tag(key_hash);
	size_t i = 0;
	if (m->cap) {
		uint16_t *tags = map_tags(m);
		i = (size_t)key_hash & (m->cap - 1);
		while (tags[i]) {
			if ((tags[i] & MAP_FINGERPRINT) == tag &&
			    !strcmp(m->slots[i].key, k)) {
				/* Switch ownership before the previous owner can free its key. */
				if (borrowed) {
					if (m->slots[i].key != k &&
					    (tags[i] & MAP_OWNED))
						free(m->slots[i].key);
					m->slots[i].key = (char *)k;
					tags[i] = tag;
				} else if (!borrowed &&
					   !(tags[i] & MAP_OWNED)) {
					m->slots[i].key = copy(k);
					tags[i] = tag | MAP_OWNED;
				}
				m->slots[i].value = v;
				return;
			}
			i = (i + 1) & (m->cap - 1);
		}
	}
	if (!m->cap || m->size * 2 >= m->cap) {
		map_resize(m, m->cap ? m->cap * 2 : 64);
		i = (size_t)key_hash & (m->cap - 1);
		while (map_tags(m)[i])
			i = (i + 1) & (m->cap - 1);
	}
	m->slots[i] = (Slot){ borrowed ? (char *)k : copy(k), v };
	map_tags(m)[i] = tag | (borrowed ? 0 : MAP_OWNED);
	m->size++;
}

void map_put(Map *m, const char *key, void *value)
{
	map_store(m, key, value, 0);
}
void map_put_ref(Map *m, const char *key, void *value)
{
	map_store(m, key, value, 1);
}

/* Per-word dependency and alias buckets usually contain only one entry. */
void map_put_small(Map *m, const char *key, void *value)
{
	if (!m->cap) {
		m->cap = 8;
		m->slots = calloc(m->cap, sizeof(*m->slots) + sizeof(uint16_t));
	}
	map_put(m, key, value);
}

void map_free(Map *m)
{
	for (size_t i = 0; i < m->cap; i++)
		if (map_tags(m)[i] & MAP_OWNED)
			free(m->slots[i].key);
	free(m->slots);
	memset(m, 0, sizeof(*m));
}

/* Hashed object construction, preserving member order (craigtrim/axiom#129). */
J *indexed_ensure(J *object, Map *index, const char *key, int array)
{
	J *value = map_get(index, key);
	if (!value) {
		value = array ? ARR() : OBJ();
		PUT(object, key, value);
		map_put_ref(index, value->string, value);
	}
	return value;
}

void indexed_set(J *object, Map *index, const char *key, J *value)
{
	J *old = map_get(index, key);
	if (old) {
		if (!(value->type & cJSON_StringIsConst))
			free(value->string);
		value->type &= ~cJSON_StringIsConst;
		value->string = copy(key);
		map_put_ref(index, value->string, value);
		cJSON_ReplaceItemViaPointer(object, old, value);
	} else {
		PUT(object, key, value);
		map_put_ref(index, value->string, value);
	}
}

/* Backshift deletion keeps probe chains reachable without tombstone buildup. */
void map_delete(Map *m, const char *key)
{
	if (!m->cap)
		return;
	uint64_t key_hash = hash(key);
	uint16_t tag = map_tag(key_hash), *tags = map_tags(m);
	size_t mask = m->cap - 1, hole = (size_t)key_hash & mask;
	while (tags[hole] && ((tags[hole] & MAP_FINGERPRINT) != tag ||
			      strcmp(m->slots[hole].key, key)))
		hole = (hole + 1) & mask;
	if (!m->slots[hole].key)
		return;
	if (tags[hole] & MAP_OWNED)
		free(m->slots[hole].key);
	m->size--;
	size_t next = (hole + 1) & mask;
	while (m->slots[next].key) {
		size_t start = (size_t)hash(m->slots[next].key) & mask;
		if (((next - start) & mask) >= ((next - hole) & mask)) {
			m->slots[hole] = m->slots[next];
			tags[hole] = tags[next];
			hole = next;
		}
		next = (next + 1) & mask;
	}
	m->slots[hole] = (Slot){ 0 };
	tags[hole] = 0;
}
