cmake_minimum_required(VERSION 3.25)
project(wrdl LANGUAGES CXX)

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "wrdl targets WebAssembly only. Configure with a preset "
                      "from CMakePresets.json (e.g. `cmake --preset wasm-debug`) "
                      "or just run `./dev`.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(wrdl
  src/main.cpp
)

# Everything the browser loads ends up in ${CMAKE_BINARY_DIR}/dist.
set(WRDL_DIST_DIR ${CMAKE_BINARY_DIR}/dist)
set_target_properties(wrdl PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${WRDL_DIST_DIR})

target_link_options(wrdl PRIVATE
  -sENVIRONMENT=web
  -sMIN_WEBGL_VERSION=2
  -sMAX_WEBGL_VERSION=2
  -sALLOW_MEMORY_GROWTH=1
  -sMODULARIZE=1
  -sEXPORT_NAME=createWrdlModule
)

# Debug builds carry DWARF + source map so Chrome DevTools can show C++ source.
target_compile_options(wrdl PRIVATE $<$<CONFIG:Debug>:-g>)
target_link_options(wrdl PRIVATE
  $<$<CONFIG:Debug>:-g>
  "$<$<CONFIG:Debug>:SHELL:-gsource-map --source-map-base http://localhost:8787/>"
)

# Static web harness: copied into dist/ whenever a file in web/ changes,
# so a plain `./dev` + browser reload picks up HTML/CSS edits too.
file(GLOB WRDL_WEB_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/web/*)
set(WRDL_WEB_STAMP ${CMAKE_BINARY_DIR}/web-assets.stamp)
add_custom_command(
  OUTPUT ${WRDL_WEB_STAMP}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${WRDL_DIST_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${WRDL_WEB_FILES} ${WRDL_DIST_DIR}
  COMMAND ${CMAKE_COMMAND} -E touch ${WRDL_WEB_STAMP}
  DEPENDS ${WRDL_WEB_FILES}
  COMMENT "Copying web/ assets to dist/"
)
add_custom_target(web_assets ALL DEPENDS ${WRDL_WEB_STAMP})
add_dependencies(wrdl web_assets)
