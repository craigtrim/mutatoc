"""Reference SPARQL parity and graph reload through the native public API."""
import argparse,json,os,subprocess,sys,unittest
from pathlib import Path
import rdflib
from rdflib.compare import isomorphic
ROOT=Path(__file__).resolve().parents[1]
EXE=Path(os.environ.get('MUTATOC_EXE',ROOT/'build-release'/('mutatoc.exe' if os.name=='nt' else 'mutatoc')))
def unordered(x):
 if isinstance(x,list):return sorted((unordered(v) for v in x),key=repr)
 if isinstance(x,dict):return {k:unordered(v) for k,v in x.items()}
 return x
class SparqlTests(unittest.TestCase):
 def setUp(self):
  self.p=subprocess.Popen([str(EXE),'--serve','--python',sys.executable],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,encoding='utf-8')
  self.call(op='load',path=str(ROOT/'tests/fixtures/api/proper.owl'),class_based=True)
 def tearDown(self):
  self.p.stdin.close();self.p.wait(timeout=10);self.p.stdout.close();self.assertEqual(self.p.returncode,0)
 def request(self,**q):
  self.p.stdin.write(json.dumps(q)+'\n');self.p.stdin.flush();return json.loads(self.p.stdout.readline())
 def call(self,**q):
  r=self.request(**q);self.assertTrue(r['ok'],r);return r['result']
 def test_reference_queries(self):
  rows=json.loads((ROOT/'tests/fixtures/api/sparql.json').read_text(encoding='utf-8'))
  for row in rows:
   with self.subTest(request=row['request']):
    r=self.request(**row['request'])
    if 'error' in row:self.assertFalse(r['ok'],r);continue
    self.assertTrue(r['ok'],r)
    expected=row['expected'];actual=r['result']
    if 'unordered_last_value_candidates' in row:
     candidates=row['unordered_last_value_candidates']
     self.assertEqual(set(actual),set(expected))
     for key,value in actual.items():self.assertIn(value,candidates[key])
     continue
    if isinstance(expected,dict) and 'ntriples' in expected:
     left=rdflib.Graph().parse(data=expected['ntriples'],format='nt');right=rdflib.Graph()
     def term(v):
      if v['kind']=='iri':return rdflib.URIRef(v['value'])
      if v['kind']=='blank':return rdflib.BNode(v['value'])
      return rdflib.Literal(v['value'],lang=v.get('language'),datatype=None if v.get('language') else v.get('datatype'))
     for t in actual['triples']:right.add(tuple(term(t[k]) for k in ['subject','predicate','object']))
     self.assertTrue(isomorphic(left,right));continue
    # SELECT rows are ordered when the source query says ORDER BY.
    if 'ORDER BY' in row['request']['query']:self.assertEqual(actual,expected)
    else:self.assertEqual(unordered(actual),unordered(expected))
  print('SPARQL parity:',len(rows),'reference calls.',flush=True)
 def test_graph_reload_and_native_query_entrypoint(self):
  q='SELECT (COUNT(*) AS ?n) WHERE {?s ?p ?o}'
  self.assertEqual(self.call(op='query',interface='owl',method='adhoc',args=[q,10]),['47'])
  self.call(op='load',turtle='<urn:a> <urn:b> <urn:c> .')
  self.assertEqual(self.call(op='sparql',query=q,result_type='LIST_OF_STRINGS'),['1'])
  self.assertFalse(self.request(op='sparql',query='SELECT invalid')['ok'])
  self.assertEqual(self.call(op='sparql',query=q,result_type=10),['1'])
 def test_typed_literals_and_collection_query(self):
  literals=[('dateTime','2020-01-01T01:02:03Z'),('date','2020-01-01Z'),('time','01:02:03Z'),('gYear','2020Z'),('gYearMonth','2020-02Z'),('duration','PT25H'),('dayTimeDuration','P1DT2H'),('yearMonthDuration','P2Y12M'),('base64Binary','Y Q=='),('integer','+0001'),('integer','-000'),('integer','123456789123456789123456789'),('decimal','+0001.20'),('decimal','1E3'),('double','1e0'),('double','-0'),('double','INF'),('double','NaN'),('boolean','0'),('hexBinary','ABEF'),('normalizedString',' a\tb\r\nc '),('token',' a\tb\r\nc ')]
  for dtype,text in literals:
   with self.subTest(dtype=dtype,text=text):
    literal=rdflib.Literal(text,datatype=rdflib.XSD[dtype],normalize=False)
    source='<urn:s> <urn:p> '+literal.n3()+' .'
    self.call(op='load',turtle=source)
    actual=self.call(op='triples')[0]['object']['value']
    self.assertEqual(actual,str(rdflib.Literal(text,datatype=rdflib.XSD[dtype])))
  turtle='@prefix : <http://test/#> . :s :p [ :value "nested" ] .'
  self.call(op='load',sources=[{'name':'a','turtle':turtle},{'name':'b','turtle':turtle}])
  self.assertEqual(self.call(op='sparql',query='SELECT ?s ?o WHERE {?s :p ?o}',result_type=21),{'s':['nested','nested']})
  self.call(op='load',sources=[{'turtle':'@prefix xsd: <http://www.w3.org/2001/XMLSchema#> . <urn:s> <urn:p> "2020-01-01Z"^^xsd:date .'}])
  self.assertEqual(self.call(op='triples')[0]['object']['value'],'2020-01-01')
 def test_arbitrary_graph_does_not_require_mda_extraction(self):
  turtle='@prefix : <http://test/#> . @prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> . _:a :p _:b . _:b :p _:a . :A rdfs:subClassOf :B . :B rdfs:subClassOf :A .'
  self.call(op='load',turtle=turtle,graph_only=True)
  self.assertTrue(self.call(op='sparql',query='ASK {?a :p ?b . ?b :p ?a}')['boolean'])
  self.assertEqual(set(self.call(op='query',interface='owl',method='ancestors',args=['A'])),{'A','B'})
  self.call(op='load',path=str(ROOT/'tests/fixtures/ontologies/animals-test.owl'),graph_only=True)
  self.assertTrue(self.call(op='snapshot')['synonyms']['lookup'])
 def test_sparql_does_not_start_spacy(self):
  self.call(op='configure_spacy',python=sys.executable,worker=str(ROOT/'runtime/spacy_worker.py'),model='missing_model')
  self.assertEqual(self.call(op='sparql',query='ASK {?s ?p ?o}')['boolean'],True)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--exe',type=Path);a,rest=p.parse_known_args()
 if a.exe:EXE=a.exe.resolve()
 unittest.main(argv=[sys.argv[0],*rest],verbosity=2)
