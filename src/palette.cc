#include "palette.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <random>
#include <string_view>
#include <vector>

#include "scene.h"

namespace words {

namespace {

// java.awt.Color works in 8-bit channels; reproducing its exact rounding
// keeps our varied colors identical to the original's.
int channel(float f) { return static_cast<int>(f * 255.0f + 0.5f); }

struct Hsb {
  float h, s, b;
};

// java.awt.Color.RGBtoHSB.
Hsb rgbToHsb(int r, int g, int b) {
  int cmax = std::max({r, g, b});
  int cmin = std::min({r, g, b});
  float brightness = cmax / 255.0f;
  float saturation =
      cmax != 0 ? static_cast<float>(cmax - cmin) / cmax : 0.0f;
  float hue = 0.0f;
  if (saturation != 0.0f) {
    float redc = static_cast<float>(cmax - r) / (cmax - cmin);
    float greenc = static_cast<float>(cmax - g) / (cmax - cmin);
    float bluec = static_cast<float>(cmax - b) / (cmax - cmin);
    if (r == cmax) {
      hue = bluec - greenc;
    } else if (g == cmax) {
      hue = 2.0f + redc - bluec;
    } else {
      hue = 4.0f + greenc - redc;
    }
    hue /= 6.0f;
    if (hue < 0.0f) hue += 1.0f;
  }
  return {hue, saturation, brightness};
}

// java.awt.Color.getHSBColor / HSBtoRGB.
Color hsbToColor(float hue, float saturation, float brightness) {
  int r = 0, g = 0, b = 0;
  if (saturation == 0.0f) {
    r = g = b = channel(brightness);
  } else {
    float h = (hue - std::floor(hue)) * 6.0f;
    float f = h - std::floor(h);
    float p = brightness * (1.0f - saturation);
    float q = brightness * (1.0f - saturation * f);
    float t = brightness * (1.0f - saturation * (1.0f - f));
    switch (static_cast<int>(h)) {
      case 0: r = channel(brightness); g = channel(t); b = channel(p); break;
      case 1: r = channel(q); g = channel(brightness); b = channel(p); break;
      case 2: r = channel(p); g = channel(brightness); b = channel(t); break;
      case 3: r = channel(p); g = channel(q); b = channel(brightness); break;
      case 4: r = channel(t); g = channel(p); b = channel(brightness); break;
      case 5: r = channel(brightness); g = channel(p); b = channel(q); break;
    }
  }
  return {r / 255.0f, g / 255.0f, b / 255.0f};
}

double vary(double value, double variance, std::mt19937& rng) {
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  return std::clamp(value - variance / 2 + variance * unit(rng), 0.0, 1.0);
}

Palette makePalette(unsigned bg, std::initializer_list<unsigned> colors) {
  Palette p;
  p.background = colorFromHex(bg);
  double weight = 1.0 / colors.size();
  for (unsigned c : colors) p.colors.push_back({colorFromHex(c), weight});
  return p;
}

}  // namespace

const Color& Palette::pick(std::mt19937& rng) const {
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  double d = unit(rng);
  double c = 0;
  for (size_t i = 0; i + 1 < colors.size(); ++i) {
    c += colors[i].weight;
    if (d <= c) return colors[i].color;
  }
  return colors.back().color;
}

Color colorFromHex(unsigned rgb) {
  return {((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f,
          (rgb & 0xff) / 255.0f};
}

Color varied(const Color& c, double variance, std::mt19937& rng) {
  Hsb hsb = rgbToHsb(channel(c.r), channel(c.g), channel(c.b));
  // Achromatic colors (and pure reds — both have HSB hue 0) have no hue
  // to vary; their brightness varies instead. This is what makes BW,
  // Ghostly, and Heat vary in tone rather than drift into rainbow.
  if (hsb.h < 0.01f) {
    return hsbToColor(0.0f, hsb.s, static_cast<float>(vary(hsb.b, variance, rng)));
  }
  return hsbToColor(static_cast<float>(vary(hsb.h, variance, rng)), hsb.s,
                    hsb.b);
}

const std::vector<NamedPalette>& builtinPalettes() {
  // The original PaletteManager's table, in menu order.
  static const std::vector<NamedPalette> kPalettes = {
      {"bw", makePalette(0xffffff, {0x000000})},
      {"wb", makePalette(0x000000, {0xffffff})},
      {"wordly",
       makePalette(0xffffff, {0x880099, 0x339922, 0x993333, 0x2266CC})},
      {"asparagus",
       makePalette(0xffffff, {0xCCFFCC, 0xCCCC7E, 0x727E4C, 0xB1BD35})},
      {"bluesugar",
       makePalette(0xffffff, {0xFFCC66, 0x999999, 0x9999CC, 0xCCCCFF})},
      {"heat", makePalette(0xffffff, {0xCCCCCC, 0x996666, 0x660000,
                                      0x330000, 0x666666})},
      {"ghostly",
       makePalette(0xffffff, {0x000000, 0x333333, 0x666666, 0x888888})},
      {"chilled-summer", makePalette(0x000000, {0x8DC3F2, 0xCBE4F8,
                                                0xF2F2F2, 0x8CBF1F,
                                                0x7AA61B})},
      {"blue-meets-orange", makePalette(0x000000, {0x023059, 0x3F7EA6,
                                                   0xF2F2F2, 0xD99E32,
                                                   0xBF5E0A})},
      {"yramirp", makePalette(0x000000, {0xff0000, 0x00ff00, 0x0000ff})},
  };
  return kPalettes;
}

const Palette* findPalette(std::string_view name) {
  for (const NamedPalette& np : builtinPalettes()) {
    if (name == np.name) return &np.palette;
  }
  return nullptr;
}

std::optional<double> findVariance(std::string_view name) {
  // "Exact Palette Colors" .. "Wild Variance", by their operative word.
  if (name == "exact") return 0.0;
  if (name == "little") return 0.08;
  if (name == "some") return 0.12;
  if (name == "lots") return 0.25;
  if (name == "wild") return 0.5;
  return std::nullopt;
}

}  // namespace words
