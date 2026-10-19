/* Build-time byte substring matcher for craigtrim/axiom#129.
 * Sparse Aho-Corasick edges bound memory by pattern bytes. Output links avoid
 * copying suffix outputs. Hits are unique and returned in pattern input order.
 */
#ifndef MC_PATTERNS_H
#define MC_PATTERNS_H

typedef struct {
	int edge, sibling, fail, output, pattern;
	unsigned char byte;
} PatternNode;
typedef struct {
	PatternNode *nodes;
	int n, cap, count;
	const char **text;
	int *hits;
	unsigned *seen, stamp;
} Patterns;

static int pattern_node(Patterns *p, unsigned char byte)
{
	if (p->n == p->cap) {
		p->cap = p->cap ? p->cap * 2 : 64;
		p->nodes =
			realloc(p->nodes, (size_t)p->cap * sizeof(*p->nodes));
	}
	int n = p->n++;
	p->nodes[n] = (PatternNode){ 0, 0, 0, 0, -1, byte };
	return n;
}

static int pattern_edge(Patterns *p, int node, unsigned char byte)
{
	for (int e = p->nodes[node].edge; e; e = p->nodes[e].sibling)
		if (p->nodes[e].byte == byte)
			return e;
	return 0;
}

static Patterns *patterns_build(J *patterns)
{
	Patterns *p = calloc(1, sizeof(*p));
	p->count = SIZE(patterns);
	p->text = calloc((size_t)p->count + 1, sizeof(*p->text));
	p->hits = calloc((size_t)p->count + 1, sizeof(*p->hits));
	p->seen = calloc((size_t)p->count + 1, sizeof(*p->seen));
	pattern_node(p, 0);
	int id = 0;
	EACH(v, patterns) {
		p->text[id] = S(v);
		int node = 0;
		for (const unsigned char *s = (const unsigned char *)S(v); *s;
		     s++) {
			int e = pattern_edge(p, node, *s);
			if (!e) {
				e = pattern_node(p, *s);
				p->nodes[e].sibling = p->nodes[node].edge;
				p->nodes[node].edge = e;
			}
			node = e;
		}
		p->nodes[node].pattern = id++;
	}
	int *queue = malloc((size_t)p->n * sizeof(*queue));
	int head = 0, tail = 0;
	for (int e = p->nodes[0].edge; e; e = p->nodes[e].sibling)
		queue[tail++] = e;
	while (head < tail) {
		int node = queue[head++];
		for (int e = p->nodes[node].edge; e; e = p->nodes[e].sibling) {
			int f = p->nodes[node].fail;
			int next = pattern_edge(p, f, p->nodes[e].byte);
			while (f && !next) {
				f = p->nodes[f].fail;
				next = pattern_edge(p, f, p->nodes[e].byte);
			}
			p->nodes[e].fail = next;
			p->nodes[e].output = p->nodes[next].pattern >= 0 ?
							   next :
							   p->nodes[next].output;
			queue[tail++] = e;
		}
	}
	free(queue);
	return p;
}

static int pattern_order(const void *a, const void *b)
{
	int x = *(const int *)a, y = *(const int *)b;
	return (x > y) - (x < y);
}

static int patterns_find(Patterns *p, const char *text)
{
	if (!++p->stamp) {
		memset(p->seen, 0, ((size_t)p->count + 1) * sizeof(*p->seen));
		p->stamp = 1;
	}
	int node = 0, n = 0;
	for (const unsigned char *s = (const unsigned char *)text; *s; s++) {
		int next = pattern_edge(p, node, *s);
		while (node && !next) {
			node = p->nodes[node].fail;
			next = pattern_edge(p, node, *s);
		}
		node = next;
		int output = node;
		while (output) {
			int id = p->nodes[output].pattern;
			if (id >= 0 && p->seen[id] != p->stamp) {
				p->seen[id] = p->stamp;
				p->hits[n++] = id;
			}
			output = p->nodes[output].output;
		}
	}
	qsort(p->hits, (size_t)n, sizeof(*p->hits), pattern_order);
	return n;
}

static void patterns_free(Patterns *p)
{
	free(p->nodes);
	free((void *)p->text);
	free(p->hits);
	free(p->seen);
	free(p);
}
#endif
