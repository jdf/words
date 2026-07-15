#include "svg.h"

#include <clipper2/clipper.h>

#include <cstdio>
#include <string>

#include "scene.h"
#include "shape.h"
#include "word.h"

namespace words {

namespace {

// Fixed-precision number formatting: enough digits to be faithful, few
// enough to be stable and readable in diffs.
std::string num(double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.3f", v);
  // Normalize negative zero so -0.000 and 0.000 can't flip-flop.
  if (std::string(buf) == "-0.000") return "0.000";
  return buf;
}

std::string rgb(const Color& c) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "#%02x%02x%02x",
                static_cast<int>(c.r * 255.0f + 0.5f),
                static_cast<int>(c.g * 255.0f + 0.5f),
                static_cast<int>(c.b * 255.0f + 0.5f));
  return buf;
}

std::string pathData(const Clipper2Lib::PathsD& paths, double dx, double dy) {
  std::string d;
  for (const auto& path : paths) {
    for (size_t i = 0; i < path.size(); ++i) {
      d += (i == 0) ? "M" : "L";
      d += num(path[i].x + dx) + " " + num(path[i].y + dy);
    }
    d += "Z";
  }
  return d;
}

}  // namespace

std::string toSvg(const Scene& scene) {
  std::string svg;
  char buf[256];
  std::snprintf(buf, sizeof buf,
                "<svg xmlns=\"http://www.w3.org/2000/svg\" "
                "viewBox=\"%d %d %d %d\">\n",
                static_cast<int>(-Scene::kWidth / 2),
                static_cast<int>(-Scene::kHeight / 2),
                static_cast<int>(Scene::kWidth),
                static_cast<int>(Scene::kHeight));
  svg += buf;
  svg += "<rect x=\"-800\" y=\"-500\" width=\"1600\" height=\"1000\" "
         "fill=\"#17171c\"/>\n";
  // Scene coordinates are y-up; SVG is y-down.
  svg += "<g transform=\"scale(1,-1)\">\n";

  svg += "<path fill=\"#3a5f4a\" d=\"" +
         pathData(scene.shape().worldPath(), 0, 0) + "\"/>\n";

  for (const Scene::Entry& e : scene.entries()) {
    svg += "<path fill=\"" + rgb(e.color) + "\" fill-rule=\"evenodd\" d=\"" +
           pathData(e.word.localPaths(), e.word.x(), e.word.y()) + "\"/>\n";
    Box b = e.word.worldBounds();
    svg += "<rect fill=\"none\" stroke=\"" + rgb(e.color) +
           (e.hit ? "\" stroke-width=\"3\"" : "\" stroke-dasharray=\"8 8\"") +
           " x=\"" + num(b.minX) + "\" y=\"" + num(b.minY) + "\" width=\"" +
           num(b.width()) + "\" height=\"" + num(b.height()) + "\"/>\n";
  }

  svg += "</g>\n</svg>\n";
  return svg;
}

}  // namespace words
