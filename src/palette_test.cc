// Logic tests for the palette port: registry, weighted picking, and the
// structural invariants of the variance rule. Actual color appearance is
// verified by the GL e2e goldens (tools/e2e-golden.sh), which pin every
// pixel of palette-colored clouds through the real renderer.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <string>

#include "palette.h"
#include "scene.h"

namespace {

bool sameColor(const words::Color& a, const words::Color& b) {
  return a.r == b.r && a.g == b.g && a.b == b.b;
}

}  // namespace

TEST_CASE("builtin palettes match the original menu") {
  const auto& palettes = words::builtinPalettes();
  REQUIRE(palettes.size() == 10);
  CHECK(palettes.front().name == std::string("bw"));
  CHECK(palettes.back().name == std::string("yramirp"));

  const words::NamedPalette* wordly = words::findPalette("wordly");
  REQUIRE(wordly != nullptr);
  CHECK(wordly->displayName == std::string("Wordly"));
  CHECK(wordly->palette.colors.size() == 4);
  CHECK(sameColor(wordly->palette.background, words::colorFromHex(0xffffff)));
  CHECK(sameColor(wordly->palette.colors[0].color,
                  words::colorFromHex(0x880099)));

  CHECK(words::findPalette("no-such-palette") == nullptr);
  CHECK(words::findPalette("") == nullptr);
}

TEST_CASE("custom palettes parse from the page's serialization") {
  auto p = words::parseCustomPalette("custom:101014:FF0000,00ff00,0000FF");
  REQUIRE(p.has_value());
  CHECK(sameColor(p->background, words::colorFromHex(0x101014)));
  REQUIRE(p->colors.size() == 3);
  CHECK(sameColor(p->colors[0].color, words::colorFromHex(0xff0000)));
  CHECK(sameColor(p->colors[1].color, words::colorFromHex(0x00ff00)));
  CHECK(sameColor(p->colors[2].color, words::colorFromHex(0x0000ff)));
  for (const auto& wc : p->colors) CHECK(wc.weight == 1.0 / 3);

  auto single = words::parseCustomPalette("custom:000000:ffffff");
  REQUIRE(single.has_value());
  REQUIRE(single->colors.size() == 1);
  CHECK(single->colors[0].weight == 1.0);
}

TEST_CASE("malformed custom palettes are rejected whole") {
  CHECK_FALSE(words::parseCustomPalette("").has_value());
  CHECK_FALSE(words::parseCustomPalette("wordly").has_value());
  // No word-color section, or an empty one.
  CHECK_FALSE(words::parseCustomPalette("custom:000000").has_value());
  CHECK_FALSE(words::parseCustomPalette("custom:000000:").has_value());
  // Components must be exactly six hex digits.
  CHECK_FALSE(words::parseCustomPalette("custom:000000:ff00").has_value());
  CHECK_FALSE(words::parseCustomPalette("custom:000000:ff000000").has_value());
  CHECK_FALSE(words::parseCustomPalette("custom:#00000:ffffff").has_value());
  CHECK_FALSE(words::parseCustomPalette("custom:000000:gg0000").has_value());
  // Stray separators.
  CHECK_FALSE(words::parseCustomPalette("custom:000000:ff0000,").has_value());
  CHECK_FALSE(
      words::parseCustomPalette("custom:000000:ff0000,,00ff00").has_value());
}

TEST_CASE("variance levels match the original menu") {
  CHECK(words::findVariance("exact") == 0.0);
  CHECK(words::findVariance("little") == 0.08);
  CHECK(words::findVariance("some") == 0.12);
  CHECK(words::findVariance("lots") == 0.25);
  CHECK(words::findVariance("wild") == 0.5);
  CHECK_FALSE(words::findVariance("extreme").has_value());
  CHECK(words::kDefaultVariance == 0.08);
}

TEST_CASE("weighted pick walks the cumulative distribution") {
  // A lopsided palette: the heavy color must dominate draws.
  words::Palette p{words::colorFromHex(0x000000),
                   {{words::colorFromHex(0xff0000), 0.9},
                    {words::colorFromHex(0x0000ff), 0.1}}};
  std::mt19937 rng(1);
  int red = 0;
  for (int i = 0; i < 1000; ++i) {
    if (p.pick(rng).r == 1.0f) ++red;
  }
  CHECK(red > 850);
  CHECK(red < 950);
}

TEST_CASE("zero variance returns 8-bit colors unchanged") {
  // The original always round-trips RGB->HSB->RGB; with java.awt.Color's
  // quantization that trip is exact for 8-bit inputs, so "Exact Palette
  // Colors" really is exact.
  std::mt19937 rng(2);
  for (const auto& np : words::builtinPalettes()) {
    for (const auto& wc : np.palette.colors) {
      words::Color out = words::varied(wc.color, 0.0, rng);
      INFO(np.name);
      CHECK(sameColor(out, wc.color));
    }
  }
}

TEST_CASE("achromatic and pure-red colors vary in brightness, not hue") {
  std::mt19937 rng(3);
  for (int i = 0; i < 200; ++i) {
    // Pure red has HSB hue 0: it must stay pure red at any brightness.
    words::Color red = words::varied(words::colorFromHex(0xff0000), 0.5, rng);
    CHECK(red.g == 0.0f);
    CHECK(red.b == 0.0f);
    // Grays have no hue or saturation: they must stay gray.
    words::Color gray = words::varied(words::colorFromHex(0x888888), 0.5, rng);
    CHECK(gray.r == gray.g);
    CHECK(gray.g == gray.b);
  }
}

TEST_CASE("chromatic colors vary in hue, keeping tone") {
  // A hue rotation preserves HSB saturation and brightness; for a fully
  // saturated color that means one channel stays at the maximum and the
  // minimum stays at zero.
  std::mt19937 rng(4);
  bool moved = false;
  for (int i = 0; i < 200; ++i) {
    words::Color c = words::varied(words::colorFromHex(0x00ff00), 0.5, rng);
    float mx = std::max({c.r, c.g, c.b});
    float mn = std::min({c.r, c.g, c.b});
    CHECK(mx == 1.0f);
    CHECK(mn == 0.0f);
    if (!sameColor(c, words::colorFromHex(0x00ff00))) moved = true;
  }
  CHECK(moved);
}
