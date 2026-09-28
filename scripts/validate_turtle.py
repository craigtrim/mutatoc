"""Run the W3C RDF 1.1 Turtle manifest against the native graph reader."""
import argparse,json,subprocess
from pathlib import Path
import rdflib
from rdflib.compare import isomorphic
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--exe',default=str(ROOT/'build-release/mutatoc.exe'));p.add_argument('--folder',type=Path,default=ROOT/'tests/w3c-turtle');a=p.parse_args()
MF=rdflib.Namespace('http://www.w3.org/2001/sw/DataAccess/tests/test-manifest#')
RT=rdflib.Namespace('http://www.w3.org/ns/rdftest#')
base='https://w3c.github.io/rdf-tests/rdf/rdf11/rdf-turtle/'
g=rdflib.Graph().parse(a.folder/'manifest.ttl',publicID=base+'manifest.ttl')
client=subprocess.Popen([a.exe,'--serve'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,encoding='utf-8')
def call(q):
 client.stdin.write(json.dumps(q)+'\n');client.stdin.flush();return json.loads(client.stdout.readline())
def term(v):
 if v['kind']=='iri':return rdflib.URIRef(v['value'])
 if v['kind']=='blank':return rdflib.BNode(v['value'])
 return rdflib.Literal(v['value'],lang=v.get('language'),datatype=None if v.get('language') else v.get('datatype',str(rdflib.XSD.string)))
rows=[]
for test,_,action in g.triples((None,MF.action,None)):
 name=str(action).rsplit('/',1)[-1];text=(a.folder/name).read_bytes().decode('utf-8');kind=g.value(test,rdflib.RDF.type)
 r=call({'op':'read_rdf','turtle':text,'base':base+name})
 negative=kind in [RT.TestTurtleNegativeSyntax,RT.TestTurtleNegativeEval]
 ok=not r['ok'] if negative else r['ok'];result=g.value(test,MF.result)
 if ok and result and not negative:
  expected=rdflib.Graph().parse(a.folder/str(result).rsplit('/',1)[-1],format='nt')
  for s,p,o in list(expected):
   if isinstance(o,rdflib.Literal) and not o.language and not o.datatype:
    expected.remove((s,p,o));expected.add((s,p,rdflib.Literal(str(o),datatype=rdflib.XSD.string)))
  native=rdflib.Graph()
  for t in r['result']:native.add(tuple(term(t[k]) for k in ['subject','predicate','object']))
  ok=isomorphic(native,expected)
 row={'name':name,'passed':bool(ok),'negative':negative}
 if not ok:
  row['detail']=r.get('error',{'message':'accepted invalid syntax' if negative else 'different graph'})
  print(row,flush=True)
 rows.append(row)
client.stdin.close();client.wait(timeout=10)
print(sum(r['passed'] for r in rows),'/',len(rows),'Turtle tests passed')
(ROOT/'artifacts').mkdir(exist_ok=True)
(ROOT/'artifacts/turtle-validation.json').write_text(json.dumps(rows,indent=2))
raise SystemExit(not all(r['passed'] for r in rows))
