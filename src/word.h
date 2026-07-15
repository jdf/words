#pragma once

#include <clipper2/clipper.h>

#include <string>

namespace words {

// Axis-aligned bounding box.
struct Box {
  double minX = 0, minY = 0, maxX = 0, maxY = 0;

  double width() const { return maxX - minX; }
  double height() const { return maxY - minY; }
  double centerX() const { return (minX + maxX) / 2.0; }
  double centerY() const { return (minY + maxY) / 2.0; }

  Box translated(double dx, double dy) const {
    return {minX + dx, minY + dy, maxX + dx, maxY + dy};
  }

  // The box as a closed CCW polygon, for boolean ops against other geometry.
  Clipper2Lib::PathD asPath() const {
    return {{minX, minY}, {maxX, minY}, {maxX, maxY}, {minX, maxY}};
  }
};

// The outline geometry of a shaped piece of text, in integer font units
// (y-up, baseline at y=0): HarfBuzz-shaped, FreeType-decomposed, flattened,
// and merged with Clipper2 into disjoint contours (holes as separate paths)
// — ready for even-odd filling and boolean ops.
struct ShapedText {
  Clipper2Lib::PathsD paths;
  Box bounds;

  bool empty() const { return paths.empty(); }
};

// Shapes `text` with the font at `fontPath` (a filesystem path; under
// Emscripten, a file in the virtual FS). Returns empty geometry on failure.
ShapedText shapeText(const std::string& fontPath, const std::string& text);

// A rigid rendering of a word: scale and rotation are baked into word-local
// outline geometry centered on the word's own origin, and the root bounding
// box lives with the curves in that local frame (an axis-aligned box is only
// valid for one size and angle, so it is computed after the rotation is
// applied). Position is the single dynamic degree of freedom; moving a word
// translates curves and box together without touching the geometry. That is
// what makes layout-time movement cheap.
class Word {
 public:
  Word(const ShapedText& text, double scale, double angleRad);

  double scale() const { return scale_; }
  double angle() const { return angleRad_; }

  double x() const { return x_; }
  double y() const { return y_; }
  void moveTo(double x, double y) {
    x_ = x;
    y_ = y;
  }
  void moveBy(double dx, double dy) {
    x_ += dx;
    y_ += dy;
  }

  // Word-local geometry: scale+rotation applied, centered at the origin.
  const Clipper2Lib::PathsD& localPaths() const { return localPaths_; }
  const Box& localBounds() const { return localBounds_; }

  // The root box in world coordinates (local box translated by position).
  Box worldBounds() const { return localBounds_.translated(x_, y_); }

  // True if `poly` (world coordinates) overlaps this word's root box,
  // decided by Clipper2 on the actual geometry.
  bool boxIntersects(const Clipper2Lib::PathsD& poly) const;

 private:
  double scale_;
  double angleRad_;
  double x_ = 0, y_ = 0;
  Clipper2Lib::PathsD localPaths_;
  Box localBounds_;
};

}  // namespace words
