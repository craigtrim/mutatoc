# C coding style

Project C sources and headers follow the [Linux kernel coding style](https://docs.kernel.org/process/coding-style.html). The root `.clang-format` sets eight-column tabs, an 80-column preference, and kernel brace placement. Function opening braces go on their own line; control statement braces stay on the opening line. Keep long strings intact and preserve include order.

Use plain `/* ... */` comments. Explain ownership, compatibility requirements, or reasoning that the code does not make clear. Keep comments concise and use Craig's tonality conventions. Start each file with a descriptive block comment that states its purpose. Retain source and version information in generated headers.

Format the project files from the repository root with clang-format 20.1.8, the version pinned in the reference test environment:

```powershell
$cFiles = Get-ChildItem -Path src\*.c,src\*.h,include\*.h,examples\*.c,tests\*.c
clang-format -i $cFiles.FullName
```

Vendor sources retain their upstream style. Formatting changes preserve identifiers, types, expressions, string literals, and control flow. Existing public API types stay compatible with callers.

After regenerating lookup tables, restore their descriptive headers and run the formatter. Refresh the generated file hashes in `vendor/lingpatlab/data-manifest.json` after any formatting or comment changes to those headers.
