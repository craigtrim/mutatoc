"""Install the pinned preprocessing environment from the reference model archive."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import venv

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model-archive", required=True, type=Path)
    parser.add_argument("--with-sparql", action="store_true", help="Include the optional RDFLib ad hoc query worker dependencies")
    parser.add_argument("--venv", type=Path, default=ROOT / ".runtime")
    args = parser.parse_args()
    if sys.version_info[:2] != (3, 11):
        parser.error("Use Python 3.11, the tested reference interpreter version.")
    manifest = json.loads((ROOT / "runtime/model-manifest.json").read_text())
    archive = args.model_archive.resolve()
    if not archive.is_file():
        parser.error(f"Model archive does not exist: {archive}")
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != manifest["sha256"]:
        parser.error(f"Model checksum differs from the pinned reference: {digest}")
    target = args.venv.resolve()
    venv.EnvBuilder(with_pip=True, symlinks=os.name != "nt").create(target)
    python = target / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    subprocess.run([str(python), "-m", "pip", "install", "-r", str(ROOT / "runtime/requirements.lock.txt")], check=True)
    subprocess.run([str(python), "-m", "pip", "install", "--no-deps", str(archive)], check=True)
    if args.with_sparql:
        subprocess.run([str(python), "-m", "pip", "install", "-r", str(ROOT / "runtime/sparql-requirements.lock.txt")], check=True)
    subprocess.run([str(python), "-m", "pip", "uninstall", "-y", "lingpatlab", "wordnet-lookup", "unicodedata2"], check=True)
    subprocess.run([str(python), "-m", "pip", "check"], check=True)
    subprocess.run([str(python), str(ROOT / "runtime/spacy_worker.py")], input="", text=True, check=True)
    print(f"Set MUTATOC_PYTHON to {python}")


if __name__ == "__main__":
    main()
