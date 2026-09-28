import json, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=subprocess.Popen([str(root/'build/mutatoc.exe'),'--serve'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,encoding='utf-8')
def call(q):
    p.stdin.write(json.dumps(q)+'\n');p.stdin.flush()
    r=json.loads(p.stdout.readline())
    if not r['ok']: raise RuntimeError(r)
    return r['result']
def normalized(v):
    if isinstance(v,dict): return {k:normalized(x) for k,x in sorted(v.items())}
    if isinstance(v,list): return sorted([normalized(x) for x in v],key=lambda x:json.dumps(x,sort_keys=True))
    return v
def differences(a,b,path=''):
    if type(a)!=type(b): return [(path,str(a)[:100],str(b)[:100])]
    if isinstance(a,dict):
        out=[]
        for k in sorted(a.keys()|b.keys()):
            if k not in a or k not in b:out.append((path+'/'+k,'missing in '+('reference' if k not in a else 'C'),''))
            else:out+=differences(a[k],b[k],path+'/'+k)
        return out
    if a!=b:return [(path,str(a)[:180],str(b)[:180])]
    return []
for file in sorted((root/'tests/fixtures/reference').glob('*.json')):
    call({'op':'load','path':str(root/'tests/fixtures/ontologies'/(file.stem+'.owl'))})
    native=call({'op':'snapshot'})
    original=json.loads(file.read_text(encoding='utf-8'))
    diff=differences(normalized(original),normalized(native))
    print(file.stem,len(diff),'differences')
    for row in diff[:30]:print(row)
p.stdin.close();p.wait()
