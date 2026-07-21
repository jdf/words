#pragma once

#include <optional>
#include <random>
#include <string_view>
#include <vector>

#include "scene.h"

namespace words {

// Color palettes and per-word color variance, ported from the original
// Wordle's PaletteManager / RandomPalette / WeightedPalette /
// ColorVariance. Color math follows java.awt.Color's HSB conversions,
// quantized to 8 bits per channel like the original.

struct WeightedColor {
  Color color;
  double weight;
};

// A background plus weighted word colors. pick() draws a color with
// probability proportional to weight (the original's WeightedPalette walk;
// with equal weights it matches RandomPalette's uniform choice).
struct Palette {
  Color background;
  std::vector<WeightedColor> colors;

  const Color& pick(std::mt19937& rng) const;
};

// 0xRRGGBB, like the original's palette tables.
Color colorFromHex(unsigned rgb);

// The original's ColorVariance.tweak: perturb uniformly by ±variance/2,
// clamped to [0,1] — the hue for chromatic colors, the brightness for
// achromatic (or pure red) ones, i.e. HSB hue < 0.01. Draws exactly one
// uniform variate. The result is quantized to 8 bits per channel, so
// variance 0 returns 8-bit colors unchanged.
Color varied(const Color& c, double variance, std::mt19937& rng);

// A user-made palette serialized by the page (and carried in ?palette=):
// "custom:RRGGBB:RRGGBB,RRGGBB,..." — the background, then one or more
// equally weighted word colors. Returns nullopt unless the whole string
// parses (strict 6-digit hex components, no stray separators).
std::optional<Palette> parseCustomPalette(std::string_view spec);

// The built-in palettes, in the original menu's order.
struct NamedPalette {
  const char* name;         // lowercase slug: "wordly", "blue-meets-orange"
  const char* displayName;  // the original menu label: "Blue Meets Orange"
  Palette palette;
};
const std::vector<NamedPalette>& builtinPalettes();
const NamedPalette* findPalette(std::string_view name);

// The original's variance menu: exact 0, little 0.08 (the startup
// default), some 0.12, lots 0.25, wild 0.5.
inline constexpr double kDefaultVariance = 0.08;
std::optional<double> findVariance(std::string_view name);

}  // namespace words
