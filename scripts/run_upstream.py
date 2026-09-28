import os, sys, shutil
from pathlib import Path
root=Path(__file__).resolve().parents[1]
target=root/'.reference/native-tests'
target.mkdir(parents=True,exist_ok=True)
(root/'artifacts').mkdir(exist_ok=True)
shutil.copytree(root/'tests/upstream/source',target/'tests',dirs_exist_ok=True)
shutil.copytree(root/'tests/fixtures/ontologies',target/'tests/test_data/ontologies',dirs_exist_ok=True)
shutil.copytree(root/'tests/fixtures/generated',target/'tests/test_data/generated',dirs_exist_ok=True)
os.chdir(target)
import native_adapter
import pytest
raise SystemExit(pytest.main((sys.argv[1:] or ['tests'])+['-q','--tb=short','--disable-warnings','--junitxml='+str(root/'artifacts/native-upstream.xml')]))
