#pragma once

#include <cstdint>
#include <utility>
#include <vector>

// xoshiro256** seeded through splitmix64.
//
// We avoid <random> distributions on purpose: their output differs between
// standard libraries, which would break "same seed, same history". Everything
// here is plain integer math, so a seed replays identically on every platform
// (floating-point math elsewhere can still differ across compilers in the last
// bits, so cross-platform replays are best-effort; same-binary replays are exact).
class Rng {
 public:
  explicit Rng(std::uint64_t seed) {
    std::uint64_t x = seed;
    for (auto& s : s_) s = splitmix(x);
  }

  std::uint64_t next() {
    const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
    const std::uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return result;
  }

  // Uniform double in [0, 1).
  double uniform() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }
  double uniform(double lo, double hi) { return lo + (hi - lo) * uniform(); }

  // Uniform int in [lo, hi], inclusive. Modulo bias is negligible at our sizes.
  int range(int lo, int hi) {
    const auto span = static_cast<std::uint64_t>(hi - lo + 1);
    return lo + static_cast<int>(next() % span);
  }

  bool chance(double p) { return uniform() < p; }

  template <class T>
  void shuffle(std::vector<T>& v) {
    for (std::size_t i = v.size(); i > 1; --i) std::swap(v[i - 1], v[next() % i]);
  }

  // An independent stream. Giving each subsystem its own fork means a change
  // to one subsystem does not reshuffle every other subsystem's rolls.
  Rng fork(std::uint64_t salt) { return Rng(next() ^ (salt * 0x9E3779B97F4A7C15ULL)); }

 private:
  static std::uint64_t splitmix(std::uint64_t& x) {
    std::uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
  static std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

  std::uint64_t s_[4];
};
