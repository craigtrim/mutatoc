"""Record optional matching services and the live finder against Python Mutato."""
import copy,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'.reference/src'))
from mutato.finder.multiquery import FindOntologyData
from mutato.parser import MutatoAPI
from mutato.parser.svc import PerformExactMatching,PerformSpanMatching,PerformHierarchyMatching,PerformSpacyMatching,AugmentTokenHierarchy
from lingpatlab import LingPatLab
import spacy
folder=ROOT/'tests/fixtures/api';model=spacy.load('en_core_web_sm');ling=LingPatLab()
stages={'exact':PerformExactMatching,'spans':PerformSpanMatching,'hierarchy':PerformHierarchyMatching,'spacy':PerformSpacyMatching,'augment':AugmentTokenHierarchy}
def finder():return FindOntologyData(['proper'],str(folder),None)
def tokenize(text):
 sentence=ling.parse_input_text(text,en_spacy_model=model)
 return [t.to_json() for t in sentence.tokens] if sentence else []
texts=['pupper wears doggo collar','hound fancy collar','Dog Cat','Dog walks through New York City.','Unlabelled collar','dog collar','kitty','Dog\tCat','cafÃ© Dog ðŸ˜€']
rows=[]
for text in texts:
 tokens=tokenize(text)
 for stage,cls in stages.items():
  print('Stage',stage,repr(text),flush=True)
  f=finder();input_tokens=copy.deepcopy(tokens)
  if stage=='hierarchy':input_tokens=AugmentTokenHierarchy(f).process(input_tokens)
  row={'fixture':'proper','request':{'op':'transform_tokens','stage':stage,'tokens':copy.deepcopy(input_tokens)}}
  try:
   service=cls(f)
   if stage=='hierarchy':
    current=input_tokens
    for _ in range(len(current)+1):
     following,recurse=service._process(current)
     if not recurse:break
     if following==current:
      row['upstream_defect']='Hierarchy service repeats an unchanged token list forever; C returns the unchanged list.'
      break
     current=following
    else:raise RuntimeError('Unexpected non-convergent oracle')
    row['expected']=current
   else:row['expected']=service.process(input_tokens)
  except Exception as e:row['error']=type(e).__name__
  rows.append(row)
 for ctr in [-1,0,1,2,3]:
  print('Pipeline',ctr,repr(text),flush=True)
  row={'fixture':'proper','request':{'op':'parse_tokens','tokens':copy.deepcopy(tokens),'ctr':ctr}}
  try:row['expected']=MutatoAPI(finder(),en_spacy_model=model).swap_input_tokens(copy.deepcopy(tokens),ctr=ctr)
  except Exception as e:row['error']=type(e).__name__
  rows.append(row)
(folder/'stages.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
print('Recorded',len(rows),'stage/live matching cases.')
