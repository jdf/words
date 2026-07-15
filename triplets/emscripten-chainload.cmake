# Chainload toolchain for wasm32-emscripten port builds: Emscripten's own
# toolchain plus project-required compile flags. The stock triplet chainloads
# Emscripten.cmake directly, which bypasses vcpkg's VCPKG_C(XX)_FLAGS
# plumbing entirely — so flags ports need must be appended here.
include("$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")

# HarfBuzz promotes a curated warning list to errors via pragmas in hb.hh;
# emsdk's clang 23 newly fires -Wunused-template inside hb-algs.hh, breaking
# the build. Disable the error pragmas (a no-op for every other port).
string(APPEND CMAKE_C_FLAGS_INIT " -DHB_NO_PRAGMA_GCC_DIAGNOSTIC_ERROR")
string(APPEND CMAKE_CXX_FLAGS_INIT " -DHB_NO_PRAGMA_GCC_DIAGNOSTIC_ERROR")
