#pragma once

#include <set>
#include <string>

#include "rng.h"

// Syllable-based name generator. unique() and speciesName() never repeat a name.
class NameGen {
 public:
  explicit NameGen(Rng& rng) : rng_(rng) {}

  std::string word(int minSyllables = 2, int maxSyllables = 3);
  std::string unique(int minSyllables = 2, int maxSyllables = 3);
  std::string speciesName();

 private:
  Rng& rng_;
  std::set<std::string> used_;
};

std::string roman(int n);
