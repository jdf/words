// Golden-file tests over the scene's pure geometry, serialized as SVG.
// The approved files are viewable images: open tests/goldens/*.svg in a
// browser to see exactly what the geometry looks like.

#include <ApprovalTests.hpp>
#include <catch2/catch_test_macros.hpp>

#include "demo_scene.h"
#include "scene.h"
#include "svg.h"

namespace {

constexpr const char* kFont = WORDS_ASSETS_DIR "/Sexsmith.otf";

auto subdirectory = ApprovalTests::Approvals::useApprovalsSubdirectory(
    "goldens/geometry");

}  // namespace

TEST_CASE("demo scene geometry at t=0") {
  words::Scene scene = words::buildDemoScene(kFont);
  scene.update(0.0);
  ApprovalTests::Approvals::verify(
      words::toSvg(scene),
      ApprovalTests::Options().fileOptions().withFileExtension(".svg"));
}

TEST_CASE("demo scene geometry at t=2s") {
  words::Scene scene = words::buildDemoScene(kFont);
  scene.update(2.0);
  ApprovalTests::Approvals::verify(
      words::toSvg(scene),
      ApprovalTests::Options().fileOptions().withFileExtension(".svg"));
}
