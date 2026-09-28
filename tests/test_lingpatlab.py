"""Compare the native LingPatLab replacement against independent source fixtures."""
import argparse,hashlib,importlib.util,json,os,subprocess,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--exe',type=Path,required=True);parser.add_argument('--require-clean-runtime',action='store_true');args,rest=parser.parse_known_args();args.exe=args.exe.resolve()
def equivalent(a,b):
 if isinstance(a,bool) or isinstance(b,bool):return type(a) is type(b) and a==b
 if isinstance(a,dict) and isinstance(b,dict):return a.keys()==b.keys() and all(equivalent(a[k],b[k]) for k in a)
 if isinstance(a,list) and isinstance(b,list):return len(a)==len(b) and all(equivalent(x,y) for x,y in zip(a,b))
 return a==b

class LingPatLabTests(unittest.TestCase):
 def test_reference_corpus(self):
  baseline=(ROOT/'tests/lingpatlab/parity.json').read_bytes()
  rows=json.loads(baseline);failures=[]
  corrected=json.loads((ROOT/'tests/lingpatlab/punctuation-corrections.json').read_text(encoding='utf-8'))
  self.assertEqual(hashlib.sha256(baseline).hexdigest(),corrected['baseline_sha256'])
  seen=set()
  for correction in corrected['corrections']:
   index=correction['index'];self.assertNotIn(index,seen);seen.add(index)
   self.assertEqual(rows[index]['request'],correction['request'])
   self.assertNotEqual({k:v for k,v in rows[index].items() if k!='request'},{k:v for k,v in correction.items() if k not in ['request','index']})
   rows[index]={k:v for k,v in correction.items() if k!='index'}
  with subprocess.Popen([str(args.exe),'--serve','--python',sys.executable],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True,encoding='utf-8') as process:
   for index,row in enumerate(rows):
    request=row['request'].copy()
    if request['method']=='text.split_on_len':request['separator']='\\'
    process.stdin.write(json.dumps(request)+'\n');process.stdin.flush();line=process.stdout.readline()
    if not line:raise AssertionError(f'Native engine exited at case {index}: {request}')
    actual=json.loads(line)
    if 'error' in row:ok=not actual['ok']
    else:ok=actual['ok'] and equivalent(actual['result'],row['expected'])
    if not ok:failures.append({'index':index,'request':request,'expected':row.get('expected'),'expected_error':row.get('error'),'actual':actual})
   process.stdin.close();process.wait(timeout=10)
   self.assertEqual(process.returncode,0)
  (ROOT/'artifacts').mkdir(exist_ok=True)
  (ROOT/'artifacts/lingpatlab-parity-failures.json').write_text(json.dumps(failures,ensure_ascii=True,indent=2),encoding='utf-8')
  self.assertEqual(len(failures),0,f'{len(failures)} failures; first: {str(failures[:1])[:1200]}')
  print(f'LingPatLab parity: {len(rows)} comparisons across {len({r["request"]["method"] for r in rows})} methods; {len(seen)} explicitly corrected Python reference outcomes.',flush=True)
 def test_invalid_requests_preserve_engine(self):
  requests=[{'method':'parse_input_tokens','tokens':['Dog',12]}, {'method':'parse_input_lines','lines':['Dog',None]}, {'method':'people_sequence','tokens':[{}]}, {'method':'extract_topics','sentences':[[{'text':'x','pos':1,'ent':''}]]}, {'method':'people_analyze','exact':None,'fuzzy':[]}, {'method':'people_cleanse','people':{'Smith':'John Smith'}}, {'method':'text.sliding_window','tokens':['x'],'window_size':1.5}, {'method':'text.sliding_window','tokens':['x'],'window_size':2**63}, {'method':'text.remove_punctuation','text':[]}, {'method':'text.update_determiners','text':'a  cat'}, {'method':'post_process_sentences','sentences':[None]}, {'method':'numbered_list_normalizer','text':12}, {'method':'generate_prompt','text':'hello','version':9}, {'method':'dictionary','name':'missing'}, {'method':'dto.token_to_string','token':{}}, {'method':'missing','text':'x'}]
  with subprocess.Popen([str(args.exe),'--serve','--python','missing-python'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,encoding='utf-8') as process:
   def call(q):
    process.stdin.write(json.dumps(dict(op='lingpatlab',**q))+'\n');process.stdin.flush();return json.loads(process.stdout.readline())
   for q in requests:self.assertFalse(call(q)['ok'],q)
   self.assertEqual(call({'method':'stem','text':'caresses'}),{'ok':True,'result':'care'})
   self.assertEqual(call({'method':'tokenize_input_text','text':'Dog and cat.'})['result'],['Dog ','and ','cat','.'])
   process.stdin.close();process.wait(timeout=10);self.assertEqual(process.returncode,0)
 def test_runtime_has_no_lingpatlab_packages(self):
  if not args.require_clean_runtime:self.skipTest('Clean environment check requires --require-clean-runtime')
  subprocess.run([sys.executable,'-I','-c',"import importlib.util; names=['lingpatlab','wordnet_lookup','unicodedata2']; assert all(importlib.util.find_spec(n) is None for n in names), names"],check=True)
if __name__=='__main__':unittest.main(argv=[sys.argv[0],*rest],verbosity=2)
