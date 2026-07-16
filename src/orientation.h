#pragma once

#include <optional>
#include <random>
#include <string_view>

namespace words {

// Word orientation strategies, ported from the original Wordle's
// OrientationStrategy. Angles are radians in the scene's y-up convention:
// 0 reads left-to-right, +pi/2 reads bottom-to-top (the original's
// vertical), and AnyWhichWay spans the diagonals in between.
enum class Orientation {
  kHorizontal,            // always 0
  kMostlyHorizontal,      // vertical 1 time in 4 (the app default)
  kLongHorizontalLikely,  // >6 letters always horizontal; else half and half
  kHalfAndHalf,
  kMostlyVertical,  // vertical 3 times in 4
  kVertical,        // always vertical
  kAnyWhichWay,     // uniform in (-pi/2, pi/2)
};

// The angle for one word. Draws at most one uniform variate from `rng`
// (exactly one, except LongHorizontalLikely's long-word short-circuit,
// like the original).
double orientationAngle(Orientation orientation, std::string_view label,
                        std::mt19937& rng);

// Lookup by slug: "horizontal", "mostly-horizontal",
// "long-horizontal-likely", "half-and-half", "mostly-vertical",
// "vertical", "any-which-way".
std::optional<Orientation> findOrientation(std::string_view name);

// Human-readable label, e.g. "Mostly Horizontal".
std::string_view orientationName(Orientation orientation);

}  // namespace words
