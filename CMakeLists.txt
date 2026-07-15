cmake_minimum_required(VERSION 3.25)
project(words LANGUAGES CXX)

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "words targets WebAssembly only. Configure with a preset "
                      "from CMakePresets.json (e.g. `cmake --preset wasm-debug`) "
                      "or just run `./dev`.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Freetype REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)
find_package(Clipper2 CONFIG REQUIRED)

add_executable(words
  src/gl_util.cc
  src/main.cc
  src/scene.cc
  src/word.cc
  src/word_renderer.cc
)
target_link_libraries(words PRIVATE
  Freetype::Freetype
  harfbuzz::harfbuzz
  Clipper2::Clipper2
)

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
  "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets/Sexsmith.otf@/fonts/Sexsmith.otf"
)
set_property(TARGET words APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/assets/Sexsmith.otf)

# Debug builds carry DWARF + source map so Chrome DevTools can show C++ source.
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
