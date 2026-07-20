#include "text.h"

#include <utf8proc.h>

#include <absl/container/flat_hash_map.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace words {

namespace {

// Decodes one UTF-8 code point at `i`; advances `i`. Invalid bytes decode
// as U+FFFD and advance by one, so bad input degrades instead of looping.
int32_t decodeAt(std::string_view s, size_t& i) {
  utf8proc_int32_t cp = -1;
  utf8proc_ssize_t len = utf8proc_iterate(
      reinterpret_cast<const utf8proc_uint8_t*>(s.data() + i),
      static_cast<utf8proc_ssize_t>(s.size() - i), &cp);
  if (len < 1 || cp < 0) {
    i += 1;
    return 0xFFFD;
  }
  i += static_cast<size_t>(len);
  return cp;
}

void encodeTo(int32_t cp, std::string& out) {
  utf8proc_uint8_t buf[4];
  utf8proc_ssize_t len = utf8proc_encode_char(cp, buf);
  out.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(len));
}

// Character.isLetterOrDigit semantics: Unicode letters (L*) and decimal
// digits (Nd) — plus cue.language's '@' and '+'.
bool isWordChar(int32_t cp) {
  if (cp == '@' || cp == '+') return true;
  switch (utf8proc_category(cp)) {
    case UTF8PROC_CATEGORY_LU:
    case UTF8PROC_CATEGORY_LL:
    case UTF8PROC_CATEGORY_LT:
    case UTF8PROC_CATEGORY_LM:
    case UTF8PROC_CATEGORY_LO:
    case UTF8PROC_CATEGORY_ND:
      return true;
    default:
      return false;
  }
}

// cue.language's joiners: characters that continue a word when letters
// follow (so "don't" and "U.S.A." hold together but a trailing "." drops).
bool isJoiner(int32_t cp) {
  switch (cp) {
    case '-':
    case '.':
    case ':':
    case '/':
    case '\'':
    case '~':
    case 0x2019:  // right single quote
    case 0x2032:  // prime
    case 0x00A0:  // no-break space
    case 0x200C:  // zero-width non-joiner
    case 0x200D:  // zero-width joiner
      return true;
    default:
      break;
  }
  switch (utf8proc_category(cp)) {
    case UTF8PROC_CATEGORY_MN:
    case UTF8PROC_CATEGORY_MC:
    case UTF8PROC_CATEGORY_ME:
      return true;
    default:
      return false;
  }
}

size_t codePointCount(std::string_view s) {
  size_t i = 0, n = 0;
  while (i < s.size()) {
    decodeAt(s, i);
    ++n;
  }
  return n;
}

}  // namespace

std::vector<std::string> wordsOf(std::string_view text) {
  std::vector<std::string> result;
  size_t i = 0;
  while (i < text.size()) {
    // Find the start of a word: a word char.
    size_t start = i;
    int32_t cp = decodeAt(text, i);
    if (!isWordChar(cp)) continue;

    // Extend: letters, then greedily accept joiner-runs that are followed
    // by another letter.
    size_t end = i;
    while (true) {
      // letters
      while (end < text.size()) {
        size_t next = end;
        if (!isWordChar(decodeAt(text, next))) break;
        end = next;
      }
      // joiner run + letter?
      size_t probe = end;
      bool sawJoiner = false;
      while (probe < text.size()) {
        size_t next = probe;
        if (!isJoiner(decodeAt(text, next))) break;
        probe = next;
        sawJoiner = true;
      }
      if (!sawJoiner || probe >= text.size()) break;
      size_t next = probe;
      if (!isWordChar(decodeAt(text, next))) break;
      end = next;  // consume joiners plus the letter; keep extending
    }
    result.emplace_back(text.substr(start, end - start));
    i = end;
  }
  return result;
}

std::string foldForMatch(std::string_view word) {
  std::string out;
  out.reserve(word.size());
  size_t i = 0;
  while (i < word.size()) {
    int32_t cp = decodeAt(word, i);
    if (cp == 0x2019) cp = '\'';
    // Full case mapping is contextual for Greek capital sigma: word-final
    // Σ lowercases to ς, not σ. utf8proc's tolower is per-code-point, so
    // apply the rule ourselves (the input here is always a single word).
    if (cp == 0x03A3 && i >= word.size()) {
      encodeTo(0x03C2, out);
    } else {
      encodeTo(utf8proc_tolower(cp), out);
    }
  }
  return out;
}

std::string collationKey(std::string_view word) {
  utf8proc_uint8_t* mapped = nullptr;
  utf8proc_ssize_t n = utf8proc_map(
      reinterpret_cast<const utf8proc_uint8_t*>(word.data()),
      static_cast<utf8proc_ssize_t>(word.size()), &mapped,
      static_cast<utf8proc_option_t>(UTF8PROC_DECOMPOSE | UTF8PROC_STRIPMARK |
                                     UTF8PROC_CASEFOLD | UTF8PROC_STABLE));
  if (n < 0) return std::string(word);
  std::string result(reinterpret_cast<char*>(mapped), static_cast<size_t>(n));
  free(mapped);
  return result;
}

void Counter::note(std::string_view item, int count) {
  auto it = indexByKey_.find(item);  // heterogeneous string_view lookup
  if (it == indexByKey_.end()) {
    indexByKey_.emplace(std::string(item), items_.size());
    items_.emplace_back(std::string(item), count);
  } else {
    items_[it->second].second += count;
  }
  total_ += count;
}

int Counter::count(std::string_view item) const {
  auto it = indexByKey_.find(item);
  return it == indexByKey_.end() ? 0 : items_[it->second].second;
}

