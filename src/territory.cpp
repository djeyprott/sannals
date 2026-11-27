#include "territory.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace {

// Stars sit about 18 ly apart on average, so a lone outpost claims roughly
// its own neighbourhood and a developed capital reaches a little further.
constexpr double kBaseClaimLy = 14.0;      // a lone outpost
constexpr double kPerExtraColonyLy = 5.0;  // each further colony in the system
constexpr double kCapitalBonusLy = 8.0;

struct Claim {
  double x, y, radius;
  Id empire;
};

std::vector<Claim> gatherClaims(const World& w) {
  // Count colonies per (system, empire).
  struct Tally {
    Id empire = kNone;
    int colonies = 0;
    bool capital = false;
  };
  std::vector<std::vector<Tally>> perSystem(w.systems.size());
  for (const Colony& c : w.colonies) {
    if (c.empire == kNone) continue;
    auto& tallies = perSystem[static_cast<std::size_t>(w.bodies[static_cast<std::size_t>(c.body)].system)];
    auto it = std::find_if(tallies.begin(), tallies.end(), [&](const Tally& t) { return t.empire == c.empire; });
    if (it == tallies.end()) {
      tallies.push_back({c.empire, 0, false});
      it = std::prev(tallies.end());
    }
    ++it->colonies;
    if (w.empires[static_cast<std::size_t>(c.empire)].capital == c.id) it->capital = true;
  }

  std::vector<Claim> claims;
  for (std::size_t s = 0; s < perSystem.size(); ++s) {
    for (const Tally& t : perSystem[s]) {
      const double radius =
          kBaseClaimLy + kPerExtraColonyLy * (t.colonies - 1) + (t.capital ? kCapitalBonusLy : 0.0);
      claims.push_back({w.systems[s].x, w.systems[s].y, radius, t.empire});
    }
  }
  return claims;
}

}  // namespace

std::uint64_t territorySignature(const World& w) {
  std::uint64_t h = 1469598103934665603ULL;  // FNV-1a
  auto mix = [&h](std::uint64_t v) {
    h ^= v;
    h *= 1099511628211ULL;
  };
  for (const Colony& c : w.colonies) {
    mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(c.body)));
    mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(c.empire)));
  }
  for (const Empire& e : w.empires) mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(e.capital)));
  return h;
}

Territory computeTerritory(const World& w, int maxCells) {
  Territory t;
  if (w.systems.empty()) return t;

  double minX = w.systems.front().x, maxX = minX, minY = w.systems.front().y, maxY = minY;
  for (const StarSystem& s : w.systems) {
    minX = std::min(minX, s.x);
    maxX = std::max(maxX, s.x);
    minY = std::min(minY, s.y);
    maxY = std::max(maxY, s.y);
  }
  const double margin = 60.0;  // room for claims at the galaxy's edge
  minX -= margin;
  minY -= margin;
  maxX += margin;
  maxY += margin;

  t.cellSize = std::max(maxX - minX, maxY - minY) / maxCells;
  t.originX = minX;
  t.originY = minY;
  t.width = static_cast<int>(std::ceil((maxX - minX) / t.cellSize));
  t.height = static_cast<int>(std::ceil((maxY - minY) / t.cellSize));
  const std::size_t cells = static_cast<std::size_t>(t.width) * static_cast<std::size_t>(t.height);
  t.owner.assign(cells, kNone);

  // best[i] is the strongest claim on cell i so far, as distance / radius.
  // Anything at or beyond 1.0 is out of reach.
  std::vector<float> best(cells, 1.0f);
  for (const Claim& c : gatherClaims(w)) {
    const int cx = static_cast<int>((c.x - t.originX) / t.cellSize);
    const int cy = static_cast<int>((c.y - t.originY) / t.cellSize);
    const int reach = static_cast<int>(std::ceil(c.radius / t.cellSize)) + 1;
    for (int y = std::max(0, cy - reach); y <= std::min(t.height - 1, cy + reach); ++y) {
      const double wy = t.originY + (y + 0.5) * t.cellSize;
      for (int x = std::max(0, cx - reach); x <= std::min(t.width - 1, cx + reach); ++x) {
        const double wx = t.originX + (x + 0.5) * t.cellSize;
        const float strength = static_cast<float>(std::hypot(wx - c.x, wy - c.y) / c.radius);
        const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(t.width) + static_cast<std::size_t>(x);
        if (strength < best[i]) {
          best[i] = strength;
          t.owner[i] = c.empire;
        }
      }
    }
  }
  return t;
}
