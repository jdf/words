#include "pdf.h"

#include <zlib.h>

#include <clipper2/clipper.h>

#include <cstdio>
#include <string>
#include <vector>

#include "scene.h"

namespace words {

namespace {

std::string num(double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.2f", v);
  if (std::string(buf) == "-0.00") return "0.00";
  return buf;
}

std::string rgb(const Color& c) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.3f %.3f %.3f rg", c.r, c.g, c.b);
  return buf;
}

// The page content: background, then every word's contours in scene
// coordinates under a scale+translate matrix. PDF is y-up, like the
// scene — no flip anywhere.
std::string contentOps(const Scene& scene, double pageW, double pageH) {
  std::string ops;
  ops += "q\n";
  ops += rgb(scene.background()) + "\n";
  ops += "0 0 " + num(pageW) + " " + num(pageH) + " re f\n";

  double s = pageW / scene.width();
  ops += num(s) + " 0 0 " + num(s) + " " + num(pageW / 2) + " " +
         num(pageH / 2) + " cm\n";

  for (const Scene::Entry& e : scene.entries()) {
    ops += rgb(e.color) + "\n";
    for (const auto& path : e.word.localPaths()) {
      for (size_t i = 0; i < path.size(); ++i) {
        ops += num(path[i].x + e.word.x()) + " " +
               num(path[i].y + e.word.y()) + (i == 0 ? " m\n" : " l\n");
      }
      ops += "h\n";
    }
    ops += "f*\n";  // even-odd fill, the renderer's rule
  }
  ops += "Q\n";
  return ops;
}

std::string deflate(const std::string& data) {
  uLongf destLen = compressBound(static_cast<uLong>(data.size()));
  std::vector<Bytef> out(destLen);
  if (compress2(out.data(), &destLen,
                reinterpret_cast<const Bytef*>(data.data()),
                static_cast<uLong>(data.size()), Z_BEST_COMPRESSION) != Z_OK) {
    return data;  // uncompressed fallback; caller adjusts the filter
  }
  return std::string(reinterpret_cast<char*>(out.data()), destLen);
}

}  // namespace

std::string toPdf(const Scene& scene, double pointWidth) {
  double pageW = pointWidth;
  double pageH = scene.width() > 0
                     ? pointWidth * scene.height() / scene.width()
                     : pointWidth;

  std::string ops = contentOps(scene, pageW, pageH);
  std::string packed = deflate(ops);
  bool compressed = packed.size() < ops.size();
  const std::string& streamData = compressed ? packed : ops;

  std::string pdf = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
  std::vector<size_t> offsets;  // 1-based object offsets
  auto beginObj = [&](int n) {
    offsets.push_back(pdf.size());
    pdf += std::to_string(n) + " 0 obj\n";
  };

  beginObj(1);
  pdf += "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
  beginObj(2);
  pdf += "<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n";
  beginObj(3);
  pdf += "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + num(pageW) + " " +
         num(pageH) + "] /Contents 4 0 R /Resources << >> >>\nendobj\n";
  beginObj(4);
  pdf += "<< /Length " + std::to_string(streamData.size());
  if (compressed) pdf += " /Filter /FlateDecode";
  pdf += " >>\nstream\n";
  pdf += streamData;
  pdf += "\nendstream\nendobj\n";

  size_t xref = pdf.size();
  pdf += "xref\n0 " + std::to_string(offsets.size() + 1) + "\n";
  pdf += "0000000000 65535 f \n";
  for (size_t off : offsets) {
    char line[24];
    std::snprintf(line, sizeof line, "%010zu 00000 n \n", off);
    pdf += line;
  }
  pdf += "trailer\n<< /Size " + std::to_string(offsets.size() + 1) +
         " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
  return pdf;
}

}  // namespace words
