"""Generate differential fixtures using only the pinned Python reference."""
import ast, copy, hashlib, json, os, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'.reference/src'))
os.environ['SPAN_DISTANCE']='4'
from mutato.mda import UniversalMDAGenerator
from mutato.finder.multiquery import FindOntologyJSON
from mutato.parser import MutatoAPI
from lingpatlab import LingPatLab
import spacy
OUT=ROOT/'tests/fixtures/parity';OUT.mkdir(exist_ok=True)
model=spacy.load('en_core_web_sm');ling=LingPatLab()
def literals(node,env):
    if isinstance(node,ast.Constant):return node.value
    if isinstance(node,ast.Name):return env.get(node.id)
    if isinstance(node,(ast.List,ast.Tuple)):return [literals(x,env) for x in node.elts]
    return None
inputs={}
for f in sorted((ROOT/'tests/upstream/source/owl').rglob('test_*.py')):
    tree=ast.parse(f.read_text(encoding='utf-8'))
    constants=[n.value for n in ast.walk(tree) if isinstance(n,ast.Constant) and isinstance(n.value,str)]
    fixture=next((Path(v).stem for v in constants if (ROOT/'tests/fixtures/ontologies'/(Path(v).stem+'.owl')).exists()),None)
    if not fixture:continue
    globals={}
    for n in tree.body:
        if isinstance(n,ast.Assign):
            for t in n.targets:
                if isinstance(t,ast.Name):globals[t.id]=literals(n.value,globals)
    for cls in tree.body:
        if not isinstance(cls,ast.ClassDef):continue
        for fn in cls.body:
            if not isinstance(fn,ast.FunctionDef) or not fn.name.startswith('test_'):continue
            env=dict(globals)
            for n in ast.walk(fn):
                if isinstance(n,ast.Assign):
                    for t in n.targets:
                        if isinstance(t,ast.Name):env[t.id]=literals(n.value,env)
                if not isinstance(n,ast.Call) or not isinstance(n.func,ast.Attribute):continue
                if n.func.attr not in ['parse','swap_input_text','_swaps','_canons','_normals','_has_span_swap']:continue
                text=literals(n.args[0],env) if n.args else next((literals(k.value,env) for k in n.keywords if k.arg=='input_text'),None)
                ctr=next((literals(k.value,env) for k in n.keywords if k.arg=='ctr'),0)
                if isinstance(text,str):inputs.setdefault(fixture,{})[(text,ctr or 0)]=f'{f.relative_to(ROOT/"tests/upstream/source").as_posix()}::{cls.name}::{fn.name}'
# Additional adversarial inputs exercise contracts absent from the source assertions.
for text in ['Dog dog','fiscal policy blah analysis','café Dog 😀','Dog\tcat\nPoodle','Dog, cat!','"Poodle"','dog’s collar','DOG','',"Women's health",'dog-dog','Dog    cat']:
 inputs.setdefault('animals-test',{})[(text,0)]='additional-text-regression'
index=[]
for fixture,cases in inputs.items():
    data=UniversalMDAGenerator(fixture,str(ROOT/'tests/fixtures/ontologies'),None).generate()
    snapshot=fixture+'.snapshot.json'
    (OUT/snapshot).write_text(json.dumps(data,ensure_ascii=False),encoding='utf-8')
    finder=FindOntologyJSON(data,fixture);api=MutatoAPI(finder,en_spacy_model=model)
    rows=[]
    for (text,ctr),test in cases.items():
        sentence=ling.parse_input_text(text,en_spacy_model=model) if text else None
        tokens=[t.to_json() for t in sentence.tokens] if sentence else []
        try:
            expected=api.swap_input_tokens(copy.deepcopy(tokens),ctr=ctr)
            rendered=' '.join(t['swaps']['canon'] if t.get('swaps') else t['text'].strip() for t in expected if t.get('swaps') or t['text'].strip())
            rows.append({'test':test,'text':text,'ctr':ctr,'tokens':tokens,'expected_tokens':expected,'expected_text':rendered})
        except Exception as e:
            rows.append({'test':test,'text':text,'ctr':ctr,'tokens':tokens,'expected_error':type(e).__name__+': '+str(e)})
    casefile=fixture+'.cases.json';(OUT/casefile).write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
    index.append({'name':fixture,'snapshot':snapshot,'cases':casefile,'count':len(rows)})
    print(fixture,len(rows),flush=True)
(OUT/'index.json').write_text(json.dumps(index,indent=2)+'\n',encoding='utf-8')
print('Total',sum(x['count'] for x in index),flush=True)
