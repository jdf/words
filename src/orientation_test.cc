// Logic tests for the OrientationStrategy port: fixed strategies are
// fixed, probabilistic ones land near their probabilities, the long-word
// rule counts codepoints, and AnyWhichWay spans its range. What rotated
// clouds look like is pinned by the geometry and e2e goldens.

#include <catch2/catch_test_macros.hpp>

#include <numbers>
#include <random>

#include "orientation.h"

namespace {

constexpr double kVertical = std::numbers::pi / 2.0;

// Vertical draws out of `n` under a strategy, for a fixed label.
int verticalCount(words::Orientation o, const char* label, int n,
                  std::mt19937& rng) {
  int vertical = 0;
  for (int i = 0; i < n; ++i) {
    if (words::orientationAngle(o, label, rng) == kVertical) ++vertical;
  }
  return vertical;
}

}  // namespace

TEST_CASE("fixed orientations never waver") {
  std::mt19937 rng(1);
  CHECK(verticalCount(words::Orientation::kHorizontal, "word", 500, rng) ==
        0);
  CHECK(verticalCount(words::Orientation::kVertical, "word", 500, rng) ==
        500);
}

TEST_CASE("probabilistic orientations match the original's mix") {
  std::mt19937 rng(2);
  int mostlyH =
      verticalCount(words::Orientation::kMostlyHorizontal, "word", 4000, rng);
  CHECK(mostlyH > 850);
  CHECK(mostlyH < 1150);  // ~25%

  int half =
      verticalCount(words::Orientation::kHalfAndHalf, "word", 4000, rng);
  CHECK(half > 1850);
  CHECK(half < 2150);  // ~50%

  int mostlyV =
      verticalCount(words::Orientation::kMostlyVertical, "word", 4000, rng);
  CHECK(mostlyV > 2850);
  CHECK(mostlyV < 3150);  // ~75%
}

TEST_CASE("long words lie down") {
  std::mt19937 rng(3);
  // 7+ codepoints: always horizontal, no coin flip.
  CHECK(verticalCount(words::Orientation::kLongHorizontalLikely, "longword",
                      500, rng) == 0);
  // "Настенька" is 9 codepoints but 18 UTF-8 bytes: still a long word.
  CHECK(verticalCount(words::Orientation::kLongHorizontalLikely,
                      "Настенька", 500, rng) == 0);
  // At 6 codepoints ("бабушка" is 7 — use "сердце") the 50/50 flip is on.
  int flips = verticalCount(words::Orientation::kLongHorizontalLikely,
                            "сердце", 4000, rng);
  CHECK(flips > 1850);
  CHECK(flips < 2150);
}

TEST_CASE("any which way spans the diagonals") {
  std::mt19937 rng(4);
  bool low = false, high = false;
  for (int i = 0; i < 2000; ++i) {
    double a =
        words::orientationAngle(words::Orientation::kAnyWhichWay, "w", rng);
    CHECK(a > -kVertical);
    CHECK(a < kVertical);
    if (a < -1.0) low = true;
    if (a > 1.0) high = true;
  }
  CHECK(low);
  CHECK(high);
}

TEST_CASE("orientation lookup by slug") {
  CHECK(words::findOrientation("horizontal") ==
        words::Orientation::kHorizontal);
  CHECK(words::findOrientation("mostly-horizontal") ==
        words::Orientation::kMostlyHorizontal);
  CHECK(words::findOrientation("long-horizontal-likely") ==
        words::Orientation::kLongHorizontalLikely);
  CHECK(words::findOrientation("half-and-half") ==
        words::Orientation::kHalfAndHalf);
  CHECK(words::findOrientation("mostly-vertical") ==
        words::Orientation::kMostlyVertical);
  CHECK(words::findOrientation("vertical") == words::Orientation::kVertical);
  CHECK(words::findOrientation("any-which-way") ==
        words::Orientation::kAnyWhichWay);
  CHECK_FALSE(words::findOrientation("sideways").has_value());
}