std::vector<std::pair<std::string, int>> Counter::byFrequency() const {
  std::vector<std::pair<std::string, int>> result = items_;
  std::stable_sort(result.begin(), result.end(),
                   [](const auto& a, const auto& b) {
                     return a.second > b.second;
                   });
  return result;
}

std::vector<std::string> Counter::mostFrequent(size_t n) const {
  auto all = byFrequency();
  n = std::min(n, all.size());
  std::vector<std::string> result;
  result.reserve(n);
  for (size_t i = 0; i < n; ++i) result.push_back(std::move(all[i].first));
  return result;
}

StopWords StopWords::fromFile(const std::string& path) {
  StopWords sw;
  sw.name_ = std::filesystem::path(path).filename().string();
  std::ifstream in(path);
  if (!in) return sw;
  std::string line;
  while (std::getline(in, line)) {
    // `|` starts a comment; words are whitespace-separated.
    if (size_t bar = line.find('|'); bar != std::string::npos) {
      line.erase(bar);
    }
    std::istringstream ss(line);
    std::string w;
    while (ss >> w) {
      sw.stopwords_.push_back(foldForMatch(w));
    }
  }
  std::sort(sw.stopwords_.begin(), sw.stopwords_.end());
  sw.stopwords_.erase(
      std::unique(sw.stopwords_.begin(), sw.stopwords_.end()),
      sw.stopwords_.end());
  return sw;
}

bool StopWords::isStopWord(std::string_view word) const {
  if (codePointCount(word) == 1) return true;
  std::string folded = foldForMatch(word);
  return std::binary_search(stopwords_.begin(), stopwords_.end(), folded);
}

StopWordsSet::StopWordsSet(const std::string& dir) {
  std::error_code ec;
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file()) continue;
    std::string name = entry.path().filename().string();
    if (name == "NOTICE" || name[0] == '.') continue;
    files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());  // deterministic order
  for (const auto& f : files) {
    StopWords sw = StopWords::fromFile(f.string());
    if (sw.loaded()) languages_.push_back(std::move(sw));
  }
}

const StopWords* StopWordsSet::find(std::string_view name) const {
  for (const StopWords& sw : languages_) {
    if (sw.name() == name) return &sw;
  }
  return nullptr;
}

const StopWords* StopWordsSet::guess(const Counter& wordCounter) const {
  std::vector<std::string> top = wordCounter.mostFrequent(50);
  const StopWords* winner = nullptr;
  size_t best = 0;
  for (const StopWords& sw : languages_) {
    size_t hits = 0;
    for (const std::string& w : top) {
      // Single-codepoint words are "stop words" for every language and
      // carry no signal; skip them so they can't crown a winner alone.
      if (codePointCount(w) > 1 && sw.isStopWord(w)) ++hits;
    }
    if (hits > best) {
      winner = &sw;
      best = hits;
    }
  }
  return winner;
}

const StopWords* StopWordsSet::guess(std::string_view text) const {
  Counter counter;
  for (const std::string& w : wordsOf(text)) {
    counter.note(foldForMatch(w));
  }
  return guess(counter);
}

std::optional<CaseFold> findCaseFold(std::string_view slug) {
  if (slug == "guess") return CaseFold::kGuess;
  if (slug == "as-written") return CaseFold::kAsWritten;
  if (slug == "lower") return CaseFold::kLower;
  if (slug == "upper") return CaseFold::kUpper;
  return std::nullopt;
}

std::string foldDisplay(std::string_view word, CaseFold fold) {
  if (fold != CaseFold::kLower && fold != CaseFold::kUpper) {
    return std::string(word);
  }
  const bool upper = fold == CaseFold::kUpper;
  std::string out;
  out.reserve(word.size());
  size_t i = 0;
  while (i < word.size()) {
    int32_t cp = decodeAt(word, i);
    if (upper) {
      encodeTo(utf8proc_toupper(cp), out);
    } else if (cp == 0x03A3 && i >= word.size()) {
      encodeTo(0x03C2, out);  // word-final sigma, as in foldForMatch
    } else {
      encodeTo(utf8proc_tolower(cp), out);
    }
  }
  return out;
}

std::vector<WordCount> countWords(std::string_view text,
                                  const StopWords* reject, CaseFold fold) {
  Counter counter;
  // Every distinct casing seen per folded key, with its own count, in
  // first-seen order. Under kGuess the display form is the most frequent
  // casing, ties to the earliest — the original's "Guess Case for Each
  // Word" fold, so "Que" from chapter headings can't outrank the
  // ordinary "que". Under kAsWritten each spelling is its own key.
  absl::flat_hash_map<std::string, std::vector<std::pair<std::string, int>>>
      casings;
  const bool merge = fold != CaseFold::kAsWritten;
  for (const std::string& w : wordsOf(text)) {
    if (reject && reject->isStopWord(w)) continue;
    if (!merge) {
      counter.note(w);
      continue;
    }
    std::string key = foldForMatch(w);
    counter.note(key);
    auto& forms = casings[key];
    bool seen = false;
    for (auto& [form, n] : forms) {
      if (form == w) {
        ++n;
        seen = true;
        break;
      }
    }
    if (!seen) forms.emplace_back(w, 1);
  }
  std::vector<WordCount> result;
  for (auto& [key, count] : counter.byFrequency()) {
    if (!merge) {
      result.push_back({key, count});
      continue;
    }
    const auto& forms = casings[key];
    const std::pair<std::string, int>* best = &forms.front();
    for (const auto& form : forms) {
      if (form.second > best->second) best = &form;
    }
    result.push_back({foldDisplay(best->first, fold), count});
  }
  return result;
}

}  // namespace words
