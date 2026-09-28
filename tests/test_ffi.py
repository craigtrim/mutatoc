"""Load the shared library through its public C ABI, including concurrent engines."""
import argparse,ctypes,json,sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class Error(ctypes.Structure):
 _fields_=[('code',ctypes.c_int),('line',ctypes.c_size_t),('column',ctypes.c_size_t),('message',ctypes.c_char*512)]
p=argparse.ArgumentParser();p.add_argument('--library',required=True,type=Path);a=p.parse_args()
lib=ctypes.CDLL(str(a.library.resolve()))
lib.mc_create.restype=ctypes.c_void_p
lib.mc_destroy.argtypes=[ctypes.c_void_p]
lib.mc_free.argtypes=[ctypes.c_void_p]
lib.mc_request.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.POINTER(Error)]
lib.mc_request.restype=ctypes.c_void_p
lib.mc_use_spacy.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_uint,ctypes.POINTER(Error)]
lib.mc_use_spacy.restype=ctypes.c_int

def run(index):
 engine=lib.mc_create();assert engine
 def call(q):
  error=Error();raw=lib.mc_request(engine,json.dumps(q).encode(),ctypes.byref(error));assert raw
  try:r=json.loads(ctypes.string_at(raw))
  finally:lib.mc_free(raw)
  assert r['ok'],r
  return r['result']
 try:
  call({'op':'load','path':str(ROOT/'tests/fixtures/ontologies/animals-test.owl')})
  for _ in range(20):
   result=call({'op':'parse_tokens','tokens':[{'id':index,'text':'Dog','normal':'dog','x':0,'y':3,'ent':'','other':{'orth':18446744073709551615}}]})
   assert result['text']=='dog',result
   assert result['tokens'][0]['swaps']['tokens'][0]['other']['orth']==18446744073709551615
  if index==0:
   error=Error();assert lib.mc_use_spacy(engine,str(sys.executable).encode(),str(ROOT/'runtime/spacy_worker.py').encode(),b'en_core_web_sm',120000,ctypes.byref(error)),error.message
   assert call({'op':'parse','text':'Dog walks through London.'})['text']=='dog walks through London .'
 finally:lib.mc_destroy(engine)
with ThreadPoolExecutor(max_workers=8) as pool:list(pool.map(run,range(24)))
print('C ABI: 24 concurrent engines, 480 prepared requests, full raw-text worker call passed.')
