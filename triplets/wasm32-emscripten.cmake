# Project overlay of vcpkg's community wasm32-emscripten triplet. Identical
# except the chainload toolchain is our wrapper (emscripten-chainload.cmake),
# which layers required compile flags on top of Emscripten's toolchain —
# the stock triplet's direct chainload bypasses VCPKG_C(XX)_FLAGS entirely.
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED EMSCRIPTEN_ROOT EMSDK PATH)

if(NOT DEFINED ENV{EMSDK})
   message(FATAL_ERROR "The EMSDK environment variable must point at the emsdk root")
endif()

if(NOT EXISTS "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
   message(FATAL_ERROR "Emscripten.cmake toolchain file not found under $ENV{EMSDK}")
endif()

set(VCPKG_TARGET_ARCHITECTURE wasm32)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Emscripten)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/emscripten-chainload.cmake")
