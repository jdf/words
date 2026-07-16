#include "orientation.h"

#include <cstddef>
#include <numbers>
#include <optional>
#include <random>
#include <string_view>

namespace words {

namespace {

// Length in Unicode codepoints (the original compared Java string length;
// byte length would misjudge non-ASCII words).
size_t codepointCount(std::string_view utf8) {
  size_t n = 0;
  for (unsigned char c : utf8) {
    if ((c & 0xC0) != 0x80) ++n;
  }
  return n;
}

double horVert(double verticalProbability, std::mt19937& rng) {
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  return unit(rng) < verticalProbability ? std::numbers::pi / 2.0 : 0.0;
}

}  // namespace

double orientationAngle(Orientation orientation, std::string_view label,
                        std::mt19937& rng) {
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  switch (orientation) {
    case Orientation::kHorizontal:
      return horVert(0.0, rng);
    case Orientation::kMostlyHorizontal:
      return horVert(0.25, rng);
    case Orientation::kLongHorizontalLikely:
      if (codepointCount(label) > 6) return 0.0;
      return horVert(0.5, rng);
    case Orientation::kHalfAndHalf:
      return horVert(0.5, rng);
    case Orientation::kMostlyVertical:
      return horVert(0.75, rng);
    case Orientation::kVertical:
      return horVert(1.0, rng);
    case Orientation::kAnyWhichWay:
      return (unit(rng) - 0.5) * std::numbers::pi;
  }
  return 0.0;
}

std::string_view orientationName(Orientation orientation) {
  switch (orientation) {
    case Orientation::kHorizontal: return "Horizontal";
    case Orientation::kMostlyHorizontal: return "Mostly Horizontal";
    case Orientation::kLongHorizontalLikely: return "Long Horizontal Likely";
    case Orientation::kHalfAndHalf: return "Half And Half";
    case Orientation::kMostlyVertical: return "Mostly Vertical";
    case Orientation::kVertical: return "Vertical";
    case Orientation::kAnyWhichWay: return "Any Which Way";
  }
  return "?";
}

std::optional<Orientation> findOrientation(std::string_view name) {
  if (name == "horizontal") return Orientation::kHorizontal;
  if (name == "mostly-horizontal") return Orientation::kMostlyHorizontal;
  if (name == "long-horizontal-likely") {
    return Orientation::kLongHorizontalLikely;
  }
  if (name == "half-and-half") return Orientation::kHalfAndHalf;
  if (name == "mostly-vertical") return Orientation::kMostlyVertical;
  if (name == "vertical") return Orientation::kVertical;
  if (name == "any-which-way") return Orientation::kAnyWhichWay;
  return std::nullopt;
}

}  // namespace words
