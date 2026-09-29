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

static int cmp_len(const void *a, const void *b)
{
	size_t x = ulen(S(*(J *const *)a)), y = ulen(S(*(J *const *)b));
	return x < y ? -1 : x > y ? 1 : cmp_lex(a, b);
}

void sort_strings(J *a, int mode)
{
	int n = SIZE(a);
	if (n < 2)
		return;
	J **vs = malloc((size_t)n * sizeof(*vs));
	if (!vs)
		return;
	for (int i = 0; i < n; i++)
		vs[i] = cJSON_DetachItemFromArray(a, 0);
	qsort(vs, (size_t)n, sizeof(*vs), mode == 0 ? cmp_lex : cmp_len);
	for (int i = 0; i < n; i++)
		ADD(a, vs[mode < 0 ? n - 1 - i : i]);
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

static uint64_t hash(const char *s)
{
	uint64_t h = 14695981039346656037ull;
	while (*s) {
		h ^= (unsigned char)*s++;
		h *= 1099511628211ull;
	}
	return h;
}

void *map_get(Map *m, const char *k)
{
	if (!m->cap)
		return NULL;
	size_t i = (size_t)hash(k) & (m->cap - 1);
	while (m->slots[i].key) {
		if (!strcmp(m->slots[i].key, k))
			return m->slots[i].value;
		i = (i + 1) & (m->cap - 1);
	}
	return NULL;
}

void map_put(Map *m, const char *k, void *v)
{
	if (!m->cap || m->size * 2 >= m->cap) {
		Map old = *m;
		m->cap = m->cap ? m->cap * 2 : 64;
		m->slots = calloc(m->cap, sizeof(Slot));
		m->size = 0;
		for (size_t i = 0; i < old.cap; i++)
			if (old.slots[i].key) {
				map_put(m, old.slots[i].key,
					old.slots[i].value);
				free(old.slots[i].key);
			}
		free(old.slots);
	}
	size_t i = (size_t)hash(k) & (m->cap - 1);
	while (m->slots[i].key && strcmp(m->slots[i].key, k))
		i = (i + 1) & (m->cap - 1);
	if (!m->slots[i].key) {
		m->slots[i].key = copy(k);
		m->size++;
	}
	m->slots[i].value = v;
}

void map_free(Map *m)
{
	for (size_t i = 0; i < m->cap; i++)
		free(m->slots[i].key);
	free(m->slots);
	memset(m, 0, sizeof(*m));
}
