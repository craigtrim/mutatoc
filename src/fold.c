/*
 * fold.c - Accent and invisible-character folding for match keys.
 *
 * Every key that matching compares drops combining marks and default
 * ignorable code points after canonical decomposition, so cafe matches café,
 * a decomposed accent matches a precomposed one, and a soft hyphen or zero
 * width space never blocks a match. Case is left to lower(). The Unicode data
 * comes from the vendored utf8proc (vendor/utf8proc). craigtrim/mutatoc#25
 */

#include "mc.h"
#include "utf8proc.h"

/*
 * Unicode reserves these unassigned code points as default ignorable, so a
 * character assigned there later folds away before this data knows it.
 * utf8proc flags only assigned ones (DerivedCoreProperties.txt, 18.0.0).
 */
static int reserved_ignorable(uint32_t c)
{
	return utf8proc_category((utf8proc_int32_t)c) == UTF8PROC_CATEGORY_CN &&
	       (c == 0x2065 || (c >= 0xfff0 && c <= 0xfff8) ||
		(c >= 0xe0000 && c <= 0xe0fff));
}

/* Combining marks and default ignorable code points; ASCII has neither. */
int ujoiner(uint32_t c)
{
	if (c < 0x80 || c >= 0x110000)
		return 0;
	const utf8proc_property_t *p =
		utf8proc_get_property((utf8proc_int32_t)c);
	return p->ignorable || p->category == UTF8PROC_CATEGORY_MN ||
	       p->category == UTF8PROC_CATEGORY_MC ||
	       p->category == UTF8PROC_CATEGORY_ME || reserved_ignorable(c);
}

#define FOLD (UTF8PROC_DECOMPOSE | UTF8PROC_STRIPMARK | UTF8PROC_IGNORE)

/* Hangul syllables decompose only into letters, so they stay whole. */
static int kept(uint32_t c)
{
	return c < 0x80 || (c >= 0xac00 && c <= 0xd7a3) || c >= 0x110000;
}

/* Whether ufold() changes c. */
int ufolds(uint32_t c)
{
	if (kept(c))
		return 0;
	if (reserved_ignorable(c))
		return 1;
	utf8proc_int32_t out[32], boundclass = 0;
	utf8proc_ssize_t n = utf8proc_decompose_char((utf8proc_int32_t)c, out,
						     32, FOLD, &boundclass);
	return n != 1 || out[0] != (utf8proc_int32_t)c;
}

static int ascii(const char *s)
{
	const unsigned char *p = (const unsigned char *)s;
	while (*p && *p < 128)
		p++;
	return !*p;
}

/* Borrows ASCII text, which folding never changes; caller frees only *owned. */
const char *ufold_view(const char *s, char **owned)
{
	*owned = ascii(s) ? NULL : ufold(s);
	return *owned ? *owned : s;
}

/* Embedded NUL keeps its internal C0 80 form through uread() and uwrite(). */
char *ufold(const char *s)
{
	if (ascii(s))
		return copy(s);
	Buf b = { 0 };
	utf8proc_int32_t small[32], *out = small;
	utf8proc_ssize_t cap = 32;
	const char *p = s;
	while (*p) {
		uint32_t c = uread(&p);
		if (kept(c)) {
			uwrite(&b, c);
			continue;
		}
		if (reserved_ignorable(c))
			continue;
		utf8proc_int32_t boundclass = 0;
		utf8proc_ssize_t n;
		while ((n = utf8proc_decompose_char((utf8proc_int32_t)c, out,
						    cap, FOLD, &boundclass)) >
		       cap) {
			utf8proc_int32_t *grown =
				malloc((size_t)n * sizeof(*grown));
			if (!grown) {
				n = -1;
				break;
			}
			if (out != small)
				free(out);
			out = grown;
			cap = n;
		}
		if (n < 0) {
			uwrite(&b, c);
			continue;
		}
		for (utf8proc_ssize_t i = 0; i < n; i++)
			uwrite(&b, (uint32_t)out[i]);
	}
	if (out != small)
		free(out);
	return buf_take(&b);
}
