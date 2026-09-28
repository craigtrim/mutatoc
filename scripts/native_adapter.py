"""Run the original assertions against the native executable.
Ontology and matching operations run in C. Raw text uses the retained
spaCy/LingPatLab worker. No Python Mutato implementation is imported.
"""
import atexit, enum, json, os, subprocess, sys, types, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
EXE=Path(os.environ.get('MUTATOC_EXE',ROOT/'build'/('mutatoc.exe' if os.name=='nt' else 'mutatoc')))
os.environ.setdefault('MUTATOC_PYTHON',sys.executable)
os.environ.setdefault('MUTATOC_SPACY_WORKER',str(ROOT/'runtime/spacy_worker.py'))
processes=[]
class Client:
    def __init__(self):
        self.p=subprocess.Popen([str(EXE),'--serve'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,encoding='utf-8')
        processes.append(self.p)
    def call(self,**q):
        self.p.stdin.write(json.dumps(q,ensure_ascii=False)+'\n');self.p.stdin.flush()
        line=self.p.stdout.readline()
        if not line:raise RuntimeError('Native process exited: '+str(self.p.poll()))
        r=json.loads(line)
        if not r['ok']:raise ValueError(r['error'])
        return r['result']
    def __del__(self):
        try:self.p.stdin.close();self.p.wait(timeout=5)
        except (AttributeError,OSError,subprocess.TimeoutExpired):pass
@atexit.register
def cleanup():
    for p in processes:
        if p.poll() is None:
            p.stdin.close()
            try:p.wait(timeout=5)
            except subprocess.TimeoutExpired:p.terminate()
class AskOwlAPI:
    interface='owl'
    def __init__(self,ontology_name,absolute_path,namespace=None,**kwargs):
        self.client=Client();self.ontology_name=ontology_name;self.path=absolute_path
        path=Path(absolute_path)/(ontology_name if ontology_name.endswith('.owl') else ontology_name+'.owl')
        self.client.call(op='load',path=str(path.resolve()),name=ontology_name,class_based=True,interface=self.interface,distance=int(os.getenv('SPAN_DISTANCE','4')))
    def __getattr__(self,name):
        def call(*args,**kwargs):
            if not args:
                for key in ['input_text','entity','predicate','predicate_name','gram_level']:
                    if key in kwargs:args=(kwargs.pop(key),);break
            value=self.client.call(op='query',method=name,args=list(args),kwargs=kwargs,interface=self.interface)
            if name in ['lookup','synonyms_lookup'] and isinstance(value,dict):value={int(k):v for k,v in value.items()}
            return value
        return call
class FindOntologyData(AskOwlAPI):
    interface='data'
    def __init__(self,ontologies,absolute_path,namespace=None,**kwargs):
        if len(ontologies)==1:
            super().__init__(ontologies[0],absolute_path,namespace)
        else:
            self.client=Client();self.path=absolute_path
            paths=[str((Path(absolute_path)/(name if name.endswith('.owl') else name+'.owl')).resolve()) for name in ontologies]
            self.client.call(op='load',paths=paths,class_based=True,interface='data')
    def absolute_path(self):return self.path
class FindOntologyJSON(AskOwlAPI):
    interface='json'
    def __init__(self,d_owl,ontology_name='ontology'):
        self.client=Client();self.ontology_name=ontology_name;self.d_owl=d_owl
        self.client.call(op='load',snapshot=d_owl,name=ontology_name)
class AskJsonAPI(FindOntologyJSON):
    interface='ask_json'
    def types(self):return self.client.call(op='query',method='types',interface='ask_json',args=[])
class MDAGenerator:
    def __init__(self,ontology_name,absolute_path,namespace=None):
        self.api=AskOwlAPI(ontology_name,absolute_path,namespace)
    def generate(self):
        d=self.api.client.call(op='snapshot')
        d['ngrams']={int(k):v for k,v in d['ngrams'].items()}
        d['trie']={int(k):v for k,v in d['trie'].items()} if d['trie'] else d['trie']
        d['synonyms']['lookup']={int(k):v for k,v in d['synonyms']['lookup'].items()}
        return d
class UniversalMDAGenerator(MDAGenerator):
    def __init__(self,ontology_name,absolute_path,namespace=None):
        self.api=object.__new__(AskOwlAPI);self.api.client=Client()
        path=Path(absolute_path)/(ontology_name if ontology_name.endswith('.owl') else ontology_name+'.owl')
        self.api.client.call(op='load',path=str(path.resolve()),name=ontology_name,distance=int(os.getenv('SPAN_DISTANCE','4')))
class MutatoAPI:
    def __init__(self,find_ontology_data,en_spacy_model=None):self.client=find_ontology_data.client
    def swap_input_text(self,input_text,ctr=0):
        if not input_text or not isinstance(input_text,str):return None
        return self.client.call(op='parse',text=input_text,ctr=ctr)['tokens']
    def swap_input_tokens(self,tokens,ctr=0):
        if tokens and hasattr(tokens[0],'to_json'):tokens=[t.to_json() for t in tokens]
        return self.client.call(op='parse_tokens',tokens=tokens,ctr=ctr)['tokens']
class OntologyParser:
    def __init__(self,owl_path,namespace=None):
        p=Path(owl_path);self.finder=UniversalMDAGenerator(p.stem,str(p.parent),namespace).api
    @classmethod
    def from_dict(cls,d_owl,name):
        obj=cls.__new__(cls);obj.finder=FindOntologyJSON(d_owl,name);return obj
    def to_dict(self):return self.finder.client.call(op='snapshot')
    def parse(self,text):return self.finder.client.call(op='parse',text=text)['text']
class GeneratePlusSpans:
    def process(self,d_results):
        c=Client();return c.call(op='generate_spans',data=d_results,plus_only=True,distance=int(os.getenv('SPAN_DISTANCE','4')))
class OWLSchema(enum.Enum):
    CLASS_BASED='class_based';MIXED='mixed';INDIVIDUAL='individual';SKOS='skos'
class OWLSchemaDetector:
    def __init__(self,graph):self.graph=graph
    def detect(self):
        c=Client();return OWLSchema[c.call(op='detect_schema',turtle=self.graph.serialize(format='turtle'))]
class Enforcer:
    @staticmethod
    def is_list(x):assert isinstance(x,list)
    @staticmethod
    def is_dict(x):assert isinstance(x,dict)
    @staticmethod
    def is_list_of_str(x):assert isinstance(x,list) and all(isinstance(t,str) for t in x)
    @staticmethod
    def is_dict_of_lists(x):assert isinstance(x,dict) and all(isinstance(t,list) for t in x.values())
class FileIO:
    @staticmethod
    def read_json(p):return json.loads(Path(p).read_text(encoding='utf-8'))
    @staticmethod
    def write_json(data,file_path,debug=False):Path(file_path).write_text(json.dumps(data),encoding='utf-8')
    @staticmethod
    def join_cwd(p):return str(Path(p).resolve())
class Stopwatch:
    def __init__(self):self.start=time.monotonic()
    def __str__(self):return str(time.monotonic()-self.start)
def install():
    modules=['mutato','mutato.api','mutato.core','mutato.parser','mutato.finder','mutato.finder.singlequery','mutato.finder.singlequery.svc','mutato.finder.multiquery','mutato.mda','mutato.mda.owl_schema','mutato.mda.owl_schema_detector']
    exports={k:v for k,v in globals().items() if isinstance(v,type)}
    for name in modules:
        m=types.ModuleType(name);m.__path__=[];m.__dict__.update(exports);sys.modules[name]=m
install()
