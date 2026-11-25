#include "names.h"

#include <cctype>

namespace {

const char* const kOnsets[] = {"",  "",   "k",  "v",  "t",  "s",  "r",  "m",  "n",  "th", "z",
                               "d", "l",  "sh", "kr", "dr", "ph", "x",  "b",  "g",  "qu", "h"};
const char* const kVowels[] = {"a", "e", "i", "o", "u", "ae", "ai", "ou", "y", "a", "e", "o"};
const char* const kCodas[] = {"", "", "", "n", "r", "s", "th", "l", "x", "k", "m", "d"};
const char* const kSpeciesSuffixes[] = {"i", "an", "ar", "ites", "ari", "on", "ese"};

template <std::size_t N>
const char* pick(Rng& rng, const char* const (&options)[N]) {
  return options[rng.next() % N];
}

}  // namespace

std::string NameGen::word(int minSyllables, int maxSyllables) {
  const int syllables = rng_.range(minSyllables, maxSyllables);
  std::string s;
  for (int i = 0; i < syllables; ++i) {
    s += pick(rng_, kOnsets);
    s += pick(rng_, kVowels);
    if (i == syllables - 1) s += pick(rng_, kCodas);
  }
  s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
  return s;
}

std::string NameGen::unique(int minSyllables, int maxSyllables) {
  // Allow longer names as collisions pile up, so huge galaxies never stall.
  for (int attempt = 0;; ++attempt) {
    std::string w = word(minSyllables, maxSyllables + attempt / 50);
    if (used_.insert(w).second) return w;
  }
}

std::string NameGen::speciesName() {
  for (int attempt = 0;; ++attempt) {
    std::string w = word(2, 2 + attempt / 50) + pick(rng_, kSpeciesSuffixes);
    if (used_.insert(w).second) return w;
  }
}

std::string roman(int n) {
  static const std::pair<int, const char*> kTable[] = {
      {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"}, {50, "L"},
      {40, "XL"},  {10, "X"},   {9, "IX"},  {5, "V"},   {4, "IV"},  {1, "I"}};
  std::string out;
  for (const auto& [value, glyph] : kTable) {
    while (n >= value) {
      out += glyph;
      n -= value;
    }
  }
  return out;
}
