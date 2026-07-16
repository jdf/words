#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "layout.h"
#include "scene.h"

namespace words {

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
                         std::string_view text, size_t maxWords = 800,
                         LayoutDebug* debug = nullptr);

// The same pipeline entered with precomputed counts: `tsv` is a
// tests/corpus file (word<TAB>count lines, most frequent first, stop
// words retained, `# language-guess:` header naming the stop list to
// apply). Deterministic for fixed inputs.
Scene buildCloudFromCountsTsv(const std::string& fontPath,
                              const std::string& stopWordsDir,
                              std::string_view tsv, size_t maxWords = 800,
                              LayoutDebug* debug = nullptr);

}  // namespace words
