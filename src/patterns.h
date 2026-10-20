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
	int n, cap, count, reverse;
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
	/* Match presence is unchanged when both patterns and text are reversed.
	 * Prefer the direction sharing more bytes between neighboring patterns;
	 * common endings otherwise duplicate long paths under every unique prefix. */
	size_t shared_front = 0, shared_back = 0, previous_length = 0;
	int id = 0;
	EACH(v, patterns) {
		const char *text = S(v);
		size_t length = strlen(text);
		p->text[id] = text;
		if (id) {
			const char *previous = p->text[id - 1];
			size_t limit = length < previous_length ?
						     length :
						     previous_length;
			for (size_t i = 0; i < limit && text[i] == previous[i];
			     i++)
				shared_front++;
			for (size_t i = 0;
			     i < limit &&
			     text[length - i - 1] ==
				     previous[previous_length - i - 1];
			     i++)
				shared_back++;
		}
		previous_length = length;
		id++;
	}
	p->reverse = shared_back > shared_front;
	pattern_node(p, 0);
	for (id = 0; id < p->count; id++) {
		const unsigned char *text = (const unsigned char *)p->text[id];
		size_t length = strlen(p->text[id]);
		int node = 0;
		for (size_t i = 0; i < length; i++) {
			unsigned char byte =
				text[p->reverse ? length - i - 1 : i];
			int e = pattern_edge(p, node, byte);
			if (!e) {
				e = pattern_node(p, byte);
				p->nodes[e].sibling = p->nodes[node].edge;
				p->nodes[node].edge = e;
			}
			node = e;
		}
		p->nodes[node].pattern = id;
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
	size_t length = strlen(text);
	for (size_t i = 0; i < length; i++) {
		unsigned char byte =
			(unsigned char)text[p->reverse ? length - i - 1 : i];
		int next = pattern_edge(p, node, byte);
		while (node && !next) {
			node = p->nodes[node].fail;
			next = pattern_edge(p, node, byte);
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
