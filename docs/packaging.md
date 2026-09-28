# Windows distribution

`scripts/package_windows.py` assembles the two CMake Release builds and the pinned `.runtime` environment into a relocatable directory. Run it with the installed 64-bit Python 3.11 interpreter used to create that environment. Existing output directories are rejected to avoid overwriting another package.

The output includes `mutatoc.exe`, `lib/static/mutatoc.lib`, `lib/shared/mutatoc.dll` and its import library, `include/mutatoc.h`, documentation and licenses. `runtime/python` contains the interpreter, standard library, binary dependencies, installed package metadata and model. The Python path configuration is relative to the package and ignores external Python environment paths. The CLI discovers this interpreter automatically unless `--python` or `MUTATOC_PYTHON` overrides it.

The script runs `pip check` and a real ontology parse with Python removed from `PATH`, then creates `package-manifest.json` with a SHA-256 for every packaged file. `--zip` also creates an archive. The package has been exercised from a directory containing spaces and non-ASCII characters. The local `.runtime` virtual environment itself is not portable; distribute the assembled package.

The package preserves dependency licenses and distribution metadata. The model archive checksum is in `runtime/model-manifest.json`. For native embedding, release buffers through `mc_free` and call `mc_destroy` before unloading the DLL. The library does not automatically configure workers: pass the bundled interpreter and worker paths to `mc_use_spacy` and `mc_use_sparql`.


The packager rejects environments that still contain LingPatLab, wordnet-lookup or unicodedata2. Running `setup_spacy.py` upgrades an existing project environment by uninstalling those retired dependencies. Native dictionary tables and Unicode rules are compiled into the library; their provenance and licenses are included under `vendor/`. Runtime model use still requires the bundled Python/spaCy files.
