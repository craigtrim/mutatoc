"""Compare query behavior without assuming that the upstream test suite covers it."""
import json,os,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
EXE=Path(os.environ.get('MUTATOC_EXE',ROOT/'build/mutatoc.exe'))
proc=subprocess.Popen([str(EXE),'--serve'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,encoding='utf-8')
def call(q):
 proc.stdin.write(json.dumps(q)+'\n');proc.stdin.flush();return json.loads(proc.stdout.readline())
def norm(x):
 if isinstance(x,dict):return {k:norm(v) for k,v in x.items()}
 if isinstance(x,list):return sorted([norm(v) for v in x],key=lambda v:json.dumps(v,sort_keys=True))
 return x
current=None;failures=[];total=0
for row in json.loads((ROOT/'tests/fixtures/api/queries.json').read_text(encoding='utf-8')):
 key=(row['fixture'],row['request']['interface'])
 if key!=current:
  name,interface=key
  if interface in ('owl','data'):
   folder=ROOT/('tests/fixtures/api' if name in ('contract','proper') else 'tests/fixtures/ontologies')
   q={'op':'load','path':str(folder/(name+'.owl')),'name':name,'class_based':True,'interface':interface}
  else:q={'op':'load','snapshot':json.loads((ROOT/'tests/fixtures/api'/(name+'.snapshot.json')).read_text()),'name':name}
  r=call(q);assert r['ok'],r;current=key
 r=call(row['request']);total+=1
 if 'error' in row:equal=not r['ok']
 else:
  actual=r.get('result')
  if row['request']['method']=='absolute_path' and r['ok']:actual='$FIXTURE_DIRECTORY'
  equal=r['ok'] and json.dumps(norm(actual),sort_keys=True)==json.dumps(norm(row['expected']),sort_keys=True)
 if not equal:failures.append({**row,'actual':r})
proc.stdin.close();proc.wait()
(ROOT/'artifacts/api-failures.json').write_text(json.dumps(failures,ensure_ascii=False,indent=2),encoding='utf-8')
from collections import Counter
print('API parity:',total-len(failures),'/',total)
print(Counter((r['request']['interface'],r['request']['method']) for r in failures))
raise SystemExit(bool(failures))
