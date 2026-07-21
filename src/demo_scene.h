#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "layout.h"
#include "orientation.h"
#include "palette.h"
#include "scene.h"
#include "text.h"

namespace words {

// How to color a cloud: a palette (word colors + background) and the
// original's per-word color variance. When none is given, the clouds use
// the app's built-in dark-scheme colors.
struct ColorScheme {
  Palette palette;
  double variance = kDefaultVariance;
};

// Knobs for the text->cloud pipeline. The defaults are the app's stock
// look: up to 800 words, one word in four vertical, center-line seeding,
// dark-scheme colors.
struct CloudOptions {
  size_t maxWords = 800;
  Orientation orientation = Orientation::kMostlyHorizontal;
  Placement placement = Placement::kCenterLine;
  // Layout randomness (seeding positions, spirals). 1447 is the curated
  // default: chosen by eye with the ?ui seed slider for how it spreads
  // the heaviest words. (Not every seed is equal — a seed whose first
  // uniform draws cluster near 1.0 pins the biggest words to the right
  // edge, since word k's starting x is the k-th draw.)
  uint32_t seed = 1447;
  // Recolor: nonzero redraws the per-word palette assignment from this
  // seed's own stream, leaving the layout untouched; zero keeps the
  // legacy colors (which interleave with angle draws — see
  // cloudFromCounts).
  uint32_t colorSeed = 0;
  // World width:height ratio — the canvas's, so portrait screens get
  // portrait clouds. 1.6 is the original's landscape default.
  double aspect = 1.6;
  // Spiral step multiplier (scales LayoutParams dTheta and dRadius
  // together): 1.0 is the original's calibration. Coarser steps loosen
  // packing and cut probe counts — a tuning knob for dense "mostly ink"
  // typefaces whose glyphs can't nest anyway.
  double spiralStep = 1.0;
  const ColorScheme* colors = nullptr;  // null = built-in dark scheme
  // The original's Case menu. Corpus TSVs are counted as-written (one
  // row per exact spelling), so every fold applies to them at load; on
  // a legacy case-merged TSV (no "# case:" header) kAsWritten is
  // unrecoverable and shows the stored majority casing.
  CaseFold caseFold = CaseFold::kGuess;
  // Words the user has removed from the cloud ("nuisance words"): any
  // count whose folded key matches drops out before sizing and the
  // maxWords cap, so the cloud refills behind it. Entries are folded
  // on the way in, so display forms are accepted too.
  std::vector<std::string> exclude;
  LayoutDebug* debug = nullptr;
  // Pipeline progress: phase is "shaping" or "layout"; (done, total)
  // within the phase. Called from whatever thread runs the pipeline.
  std::function<void(const char* phase, size_t done, size_t total)> progress;
};

// Builds the demo world: a word cloud of project-flavored vocabulary,
// weighted, colored from a small palette, mixed horizontal/vertical, laid
// out by the spiral+quadtree engine — plus the rotating-triangle prop that
// exercises the collision machinery. Deterministic for a fixed font.
Scene buildCloudScene(const std::string& fontPath);

// The real pipeline: analyzes unstructured text (tokenize, guess the
// language from the stop lists in `stopWordsDir`, drop its stop words,
// count), sizes the most frequent words linearly by count, and lays them
// out. Deterministic for fixed inputs.
Scene buildCloudFromText(const std::string& fontPath,
                         const std::string& stopWordsDir,
                         std::string_view text,
                         const CloudOptions& options = {});

// The same pipeline entered with precomputed counts: `tsv` is a
// tests/corpus file (word<TAB>count lines, most frequent first, stop
// words retained, `# language-guess:` header naming the stop list to
// apply). Deterministic for fixed inputs.
Scene buildCloudFromCountsTsv(const std::string& fontPath,
                              const std::string& stopWordsDir,
                              std::string_view tsv,
                              const CloudOptions& options = {});

// Reassigns the scene's word colors and background exactly as a full
// rebuild under `options` (orientation, colors, colorSeed) would,
// leaving the layout untouched — the recolor fast path for color-only
// spec changes. Exact only while the shared RNG stream keeps its shape:
// the scene must have been built with options.colors set iff it is set
// now (palette-colored builds draw twice per word where App Colors
// draws once, so crossing that line shifts the angle draws and a real
// rebuild would lay out differently). Callers enforce that; the engine
// falls back to a full rebuild when it doesn't hold.
void recolorScene(Scene& scene, const CloudOptions& options);

}  // namespace words
