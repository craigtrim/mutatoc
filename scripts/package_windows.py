"""Build a relocatable Windows distribution from the verified native builds and venv."""
import argparse,hashlib,json,os,shutil,subprocess,sys,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser()
 p.add_argument('--static-build',type=Path,default=ROOT/'build-msvc/Release')
 p.add_argument('--shared-build',type=Path,default=ROOT/'build-shared/Release')
 p.add_argument('--runtime',type=Path,default=ROOT/'.runtime')
 p.add_argument('--output',type=Path,default=ROOT/'dist/mutatoc-win-x64')
 p.add_argument('--zip',action='store_true')
 a=p.parse_args()
 if os.name!='nt':p.error('Run on Windows with Python 3.11 x64')
 if sys.version_info[:2]!=(3,11):p.error('Use Python 3.11')
 base=Path(sys.base_prefix);target=a.output.resolve()
 if target.exists():p.error('Output already exists; choose a new output directory')
 required=[a.static_build/'mutatoc.exe',a.static_build/'mutatoc.lib',a.shared_build/'mutatoc.dll',a.shared_build/'mutatoc.lib',a.runtime/'Lib/site-packages/spacy',base/'python311.dll']
 for source in required:
  if not source.exists():p.error(f'Missing package input: {source}')
 for package in ['lingpatlab','wordnet_lookup','unicodedata2']:
  if any((a.runtime/'Lib/site-packages').glob(package+'*')):p.error(f'Removed runtime dependency still installed: {package}')
 target.mkdir(parents=True)
 shutil.copy2(a.static_build/'mutatoc.exe',target/'mutatoc.exe')
 for name,folder in [('static',a.static_build),('shared',a.shared_build)]:
  out=target/'lib'/name;out.mkdir(parents=True)
  shutil.copy2(folder/'mutatoc.lib',out/'mutatoc.lib')
  if name=='shared':shutil.copy2(folder/'mutatoc.dll',out/'mutatoc.dll')
 shutil.copytree(ROOT/'include',target/'include')
 shutil.copytree(ROOT/'docs',target/'docs')
 shutil.copytree(ROOT/'examples',target/'examples')
 shutil.copytree(ROOT/'vendor',target/'vendor',ignore=shutil.ignore_patterns('*.c','*.h'))
 (target/'tests').mkdir()
 for name in ['validation.json','rdf-validation.json','turtle-validation.json','punctuation-validation.json']:
  shutil.copy2(ROOT/'tests'/name,target/'tests'/name)
 for name in ['README.md','LICENSE','THIRD_PARTY_NOTICES.md']:
  shutil.copy2(ROOT/name,target/name)
 runtime=target/'runtime';runtime.mkdir()
 for name in ['spacy_worker.py','sparql_worker.py','requirements.lock.txt','sparql-requirements.lock.txt','model-manifest.json']:
  shutil.copy2(ROOT/'runtime'/name,runtime/name)
 python=runtime/'python';python.mkdir()
 for name in ['python.exe','python3.dll','python311.dll','vcruntime140.dll','vcruntime140_1.dll','LICENSE.txt']:
  shutil.copy2(base/name,python/name)
 ignore=shutil.ignore_patterns('__pycache__','*.pyc')
 shutil.copytree(base/'DLLs',python/'DLLs',ignore=ignore)
 shutil.copytree(base/'Lib',python/'Lib',ignore=shutil.ignore_patterns('site-packages','__pycache__','*.pyc','test','tests','idlelib','tkinter','ensurepip'))
 shutil.copytree(a.runtime/'Lib/site-packages',python/'Lib/site-packages',ignore=ignore)
 (python/'python311._pth').write_text('Lib\nDLLs\nLib/site-packages\nimport site\n')
 smoke_env=os.environ.copy()
 for name in ['MUTATOC_PYTHON','MUTATOC_SPACY_WORKER','MUTATOC_SPARQL_WORKER','PYTHONHOME','PYTHONPATH']:
  smoke_env.pop(name,None)
 smoke_env['PATH']=str(Path(os.environ['SystemRoot'])/'System32')
 smoke_env['PYTHONDONTWRITEBYTECODE']='1'
 subprocess.run([str(python/'python.exe'),'-I','-c',"import importlib.util; names=['lingpatlab','wordnet_lookup','unicodedata2']; assert all(importlib.util.find_spec(n) is None for n in names)"],env=smoke_env,check=True)
 subprocess.run([str(python/'python.exe'),'-m','pip','check'],env=smoke_env,check=True)
 fixture=ROOT/'tests/fixtures/ontologies/animals-test.owl'
 result=subprocess.run([str(target/'mutatoc.exe'),'--ontology',str(fixture),'--input-text','Poodle'],cwd=target,env=smoke_env,check=True,capture_output=True,text=True)
 if not result.stdout.strip():raise RuntimeError('Packaged parse produced no text')
 files={str(f.relative_to(target)).replace('\\','/'):{'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in sorted(target.rglob('*')) if f.is_file()}
 (target/'package-manifest.json').write_text(json.dumps({'version':'0.2.2','python':sys.version.split()[0],'files':files},indent=2))
 if a.zip:
  with zipfile.ZipFile(target.parent/(target.name+'.zip'),'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
   for f in target.rglob('*'):
    if f.is_file():z.write(f,str(f.relative_to(target.parent)))
 print('Packaged:',target)
 print('Verified bundled preprocessing:',result.stdout.strip())
if __name__=='__main__':main()
