# Assemble a relocatable Windows distribution from the static and shared
# Release builds (craigtrim/mutatoc#1).
#
#   cmake -DSTATIC_BUILD=build-msvc/Release -DSHARED_BUILD=build-shared/Release
#         -DOUTPUT=dist/mutatoc-win-x64 [-DZIP=ON] -P scripts/package.cmake
#
# The package holds the executable, both library variants, the header,
# documentation, examples and licenses, plus a SHA-256 manifest.
cmake_minimum_required(VERSION 3.20)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
foreach(var STATIC_BUILD SHARED_BUILD OUTPUT)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "Set -D${var}=...")
  endif()
  get_filename_component(${var} "${${var}}" ABSOLUTE BASE_DIR "${ROOT}")
endforeach()
file(STRINGS "${ROOT}/include/mutatoc.h" version_line REGEX "#define MUTATOC_VERSION")
string(REGEX MATCH "\"([^\"]+)\"" _ "${version_line}")
set(VERSION "${CMAKE_MATCH_1}")

set(required "${STATIC_BUILD}/mutatoc.exe" "${STATIC_BUILD}/mutatoc.lib"
  "${SHARED_BUILD}/mutatoc.dll" "${SHARED_BUILD}/mutatoc.lib")
foreach(source IN LISTS required)
  if(NOT EXISTS "${source}")
    message(FATAL_ERROR "Missing package input: ${source}")
  endif()
endforeach()
if(EXISTS "${OUTPUT}")
  message(FATAL_ERROR "Output already exists; choose a new output directory: ${OUTPUT}")
endif()

file(MAKE_DIRECTORY "${OUTPUT}/lib/static" "${OUTPUT}/lib/shared" "${OUTPUT}/tests")
file(COPY "${STATIC_BUILD}/mutatoc.exe" DESTINATION "${OUTPUT}")
file(COPY "${STATIC_BUILD}/mutatoc.lib" DESTINATION "${OUTPUT}/lib/static")
file(COPY "${SHARED_BUILD}/mutatoc.dll" "${SHARED_BUILD}/mutatoc.lib" DESTINATION "${OUTPUT}/lib/shared")
file(COPY "${ROOT}/include" "${ROOT}/docs" "${ROOT}/examples" DESTINATION "${OUTPUT}")
file(COPY "${ROOT}/vendor" DESTINATION "${OUTPUT}" PATTERN "*.c" EXCLUDE PATTERN "*.h" EXCLUDE)
file(COPY "${ROOT}/tests/validation.json" DESTINATION "${OUTPUT}/tests")
file(COPY "${ROOT}/README.md" "${ROOT}/CHANGELOG.md" "${ROOT}/LICENSE" "${ROOT}/THIRD_PARTY_NOTICES.md"
  DESTINATION "${OUTPUT}")

# Smoke test the packaged executable from its own directory.
execute_process(COMMAND "${OUTPUT}/mutatoc.exe" --version
  OUTPUT_VARIABLE reported OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE code)
if(code OR NOT reported STREQUAL VERSION)
  message(FATAL_ERROR "Packaged executable reports '${reported}', expected ${VERSION}")
endif()
execute_process(COMMAND "${OUTPUT}/mutatoc.exe" --ontology "${ROOT}/tests/fixtures/ontologies/animals-test.owl"
  --input-text "Poodle" WORKING_DIRECTORY "${OUTPUT}"
  OUTPUT_VARIABLE parsed OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE code)
if(code OR parsed STREQUAL "")
  message(FATAL_ERROR "Packaged parse failed (${code})")
endif()

file(GLOB_RECURSE files RELATIVE "${OUTPUT}" "${OUTPUT}/*")
list(SORT files)
set(entries "")
foreach(f IN LISTS files)
  file(SIZE "${OUTPUT}/${f}" bytes)
  file(SHA256 "${OUTPUT}/${f}" digest)
  list(APPEND entries "    \"${f}\": {\"bytes\": ${bytes}, \"sha256\": \"${digest}\"}")
endforeach()
string(JOIN ",\n" body ${entries})
file(WRITE "${OUTPUT}/package-manifest.json" "{\n  \"version\": \"${VERSION}\",\n  \"files\": {\n${body}\n  }\n}\n")

if(ZIP)
  get_filename_component(parent "${OUTPUT}" DIRECTORY)
  get_filename_component(name "${OUTPUT}" NAME)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${name}.zip" --format=zip "${name}"
    WORKING_DIRECTORY "${parent}" RESULT_VARIABLE code)
  if(code)
    message(FATAL_ERROR "Could not create ${name}.zip")
  endif()
endif()
message(STATUS "Packaged: ${OUTPUT}")
message(STATUS "Verified parse: ${parsed}")
