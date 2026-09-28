"""Native collection loading, optional services, and external synonym contracts."""
import argparse,copy,json,os,subprocess,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
EXE=Path(os.environ.get('MUTATOC_EXE',ROOT/'build'/('mutatoc.exe' if os.name=='nt' else 'mutatoc')))
class Native:
 def __init__(self):self.p=subprocess.Popen([str(EXE),'--serve'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,encoding='utf-8')
 def request(self,**q):
  self.p.stdin.write(json.dumps(q)+'\n');self.p.stdin.flush();return json.loads(self.p.stdout.readline())
 def call(self,**q):
  r=self.request(**q)
  if not r['ok']:raise AssertionError(r)
  return r['result']
 def close(self):self.p.stdin.close();self.p.wait(timeout=10);self.p.stdout.close()
def token(text,i):return {'id':i,'text':text,'normal':text.lower(),'x':i*10,'y':i*10+len(text),'ent':''}
class ExtensionTests(unittest.TestCase):
 def setUp(self):self.c=Native()
 def tearDown(self):self.c.close()
 def test_all_optional_stages_and_live_matches(self):
  self.c.call(op='load',path=str(ROOT/'tests/fixtures/api/proper.owl'),interface='data',class_based=True)
  rows=json.loads((ROOT/'tests/fixtures/api/stages.json').read_text(encoding='utf-8'))
  for row in rows:
   with self.subTest(stage=row['request'].get('stage'),ctr=row['request'].get('ctr')):
    r=self.c.request(**row['request'])
    if 'error' in row:self.assertFalse(r['ok'])
    else:
     self.assertTrue(r['ok'],r)
     actual=r['result']['tokens'] if row['request']['op']=='parse_tokens' else r['result']
     self.assertEqual(actual,row['expected'])
  print('Optional stages/live matching:',len(rows),'cases.',flush=True)
 def test_window_invalidation_against_python_reference(self):
  corpus=json.loads((ROOT/'tests/fixtures/api/matching-regressions.json').read_text(encoding='utf-8'))
  self.c.call(op='load',name='window-regression',snapshot=corpus['snapshot'])
  for case in corpus['cases']:
   with self.subTest(case=case['id'],text=case['text']):
    self.assertEqual(self.c.call(op='transform_tokens',stage='exact',tokens=case['tokens']),case['expected'])
  print('Exact window Python reference:',len(corpus['cases']),'complete token cases.',flush=True)
 def test_multiple_ontologies_keep_all_names_and_matching(self):
  paths=[str(ROOT/'tests/fixtures/ontologies'/name) for name in ['animals-test.owl','colors-test.owl']]
  self.c.call(op='load',paths=paths,interface='data',class_based=True)
  self.assertEqual(self.c.call(op='query',method='ontologies'),['animals-test','colors-test'])
  out=self.c.call(op='parse_tokens',tokens=[token('dog',0),token('red',1)])
  self.assertEqual(out['text'],'dog red')
  self.assertEqual(len(out['tokens']),2)
  for t in out['tokens']:self.assertEqual(t['swaps']['ontologies'],['animals-test','colors-test'])
  labels=self.c.call(op='query',interface='data',method='labels')
  self.assertIn('dog',labels);self.assertIn('red',labels)
  before=self.c.call(op='snapshot')
  self.assertFalse(self.c.request(op='load',paths=[paths[0],str(ROOT/'missing.owl')])['ok'])
  self.assertEqual(self.c.call(op='snapshot'),before)
 def test_external_synonyms_are_live_and_survive_collection_merge(self):
  source=ROOT/'tests/fixtures/api/proper.owl'
  self.c.call(op='load',paths=[str(source),str(ROOT/'tests/fixtures/ontologies/animals-test.owl')],interface='data',class_based=True)
  self.assertEqual(self.c.call(op='parse_tokens',tokens=[token('pupper',0),token('kitty',1)])['text'],'dog cat')
  self.assertIn('pupper',self.c.call(op='query',interface='data',method='synonyms')['dog'])
  self.c.call(op='load',path=str(source),class_based=True)
  self.assertNotIn('pupper',self.c.call(op='query',interface='owl',method='synonyms')['dog'])
 def test_default_and_redeclared_query_prefixes(self):
  self.c.call(op='load',turtle='<urn:s> <http://xmlns.com/foaf/0.1/name> "Name" .')
  self.assertEqual(self.c.call(op='query',interface='owl',method='by_predicate',args=['foaf:name']),{'urn:s':['name']})
  self.c.call(op='load',turtle='@prefix rdf: <http://test/#> . rdf:s rdf:p rdf:o .')
  self.assertEqual(self.c.call(op='query',interface='owl',method='by_predicate',args=['rdf1:p']),{'s':['o']})
  self.c.call(op='load',turtle='@prefix ex: <http://a/#> . ex:s ex:p "first" . @prefix ex: <http://b/#> . ex:s ex:p "second" .')
  self.assertEqual(self.c.call(op='query',interface='owl',method='by_predicate',args=['ex:p']),{'s':['first']})
  self.assertEqual(self.c.call(op='query',interface='owl',method='by_predicate',args=['ex1:p']),{'s':['second']})
  self.assertFalse(self.c.request(op='read_rdf',turtle='<urn:s> foaf:name "Name" .')['ok'])
 def test_blank_nodes_are_scoped_to_source_documents(self):
  prefix='@prefix : <http://example/#> . @prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> . '
  sources=[{'name':'first','turtle':prefix+'_:same rdfs:label "first" .'}, {'name':'second','turtle':prefix+'_:same rdfs:label "second" .'}]
  self.c.call(op='load',sources=sources)
  triples=self.c.call(op='triples')
  self.assertEqual(len(triples),2)
  self.assertEqual(len({t['subject']['value'] for t in triples}),2)
 def test_ambiguous_canon_uses_declared_source_order(self):
  def snapshot(canon):return {'synonyms':{'fwd':{canon:['shared']},'rev':{'shared':[canon]},'lookup':{'1':['shared',canon]}},'labels':{canon:canon},'spans':{}}
  self.c.call(op='load',sources=[{'name':'a','snapshot':snapshot('first')},{'name':'b','snapshot':snapshot('second')}])
  self.assertEqual(self.c.call(op='parse_tokens',tokens=[token('shared',0)])['text'],'first')
  self.c.call(op='load',sources=[{'name':'b','snapshot':snapshot('second')},{'name':'a','snapshot':snapshot('first')}])
  self.assertEqual(self.c.call(op='parse_tokens',tokens=[token('shared',0)])['text'],'second')
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--exe',type=Path);args,rest=p.parse_known_args()
 if args.exe:EXE=args.exe.resolve()
 unittest.main(argv=[sys.argv[0],*rest],verbosity=2)
