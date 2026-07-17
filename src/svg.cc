#include "svg.h"

#include <clipper2/clipper.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "box.h"
#include "layout.h"
#include "scene.h"
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

std::string toSvg(const Scene& scene, bool background,
                  const std::string& generator) {
  std::string svg;
  char buf[256];
  std::snprintf(buf, sizeof buf,
                "<svg xmlns=\"http://www.w3.org/2000/svg\" "
                "viewBox=\"%d %d %d %d\">\n",
                static_cast<int>(-scene.width() / 2),
                static_cast<int>(-scene.height() / 2),
                static_cast<int>(scene.width()),
                static_cast<int>(scene.height()));
  svg += buf;
  if (!generator.empty()) {
    svg += "<desc>" + generator + "</desc>\n";
  }
  if (background) {
    std::snprintf(buf, sizeof buf,
                  "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" "
                  "fill=\"%s\"/>\n",
                  static_cast<int>(-scene.width() / 2),
                  static_cast<int>(-scene.height() / 2),
                  static_cast<int>(scene.width()),
                  static_cast<int>(scene.height()),
                  rgb(scene.background()).c_str());
    svg += buf;
  }
  // Scene coordinates are y-up; SVG is y-down.
  svg += "<g transform=\"scale(1,-1)\">\n";

  for (const Scene::Entry& e : scene.entries()) {
    svg += "<path fill=\"" + rgb(e.color) + "\" fill-rule=\"evenodd\" d=\"" +
           pathData(e.word.localPaths(), e.word.x(), e.word.y()) + "\"/>\n";
  }

  svg += "</g>\n</svg>\n";
  return svg;
}

std::string toSvg(const Scene& scene, const LayoutDebug& debug) {
  std::string svg = toSvg(scene);
  if (debug.trail.empty()) return svg;
  // Splice the overlay in before the closing tags (inside the y-flip).
  std::string overlay = "<polyline fill=\"none\" stroke=\"#4aa3ff\" "
                        "stroke-width=\"2.5\" stroke-opacity=\"0.9\" points=\"";
  for (const auto& p : debug.trail) {
    overlay += num(p.x) + "," + num(p.y) + " ";
  }
  overlay += "\"/>\n";
  const auto& first = debug.trail.front();
  const auto& last = debug.trail.back();
  overlay += "<circle fill=\"none\" stroke=\"#4aa3ff\" stroke-width=\"3\" r=\"8\" cx=\"" +
             num(first.x) + "\" cy=\"" + num(first.y) + "\"/>\n";
  overlay += "<circle fill=\"#4aa3ff\" r=\"6\" cx=\"" + num(last.x) +
             "\" cy=\"" + num(last.y) + "\"/>\n";
  size_t pos = svg.rfind("</g>");
  svg.insert(pos, overlay);
  return svg;
}

std::string hbbDebugSvg(const Word& word) {
  const Box& b = word.localBounds();
  double margin = std::max(b.width(), b.height()) * 0.15;
  std::string svg;
  char buf[256];
  std::snprintf(buf, sizeof buf,
                "<svg xmlns=\"http://www.w3.org/2000/svg\" "
                "viewBox=\"%s %s %s %s\">\n",
                num(b.minX - margin).c_str(), num(-b.maxY - margin).c_str(),
                num(b.width() + 2 * margin).c_str(),
                num(b.height() + 2 * margin).c_str());
  svg += buf;
  svg += "<rect x=\"" + num(b.minX - margin) + "\" y=\"" +
         num(-b.maxY - margin) + "\" width=\"" + num(b.width() + 2 * margin) +
         "\" height=\"" + num(b.height() + 2 * margin) +
         "\" fill=\"#17171c\"/>\n";
  svg += "<g transform=\"scale(1,-1)\">\n";
  svg += "<path fill=\"#8a8a96\" fill-rule=\"evenodd\" d=\"" +
         pathData(word.localPaths(), 0, 0) + "\"/>\n";
  // Swollen leaves first (the collision footprint, faint), then the raw
  // construction boxes (deflated by the swell) as crisp strokes — drawn
  // separately so padding overlap can't muddle the subdivision structure.
  double sh = word.hbb().swellH();
  double sv = word.hbb().swellV();
  word.hbb().visit([&svg](const Box& box, int depth, bool leaf) {
    (void)depth;
    if (!leaf) return;
    svg += "<rect fill=\"rgba(240,80,80,0.10)\" stroke=\"none\" x=\"" +
           num(box.minX) + "\" y=\"" + num(box.minY) + "\" width=\"" +
           num(box.width()) + "\" height=\"" + num(box.height()) +
           "\"/>\n";
  });
  word.hbb().visit([&svg, sh, sv](const Box& box, int depth, bool leaf) {
    Box raw{box.minX + sh, box.minY + sv, box.maxX - sh, box.maxY - sv};
    double strokeWidth = std::max(0.6, 6.0 / (1 << std::min(depth, 3)));
    svg += std::string("<rect fill=\"none\" stroke=\"") +
           (leaf ? "#f0a050" : "#f05050") + "\" stroke-width=\"" +
           num(strokeWidth) + "\" x=\"" + num(raw.minX) + "\" y=\"" +
           num(raw.minY) + "\" width=\"" + num(raw.width()) +
           "\" height=\"" + num(raw.height()) + "\"/>\n";
  });
  svg += "</g>\n</svg>\n";
  return svg;
}

}  // namespace words
