#include "word.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <hb.h>

#include <cmath>
#include <cstdio>

namespace words {

namespace {

// Collects one glyph's outline as flattened contours, y-up, offset by the
// pen position. Everything stays in integer font units (FT_LOAD_NO_SCALE):
// shaping, flattening, and booleans all happen in the font's own coordinate
// space, and transforms to other spaces are applied downstream.
struct OutlineSink {
  Clipper2Lib::PathsD contours;
  double penX = 0, penY = 0;
  Clipper2Lib::PointD current{0, 0};

  static constexpr int kConicSteps = 8;
  static constexpr int kCubicSteps = 12;

  Clipper2Lib::PointD toUnits(const FT_Vector* v) const {
    return {penX + static_cast<double>(v->x),
            penY + static_cast<double>(v->y)};
  }

  static int moveTo(const FT_Vector* to, void* user) {
    auto* self = static_cast<OutlineSink*>(user);
    self->contours.emplace_back();
    self->current = self->toUnits(to);
    self->contours.back().push_back(self->current);
    return 0;
  }

  static int lineTo(const FT_Vector* to, void* user) {
    auto* self = static_cast<OutlineSink*>(user);
    self->current = self->toUnits(to);
    self->contours.back().push_back(self->current);
    return 0;
  }

  static int conicTo(const FT_Vector* c, const FT_Vector* to, void* user) {
    auto* self = static_cast<OutlineSink*>(user);
    auto p0 = self->current, p1 = self->toUnits(c), p2 = self->toUnits(to);
    for (int i = 1; i <= kConicSteps; ++i) {
      double t = static_cast<double>(i) / kConicSteps, u = 1.0 - t;
      self->contours.back().push_back(
          {u * u * p0.x + 2 * u * t * p1.x + t * t * p2.x,
           u * u * p0.y + 2 * u * t * p1.y + t * t * p2.y});
    }
    self->current = p2;
    return 0;
  }

  static int cubicTo(const FT_Vector* c1, const FT_Vector* c2,
                     const FT_Vector* to, void* user) {
    auto* self = static_cast<OutlineSink*>(user);
    auto p0 = self->current, p1 = self->toUnits(c1), p2 = self->toUnits(c2),
         p3 = self->toUnits(to);
    for (int i = 1; i <= kCubicSteps; ++i) {
      double t = static_cast<double>(i) / kCubicSteps, u = 1.0 - t;
      double a = u * u * u, b = 3 * u * u * t, cc = 3 * u * t * t,
             d = t * t * t;
      self->contours.back().push_back(
          {a * p0.x + b * p1.x + cc * p2.x + d * p3.x,
           a * p0.y + b * p1.y + cc * p2.y + d * p3.y});
    }
    self->current = p3;
    return 0;
  }
};

Box boundsOf(const Clipper2Lib::PathsD& paths) {
  Box b;
  bool first = true;
  for (const auto& path : paths) {
    for (const auto& p : path) {
      if (first) {
        b = {p.x, p.y, p.x, p.y};
        first = false;
      } else {
        b.minX = std::min(b.minX, p.x);
        b.maxX = std::max(b.maxX, p.x);
        b.minY = std::min(b.minY, p.y);
        b.maxY = std::max(b.maxY, p.y);
      }
    }
  }
  return b;
}

}  // namespace

ShapedText shapeText(const std::string& fontPath, const std::string& text) {
  FT_Library library = nullptr;
  if (FT_Init_FreeType(&library) != 0) {
    std::printf("FT_Init_FreeType failed\n");
    return {};
  }
  FT_Face face = nullptr;
  if (FT_New_Face(library, fontPath.c_str(), 0, &face) != 0) {
    std::printf("FT_New_Face failed — font missing at %s?\n",
                fontPath.c_str());
    FT_Done_FreeType(library);
    return {};
  }

  hb_blob_t* blob = hb_blob_create_from_file_or_fail(fontPath.c_str());
  if (!blob) {
    std::printf("hb_blob_create_from_file_or_fail failed\n");
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return {};
  }
  hb_face_t* hbFace = hb_face_create(blob, 0);
  hb_blob_destroy(blob);
  hb_font_t* hbFont = hb_font_create(hbFace);
  unsigned int upem = hb_face_get_upem(hbFace);
  // Shaping at scale == upem makes HarfBuzz positions exact integer font
  // units, matching FT_LOAD_NO_SCALE outlines: no quantization anywhere.
  hb_font_set_scale(hbFont, static_cast<int>(upem), static_cast<int>(upem));

  hb_buffer_t* buffer = hb_buffer_create();
  hb_buffer_add_utf8(buffer, text.c_str(), -1, 0, -1);
  hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
  hb_buffer_set_script(buffer, HB_SCRIPT_LATIN);
  hb_buffer_set_language(buffer, hb_language_from_string("en", -1));
  hb_shape(hbFont, buffer, nullptr, 0);

  unsigned int glyphCount = 0;
  hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &glyphCount);
  hb_glyph_position_t* positions =
      hb_buffer_get_glyph_positions(buffer, &glyphCount);

  OutlineSink sink;
  FT_Outline_Funcs funcs = {};
  funcs.move_to = OutlineSink::moveTo;
  funcs.line_to = OutlineSink::lineTo;
  funcs.conic_to = OutlineSink::conicTo;
  funcs.cubic_to = OutlineSink::cubicTo;

  double penX = 0, penY = 0;
  for (unsigned int i = 0; i < glyphCount; ++i) {
    if (FT_Load_Glyph(face, infos[i].codepoint,
                      FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP) != 0) {
      continue;
    }
    sink.penX = penX + positions[i].x_offset;
    sink.penY = penY + positions[i].y_offset;
    FT_Outline_Decompose(&face->glyph->outline, &funcs, &sink);
    penX += positions[i].x_advance;
    penY += positions[i].y_advance;
  }

  hb_buffer_destroy(buffer);
  hb_font_destroy(hbFont);
  hb_face_destroy(hbFace);
  FT_Done_Face(face);
  FT_Done_FreeType(library);

  ShapedText result;
  // Union produces disjoint contours (holes as separate paths), which both
  // the even-odd stencil fill and downstream boolean ops rely on.
  result.paths =
      Clipper2Lib::Union(sink.contours, Clipper2Lib::FillRule::NonZero);
  result.bounds = boundsOf(result.paths);
  std::printf("shaped \"%s\": %u glyphs (upem %u), %zu raw → %zu contours\n",
              text.c_str(), glyphCount, upem, sink.contours.size(),
              result.paths.size());
  return result;
}

Word::Word(const ShapedText& text, double scale, double angleRad)
    : scale_(scale), angleRad_(angleRad) {
  double cx = text.bounds.centerX();
  double cy = text.bounds.centerY();
  double c = std::cos(angleRad), s = std::sin(angleRad);
  for (const auto& path : text.paths) {
    Clipper2Lib::PathD out;
    out.reserve(path.size());
    for (const auto& q : path) {
      double x = q.x - cx, y = q.y - cy;
      out.push_back({scale * (c * x - s * y), scale * (s * x + c * y)});
    }
    localPaths_.push_back(std::move(out));
  }
  localBounds_ = boundsOf(localPaths_);
}

bool Word::boxIntersects(const Clipper2Lib::PathsD& poly) const {
  Clipper2Lib::PathsD box{worldBounds().asPath()};
  return !Clipper2Lib::Intersect(poly, box, Clipper2Lib::FillRule::NonZero)
              .empty();
}

}  // namespace words
