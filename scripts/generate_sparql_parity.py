"""Record the reference arbitrary-query contract without importing port code."""
import json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'.reference/src'))
from mutato.finder.singlequery import AskOwlAPI
from mutato.finder.singlequery.dto import QueryResultType
queries=[
'SELECT ?s ?label WHERE {?s rdfs:label ?label} ORDER BY ?s ?label',
'SELECT DISTINCT ?s WHERE {?s rdfs:subClassOf+ :Root} ORDER BY ?s',
'SELECT ?s ?label WHERE {?s rdfs:label ?label FILTER(regex(str(?label),"dog","i"))} ORDER BY ?s ?label',
'SELECT ?s ?x WHERE {?s rdfs:label ?x OPTIONAL {?s :missing ?unused}} ORDER BY ?s ?x LIMIT 3 OFFSET 1',
'SELECT ?s ?missing WHERE {?s rdfs:label ?label OPTIONAL {?s :missing ?missing}} LIMIT 1',
'SELECT ?s ?x WHERE {{?s rdfs:label ?x} UNION {?s rdfs:seeAlso ?x}} ORDER BY ?s ?x',
'SELECT ?s ?x WHERE {?s rdfs:label ?label BIND(UCASE(str(?label)) AS ?x)} ORDER BY ?s ?x',
'SELECT ?s ?n WHERE {VALUES ?s {:Dog :Cat} ?s rdfs:label ?label} GROUP BY ?s ORDER BY ?s',
'SELECT ?s (COUNT(?label) AS ?n) WHERE {?s rdfs:label ?label} GROUP BY ?s ORDER BY ?s',
'SELECT ?s ?n WHERE {{SELECT ?s (COUNT(?label) AS ?n) WHERE {?s rdfs:label ?label} GROUP BY ?s} FILTER(?n>1)} ORDER BY ?s',
'SELECT ?s WHERE {?s rdfs:label ?label MINUS {?s rdfs:subClassOf :Pet}} ORDER BY ?s',
'SELECT ?s WHERE {?s rdfs:label ?label FILTER EXISTS {?s rdfs:subClassOf ?parent}} ORDER BY ?s',
'SELECT ?s ?o WHERE {?s :empty ?o}',
'SELECT ?s ?o WHERE {VALUES (?s ?o) {("nil" "x") ("x" "nil") ("" "y") ("x" "")}}',
'SELECT ?s ?o WHERE {?s :spans ?o} ORDER BY ?s',
'SELECT ?s ?o WHERE {?s :list ?o} ORDER BY ?s',
'ASK {?s rdfs:label ?label}',
'ASK {?s :missing ?label}',
'CONSTRUCT {?s rdfs:label ?label} WHERE {?s rdfs:label ?label}',
'DESCRIBE :Dog',
'INVALID QUERY',
]
api=AskOwlAPI('proper',str(ROOT/'tests/fixtures/api'),None)
rows=[]
for query in queries:
 for mode in QueryResultType:
  for lc,reverse in [(True,False),(False,False),(True,True)]:
   row={'request':{'op':'sparql','query':query,'result_type':mode.value,'to_lowercase':lc,'reverse':reverse}}
   try:
    expected=api.adhoc(query,mode,to_lowercase=lc,reverse=reverse)
    if mode.value==0 and expected is not None:
     if expected.type in ['SELECT','ASK']:expected=json.loads(expected.serialize(format='json'))
     else:
      expected={'type':expected.type,'ntriples':expected.graph.serialize(format='nt')}
    row['expected']=expected
    if mode.value==20 and not reverse and 'ORDER BY' not in query and expected:
     row['unordered_last_value_candidates']=api.adhoc(query,QueryResultType.DICT_OF_STR2LIST,to_lowercase=lc)
   except Exception as exc:row['error']=type(exc).__name__
   rows.append(row)
(ROOT/'tests/fixtures/api/sparql.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
print('Recorded',len(rows),'SPARQL calls')
