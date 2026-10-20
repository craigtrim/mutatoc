/* Counted, compressed byte prefixes for matching (craigtrim/axiom#129).
 * Terminal counts preserve shared prefixes across word and alias edits.
 * Edges own only their distinct suffix, rather than a full key per prefix.
 */
#include "mc.h"

typedef struct {
	char *text;
	size_t length, refs;
	int child, next;
} PrefixNode;
struct PrefixIndex {
	PrefixNode *nodes;
	int count, capacity, available;
};
static int prefix_node(PrefixIndex *index, const char *text, size_t length)
{
	int id;
	if (index->available) {
		id = index->available;
		index->available = index->nodes[id].next;
	} else {
		if (index->count == index->capacity) {
			index->capacity =
				index->capacity ? index->capacity * 2 : 64;
			index->nodes = realloc(index->nodes,
					       (size_t)index->capacity *
						       sizeof(*index->nodes));
		}
		id = index->count++;
	}
	index->nodes[id] = (PrefixNode){ slice(text, length), length, 0, 0, 0 };
	return id;
}
static int prefix_child(const PrefixIndex *index, int parent,
			unsigned char byte)
{
	for (int id = index->nodes[parent].child; id;
	     id = index->nodes[id].next)
		if ((unsigned char)index->nodes[id].text[0] == byte)
			return id;
	return 0;
}
static int prefix_append(PrefixIndex *index, int parent, const char *text,
			 size_t length)
{
	while (length) {
		int id = prefix_child(index, parent, (unsigned char)*text);
		if (!id) {
			id = prefix_node(index, text, length);
			index->nodes[id].next = index->nodes[parent].child;
			index->nodes[parent].child = id;
			return id;
		}
		PrefixNode old = index->nodes[id];
		size_t common = 0;
		while (common < length && common < old.length &&
		       text[common] == old.text[common])
			common++;
		if (common < old.length) {
			int rest = prefix_node(index, old.text + common,
					       old.length - common);
			index->nodes[rest].refs = old.refs;
			index->nodes[rest].child = old.child;
			index->nodes[id].text = slice(old.text, common);
			index->nodes[id].length = common;
			index->nodes[id].refs = 0;
			index->nodes[id].child = rest;
			free(old.text);
		}
		parent = id;
		text += common;
		length -= common;
	}
	return parent;
}
static int prefix_follow(const PrefixIndex *index, int parent, const char *text,
			 size_t length, int *path, size_t *depth)
{
	while (length) {
		int id = prefix_child(index, parent, (unsigned char)*text);
		if (!id || index->nodes[id].length > length ||
		    memcmp(index->nodes[id].text, text,
			   index->nodes[id].length))
			return -1;
		if (path)
			path[++*depth] = id;
		text += index->nodes[id].length;
		length -= index->nodes[id].length;
		parent = id;
	}
	return parent;
}
static void prefix_recycle(PrefixIndex *index, int id)
{
	free(index->nodes[id].text);
	index->nodes[id] = (PrefixNode){ 0 };
	index->nodes[id].next = index->available;
	index->available = id;
}
static void prefix_compact(PrefixIndex *index, const int *path, size_t depth)
{
	while (depth) {
		int id = path[depth], parent = path[depth - 1];
		PrefixNode *node = &index->nodes[id];
		if (!node->refs && !node->child) {
			int *link = &index->nodes[parent].child;
			while (*link != id)
				link = &index->nodes[*link].next;
			*link = node->next;
			prefix_recycle(index, id);
		} else {
			while (!node->refs && node->child &&
			       !index->nodes[node->child].next) {
				int child = node->child;
				PrefixNode next = index->nodes[child];
				Buf joined = { 0 };
				buf_add(&joined, node->text, node->length);
				buf_add(&joined, next.text, next.length);
				free(node->text);
				node->text = buf_take(&joined);
				node->length += next.length;
				node->refs = next.refs;
				node->child = next.child;
				prefix_recycle(index, child);
			}
		}
		depth--;
	}
}
void prefix_index_change(PrefixIndex **handle, const char *text, int delta)
{
	if (!delta || (!*handle && delta < 0))
		return;
	int *path = delta < 0 ? malloc((strlen(text) + 1) * sizeof(*path)) :
				      NULL;
	size_t depth = 0;
	if (path)
		path[0] = 0;
	int node = 0;
	const char *p = text, *previous = text;
	while (*p) {
		const char *at = p;
		uint32_t c = uread(&p);
		if (at == text || !uspace(c))
			continue;
		if (!*handle) {
			*handle = calloc(1, sizeof(**handle));
			prefix_node(*handle, "", 0);
		}
		PrefixIndex *index = *handle;
		if (delta > 0) {
			node = prefix_append(index, node, previous,
					     (size_t)(at - previous));
			index->nodes[node].refs++;
		} else {
			node = prefix_follow(index, node, previous,
					     (size_t)(at - previous), path,
					     &depth);
			if (node < 0)
				break;
			if (index->nodes[node].refs)
				index->nodes[node].refs--;
		}
		previous = at;
	}
	if (path && *handle)
		prefix_compact(*handle, path, depth);
	free(path);
}
int prefix_index_has(const PrefixIndex *index, const char *text)
{
	if (!index)
		return 0;
	int node = prefix_follow(index, 0, text, strlen(text), NULL, NULL);
	return node >= 0 && index->nodes[node].refs != 0;
}
void prefix_index_free(PrefixIndex *index)
{
	if (!index)
		return;
	for (int i = 0; i < index->count; i++)
		free(index->nodes[i].text);
	free(index->nodes);
	free(index);
}
