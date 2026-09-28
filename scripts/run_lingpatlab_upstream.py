"""Replay the unchanged LingPatLab tests through the C JSON API."""
import argparse,atexit,dataclasses,json,os,subprocess,sys,types
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PARSER=argparse.ArgumentParser();PARSER.add_argument('--exe',type=Path,default=Path(os.environ.get('MUTATOC_EXE',ROOT/'build-final/Release/mutatoc.exe')))
args,pytest_args=PARSER.parse_known_args()
args.exe=args.exe.resolve()
client=None

def call(method,**kwargs):
 global client
 if client is None:
  client=subprocess.Popen([str(args.exe.resolve()),'--serve','--python',sys.executable],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,encoding='utf-8')
 client.stdin.write(json.dumps(dict(op='lingpatlab',method=method,**kwargs))+'\n');client.stdin.flush()
 line=client.stdout.readline()
 if not line:raise RuntimeError('Native engine exited: '+str(client.poll()))
 result=json.loads(line)
 if not result['ok']:raise RuntimeError(result['error'])
 return result['result']

def close():
 if client is not None:
  client.stdin.close();client.wait(timeout=10);client.stdout.close()
atexit.register(close)

OtherInfo=dataclasses.make_dataclass('OtherInfo',[(n,object) for n in 'i idx orth head_i head_idx head_orth head_text'.split()])
SpacyResult=dataclasses.make_dataclass('SpacyResult',[(n,object) for n in 'id text tense noun_number verb_form sentiment pos tag dep ent shape is_alpha is_stop head other is_punct x y is_wordnet normal stem'.split()])
SpacyResult.to_json=lambda self:call('dto.to_json',data=dataclasses.asdict(self))
SpacyResult.to_string=lambda self:call('dto.token_to_string',token=dataclasses.asdict(self))
SpacyResult.is_noun=lambda self:call('dto.is_noun',token=dataclasses.asdict(self))
SpacyResult.is_hyphen=lambda self:call('dto.is_hyphen',token=dataclasses.asdict(self))
@dataclasses.dataclass
class Sentence:
 tokens:list
 def to_json(self):return call('dto.to_json',data=[dataclasses.asdict(t) for t in self.tokens])
 def to_string(self):return call('dto.sentence_to_string',tokens=self.to_json())
 def sentence_text(self):return call('dto.sentence_text',tokens=self.to_json())
 def size(self):return call('dto.size',tokens=self.to_json())
 def __iter__(self):return iter(self.tokens)
@dataclasses.dataclass
class Sentences:
 sentences:list
 def to_json(self):return [s.to_json() for s in self.sentences]
 def to_string(self):return call('dto.sentences_to_string',sentences=self.to_json())
 def sentence_text(self):return call('dto.sentences_text',sentences=self.to_json())
 def size(self):return call('dto.size',sentences=self.to_json())
 def __iter__(self):return iter(self.sentences)

def sentence(data):return Sentence([SpacyResult(**t) for t in data]) if data is not None else None
def to_spacy_result(data):return SpacyResult(**call('dto.to_spacy_result',data=data))
def transform_parse_results_to_sentences(data):return Sentences([sentence(s) for s in call('dto.restore_sentences',data=data)])
class LingPatLab:
 def parse_input_text(self,input_text,en_spacy_model=None):return sentence(call('parse_input_text',text=input_text))
 def parse_input_lines(self,input_lines,en_spacy_model=None):return Sentences([sentence(s) for s in call('parse_input_lines',lines=input_lines)])
 def extract_people(self,sentences):return ExtractPeople().process(Sentences([sentences]) if isinstance(sentences,Sentence) else sentences)
 def extract_topics(self,sentences):return ExtractTopics().process(Sentences([sentences]) if isinstance(sentences,Sentence) else sentences)
class ExtractPeople:
 def process(self,sentences):return call('extract_people',sentences=sentences.to_json())
class ExtractTopics:
 def process(self,sentences):return call('extract_topics',sentences=sentences.to_json())
class ParseInputTokens:
 def __init__(self,*args):pass
 def process(self,tokens):return sentence(call('parse_input_tokens',tokens=tokens))
class PorterStemmer:
 def stem(self,word):return call('stem',text=word)
class FileIO:
 join_cwd=staticmethod(lambda *args:os.path.join(os.getcwd(),*args))
 join=staticmethod(os.path.join)
 @staticmethod
 def exists_or_error(path):
  if not Path(path).exists():raise FileNotFoundError(path)
 @staticmethod
 def read_json(path):return json.loads(Path(path).read_text(encoding='utf-8'))
 @staticmethod
 def read_lines(path):return [line.replace('\n','').strip() for line in Path(path).read_text(encoding='utf-8').splitlines(keepends=True) if line]

exports={k:v for k,v in list(globals().items()) if k in ['OtherInfo','SpacyResult','Sentence','Sentences','LingPatLab','ExtractPeople','ExtractTopics','ParseInputTokens','PorterStemmer','FileIO','to_spacy_result','transform_parse_results_to_sentences']}
for name in ['lingpatlab','lingpatlab.utils','lingpatlab.utils.dto','lingpatlab.analyze','lingpatlab.analyze.bp','lingpatlab.parser','lingpatlab.parser.svc','lingpatlab.baseblock']:
 module=types.ModuleType(name);module.__path__=[];module.__dict__.update(exports);sys.modules[name]=module
os.chdir(ROOT/'tests/lingpatlab/upstream')
import pytest
raise SystemExit(pytest.main((pytest_args or ['tests'])+['-q','--tb=short','--junitxml='+str(ROOT/'artifacts/lingpatlab-upstream.xml')]))
