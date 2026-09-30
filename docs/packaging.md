# Windows distribution

`scripts/package.cmake` assembles the static and shared CMake Release builds into a relocatable directory. It needs only CMake. Existing output directories are rejected to avoid overwriting another package.

```powershell
cmake -S . -B build-msvc -G "Visual Studio 16 2019" -A x64
cmake --build build-msvc --config Release
cmake -S . -B build-shared -G "Visual Studio 16 2019" -A x64 -DBUILD_SHARED_LIBS=ON -DBUILD_TESTING=OFF
cmake --build build-shared --config Release
cmake -DSTATIC_BUILD=build-msvc/Release -DSHARED_BUILD=build-shared/Release -DOUTPUT=dist/mutatoc-win-x64 -DZIP=ON -P scripts/package.cmake
```

The output includes `mutatoc.exe`, `lib/static/mutatoc.lib`, `lib/shared/mutatoc.dll` and its import library, `include/mutatoc.h`, documentation, examples and licenses. The script checks the packaged executable's version, runs a real ontology parse, and writes `package-manifest.json` with a SHA-256 for every packaged file. `-DZIP=ON` also creates an archive. Nothing else is needed at run time: the MSVC build uses the static C runtime, and tokenization, matching and RDF parsing are compiled into the library.

For native embedding, release buffers through `mc_free` and call `mc_destroy` before unloading the DLL.
