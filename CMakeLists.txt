cmake_minimum_required(VERSION 3.25)
project(words LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Freetype REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)
find_package(Clipper2 CONFIG REQUIRED)
find_package(utf8proc CONFIG REQUIRED)

# The GL-free world model: geometry, shaping, scene. Builds everywhere;
# the wasm app and the native test binary both link it.
add_library(words_core STATIC
  src/demo_scene.cc
  src/hbb.cc
  src/layout.cc
  src/palette.cc
  src/quad_tree.cc
  src/shape.cc
  src/svg.cc
  src/text.cc
  src/word.cc
)
target_link_libraries(words_core PUBLIC
  Freetype::Freetype
  harfbuzz::harfbuzz
  Clipper2::Clipper2
  utf8proc::utf8proc
)
target_include_directories(words_core PUBLIC src)

if(EMSCRIPTEN)
  add_executable(words
    src/gl_util.cc
    src/main.cc
    src/word_renderer.cc
  )
  target_link_libraries(words PRIVATE words_core)

  # Everything the browser loads ends up in ${CMAKE_BINARY_DIR}/dist.
  set(WORDS_DIST_DIR ${CMAKE_BINARY_DIR}/dist)
  set_target_properties(words PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${WORDS_DIST_DIR})

  target_link_options(words PRIVATE
    -sENVIRONMENT=web
    -sMIN_WEBGL_VERSION=2
    -sMAX_WEBGL_VERSION=2
    -sALLOW_MEMORY_GROWTH=1
    -sMODULARIZE=1
    -sEXPORT_NAME=createWordsModule
    "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets/fonts/sexsmith.ttf@/fonts/sexsmith.ttf"
    "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets/stopwords@/stopwords"
    "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets/sample-text.txt@/sample-text.txt"
    # FS + run-dependency methods let the page's preRun inject lazily
    # fetched ?corpus=/?font= files into MEMFS before main() runs.
    "SHELL:-sEXPORTED_RUNTIME_METHODS=stringToNewUTF8,FS,addRunDependency,removeRunDependency"
  )
  set_property(TARGET words APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/assets/fonts/sexsmith.ttf)

  # Debug builds carry DWARF + source map so Chrome DevTools can show C++
  # source.
  target_compile_options(words PRIVATE $<$<CONFIG:Debug>:-g>)
  target_link_options(words PRIVATE
    $<$<CONFIG:Debug>:-g>
    "$<$<CONFIG:Debug>:SHELL:-gsource-map --source-map-base http://localhost:8787/>"
  )

  # Static web harness: copied into dist/ whenever a file in web/ changes,
  # so a plain `./dev` + browser reload picks up HTML/CSS edits too.
  file(GLOB WORDS_WEB_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/web/*)
  set(WORDS_WEB_STAMP ${CMAKE_BINARY_DIR}/web-assets.stamp)
  add_custom_command(
    OUTPUT ${WORDS_WEB_STAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${WORDS_DIST_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${WORDS_WEB_FILES} ${WORDS_DIST_DIR}
    COMMAND ${CMAKE_COMMAND} -E touch ${WORDS_WEB_STAMP}
    DEPENDS ${WORDS_WEB_FILES}
    COMMENT "Copying web/ assets to dist/"
  )
  add_custom_target(web_assets ALL DEPENDS ${WORDS_WEB_STAMP})
  add_dependencies(words web_assets)

  # Lazily fetched resources (?corpus=, ?font=): plain static files next to
  # the app, not part of the preloaded bundle — the browser only downloads
  # what a URL actually asks for.
  file(GLOB WORDS_CORPUS_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/corpus/*.tsv)
  file(GLOB WORDS_FONT_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/fonts/*.ttf)
  set(WORDS_LAZY_STAMP ${CMAKE_BINARY_DIR}/lazy-assets.stamp)
  add_custom_command(
    OUTPUT ${WORDS_LAZY_STAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${WORDS_DIST_DIR}/corpus ${WORDS_DIST_DIR}/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${WORDS_CORPUS_FILES} ${WORDS_DIST_DIR}/corpus
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${WORDS_FONT_FILES} ${WORDS_DIST_DIR}/fonts
    COMMAND ${CMAKE_COMMAND} -E touch ${WORDS_LAZY_STAMP}
    DEPENDS ${WORDS_CORPUS_FILES} ${WORDS_FONT_FILES}
    COMMENT "Copying corpus/ and fonts/ to dist/"
  )
  add_custom_target(lazy_assets ALL DEPENDS ${WORDS_LAZY_STAMP})
  add_dependencies(words lazy_assets)

  # wasm runtime microbenchmarks: Google Benchmark compiled to wasm, run
  # under node (same V8 as Chrome, so codegen is representative of the
  # browser). Built in both configs so the dev loop compiles it and clangd
  # has a compile command for bench/; tools/bench.sh only ever *runs* the
  # release binary — debug benchmark numbers measure nothing.
  find_package(benchmark CONFIG REQUIRED)
  add_executable(words_bench bench/word_bench.cc)
  target_link_libraries(words_bench PRIVATE words_core benchmark::benchmark)
  target_link_options(words_bench PRIVATE
    -sENVIRONMENT=node
    -sNODERAWFS=1
    -sALLOW_MEMORY_GROWTH=1
    -sEXIT_RUNTIME=1
  )
else()
  # Native host build: geometry tests (Catch2 + ApprovalTests golden files).
  enable_testing()
  find_package(Catch2 CONFIG REQUIRED)
  # approval-tests-cpp is header-only and ships no CMake config; vcpkg puts
  # ApprovalTests.hpp on the toolchain include path.

  add_executable(words_tests
    tests/geometry_approval_test.cc
    tests/palette_test.cc
    tests/test_main.cc
    tests/text_test.cc
  )
  target_link_libraries(words_tests PRIVATE
    words_core
    Catch2::Catch2WithMain
  )
  target_compile_definitions(words_tests PRIVATE
    WORDS_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
  )

  include(CTest)
  include(Catch)
  catch_discover_tests(words_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Corpus-building CLI: counts a text file's words with the app's exact
  # word model. Driven by tools/make-corpus.py.
  add_executable(word_counts tools/word_counts_main.cc)
  target_link_libraries(word_counts PRIVATE words_core)
endif()
