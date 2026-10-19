/* Deterministic large vocabulary for craigtrim/axiom#129. */
#ifndef MC_GENERATED_TEST_H
#define MC_GENERATED_TEST_H
static char *generated_entity(int id, int revision)
{
	char text[768];
	snprintf(
		text, sizeof(text),
		"<http://example.org/#id%d> <http://www.w3.org/1999/02/22-rdf-syntax-ns#type> <http://example.org/#School>; "
		"<http://www.w3.org/2000/01/rdf-schema#label> \"district%05d regional school of applied sciences%d\"; "
		"<http://www.w3.org/2000/01/rdf-schema#seeAlso> \"district%05d regional academy training center\" .\n",
		id, id, revision, id);
	return copy(text);
}
static char *generated_turtle(int n)
{
	Buf b = { 0 };
	for (int i = 0; i < n; i++) {
		char *text = generated_entity(i, 0);
		buf_put(&b, text);
		free(text);
	}
	return buf_take(&b);
}
#endif
