// Smoke test for logging initialization on the host (the ConsoleSink
// browser path is wasm-only and exercised by every e2e run).

#include "logging.h"

#include <absl/log/log.h>
#include <catch2/catch_test_macros.hpp>

namespace {

TEST_CASE("initLogging enables logging without dying") {
  words::initLogging();
  LOG(INFO) << "logging_test: alive after initLogging";
  SUCCEED();
}

}  // namespace
