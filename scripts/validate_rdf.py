"""Compare complete C-parsed graphs with RDFLib, modulo blank-node identity."""
import json,os,subprocess
from pathlib import Path
from rdflib import Graph,URIRef,BNode,Literal
from rdflib.compare import isomorphic
ROOT=Path(__file__).resolve().parents[1]
EXE=os.environ.get('MUTATOC_EXE',str(ROOT/'build/mutatoc.exe'))
p=subprocess.Popen([EXE,'--serve'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,encoding='utf-8')
def call(q):
 p.stdin.write(json.dumps(q)+'\n');p.stdin.flush();r=json.loads(p.stdout.readline());assert r['ok'],r;return r['result']
def term(t):
 if t['kind']=='iri':return URIRef(t['value'])
 if t['kind']=='blank':return BNode(t['value'])
 return Literal(t['value'],lang=t.get('language'),datatype=None if t.get('language') else t.get('datatype'))
rows=[]
for file in sorted((ROOT/'tests/fixtures/ontologies').glob('*.owl')):
 ref=Graph().parse(file,format='turtle');call({'op':'load','path':str(file)})
 triples=call({'op':'triples'});native=Graph()
 for t in triples:native.add(tuple(term(t[k]) for k in ['subject','predicate','object']))
 equal=isomorphic(ref,native)
 row={'file':file.name,'reference_triples':len(ref),'native_triples':len(native),'isomorphic':equal};rows.append(row);print(row,flush=True)
 assert equal,file
p.stdin.close();p.wait()
(ROOT/'tests/rdf-validation.json').write_text(json.dumps(rows,indent=2)+'\n')
