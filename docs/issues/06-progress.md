All 961 retained upstream test methods and 4 subtests passed against the C engine with the spaCy/LingPatLab worker (329.74 seconds on Windows). The per-method manifest now records those results.

The separate 393-case differential corpus also compares complete raw-text token objects and nested histories, including statistical annotations and exact numeric hashes. Comparing canonical text alone is no longer the raw-text compatibility check.

Native CTest coverage and worker integration tests pass. A Windows/Linux compatibility workflow has been added locally; Linux and MSVC execution are not yet verified here. Broader public API and distribution work remain tracked separately.
