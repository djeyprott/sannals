#include "galaxy_gen.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

#include "names.h"
#include "rng.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kInf = std::numeric_limits<double>::infinity();

struct SizeSpec {
  int stars;
  int minSpecies;
  int maxSpecies;
};

SizeSpec sizeSpec(GalaxySize s) {
  switch (s) {
    case GalaxySize::Cluster: return {100, 2, 4};
    case GalaxySize::Small: return {500, 4, 8};
    case GalaxySize::Medium: return {1500, 8, 15};
    case GalaxySize::Large: return {4000, 15, 30};
    case GalaxySize::Huge: return {10000, 30, 45};
  }
  return {500, 4, 8};
}

StarClass rollStarClass(Rng& rng) {
  const double u = rng.uniform();
  if (u < 0.001) return StarClass::O;
  if (u < 0.011) return StarClass::B;
  if (u < 0.041) return StarClass::A;
  if (u < 0.121) return StarClass::F;
  if (u < 0.241) return StarClass::G;
  if (u < 0.461) return StarClass::K;
  return StarClass::M;
}

// Rough surface temperature (K) of a body 1 AU from a star of this class.
double baseTemperature(StarClass c) {
  switch (c) {
    case StarClass::O: return 2000;
    case StarClass::B: return 1200;
    case StarClass::A: return 700;
    case StarClass::F: return 380;
    case StarClass::G: return 290;
    case StarClass::K: return 220;
    case StarClass::M: return 150;
  }
  return 150;
}

void placeStars(World& w, Rng& rng, NameGen& names, int count) {
  const double radius = 10.0 * std::sqrt(static_cast<double>(count));
  const int arms = rng.range(2, 4);
  const double winding = rng.uniform(3.0, 5.0);
  w.systems.reserve(static_cast<std::size_t>(count));

  for (int i = 0; i < count; ++i) {
    double x, y;
    if (rng.chance(0.2)) {  // central bulge
      const double r = radius * 0.25 * std::sqrt(rng.uniform());
      const double a = rng.uniform(0, 2 * kPi);
      x = r * std::cos(a);
      y = r * std::sin(a);
    } else {  // spiral arms
      const double t = rng.uniform();
      const double r = radius * (0.15 + 0.85 * t);
      const int arm = rng.range(0, arms - 1);
      const double a = arm * 2 * kPi / arms + t * winding + rng.uniform(-0.35, 0.35);
      const double jitter = radius * 0.04;
      x = r * std::cos(a) + rng.uniform(-jitter, jitter);
      y = r * std::sin(a) + rng.uniform(-jitter, jitter);
    }
    StarSystem s;
    s.id = i;
    s.name = names.unique(2, 3);
    s.x = x;
    s.y = y;
    s.star = rollStarClass(rng);
    w.systems.push_back(std::move(s));
  }
}

void generateBodies(World& w, Rng& rng) {
  for (StarSystem& sys : w.systems) {
    const int count = rng.range(0, sys.star == StarClass::M ? 5 : 8);
    double orbitAU = 0.3 * rng.uniform(0.8, 1.2);
    for (int i = 0; i < count; ++i) {
      Body b;
      b.id = static_cast<Id>(w.bodies.size());
      b.system = sys.id;
      b.name = sys.name + " " + roman(i + 1);
      b.temperatureK = baseTemperature(sys.star) / std::sqrt(orbitAU);

      const double roll = rng.uniform();
      if (i >= 2 && roll < 0.4) b.kind = BodyKind::GasGiant;
      else if (roll > 0.9) b.kind = BodyKind::AsteroidBelt;
      else if (b.temperatureK < 170) b.kind = BodyKind::IceWorld;
      else b.kind = BodyKind::Rocky;

      switch (b.kind) {
        case BodyKind::GasGiant:
          b.gravity = rng.uniform(1.5, 3.5);
          b.minerals = 0;  // fuel harvesting comes later
          break;
        case BodyKind::AsteroidBelt:
          b.gravity = 0;
          b.minerals = rng.uniform(5000, 50000);
          break;
        case BodyKind::IceWorld:
          b.gravity = rng.uniform(0.1, 1.5);
          b.minerals = rng.uniform(1000, 10000);
          break;
        case BodyKind::Rocky:
          b.gravity = rng.uniform(0.2, 2.2);
          b.atmosphere = b.gravity > 0.4 && b.temperatureK < 600 && rng.chance(0.7);
          b.minerals = rng.uniform(500, 20000);
          break;
      }
      sys.bodies.push_back(b.id);
      w.bodies.push_back(std::move(b));
      orbitAU *= rng.uniform(1.5, 2.0);
    }
  }
}

void addLane(World& w, Id a, Id b) {
  auto& lanes = w.systems[static_cast<std::size_t>(a)].lanes;
  if (std::find(lanes.begin(), lanes.end(), b) != lanes.end()) return;
  lanes.push_back(b);
  w.systems[static_cast<std::size_t>(b)].lanes.push_back(a);
}

Id findRoot(std::vector<Id>& parent, Id x) {
  while (parent[static_cast<std::size_t>(x)] != x) {
    auto& p = parent[static_cast<std::size_t>(x)];
    p = parent[static_cast<std::size_t>(p)];
    x = p;
  }
  return x;
}

// Jump lanes: link each star to 1-3 of its nearest neighbours, then bridge any
// disconnected pockets (Boruvka-style) so every system is reachable.
void connectLanes(World& w, Rng& rng) {
  const Id n = static_cast<Id>(w.systems.size());
  constexpr int kNearest = 3;

  for (Id i = 0; i < n; ++i) {
    std::array<std::pair<double, Id>, kNearest> best;
    best.fill({kInf, kNone});
    for (Id j = 0; j < n; ++j) {
      if (j == i) continue;
      const double d = distance(w.systems[static_cast<std::size_t>(i)], w.systems[static_cast<std::size_t>(j)]);
      if (d < best.back().first) {
        best.back() = {d, j};
        std::sort(best.begin(), best.end());
      }
    }
    const int links = rng.range(1, kNearest);
    for (int k = 0; k < links; ++k) {
      if (best[static_cast<std::size_t>(k)].second != kNone) addLane(w, i, best[static_cast<std::size_t>(k)].second);
    }
  }

  std::vector<Id> parent(static_cast<std::size_t>(n));
  std::iota(parent.begin(), parent.end(), 0);
  for (Id i = 0; i < n; ++i) {
    for (Id j : w.systems[static_cast<std::size_t>(i)].lanes) {
      parent[static_cast<std::size_t>(findRoot(parent, i))] = findRoot(parent, j);
    }
  }

  for (;;) {
    std::vector<std::vector<Id>> components(static_cast<std::size_t>(n));
    for (Id i = 0; i < n; ++i) components[static_cast<std::size_t>(findRoot(parent, i))].push_back(i);
    const auto count = std::count_if(components.begin(), components.end(), [](const auto& c) { return !c.empty(); });
    if (count <= 1) break;

    for (const auto& members : components) {
      if (members.empty()) continue;
      const Id root = findRoot(parent, members.front());
      double bestD = kInf;
      Id bestA = kNone, bestB = kNone;
      for (Id a : members) {
        for (Id b = 0; b < n; ++b) {
          if (findRoot(parent, b) == root) continue;
          const double d = distance(w.systems[static_cast<std::size_t>(a)], w.systems[static_cast<std::size_t>(b)]);
          if (d < bestD) {
            bestD = d;
            bestA = a;
            bestB = b;
          }
        }
      }
      if (bestA != kNone) {
        addLane(w, bestA, bestB);
        parent[static_cast<std::size_t>(findRoot(parent, bestA))] = findRoot(parent, bestB);
      }
    }
  }
}

void seedSpecies(World& w, Rng& rng, NameGen& names, int target, double spacefaringShare) {
  std::vector<Id> candidates;
  for (const Body& b : w.bodies) {
    if (b.kind == BodyKind::Rocky && b.atmosphere && b.temperatureK >= 230 && b.temperatureK <= 330 &&
        b.gravity >= 0.4 && b.gravity <= 1.8) {
      candidates.push_back(b.id);
    }
  }
  rng.shuffle(candidates);

  std::vector<char> systemTaken(w.systems.size(), 0);
  for (Id bodyId : candidates) {
    if (static_cast<int>(w.species.size()) >= target) break;
    Body& body = w.bodies[static_cast<std::size_t>(bodyId)];
    if (systemTaken[static_cast<std::size_t>(body.system)]) continue;
    systemTaken[static_cast<std::size_t>(body.system)] = 1;

    Species s;
    s.id = static_cast<Id>(w.species.size());
    s.name = names.speciesName();
    s.homeworld = bodyId;
    s.prefTempK = body.temperatureK;
    s.tempTolK = rng.uniform(25, 45);
    s.prefGravity = body.gravity;
    s.growthRate = rng.uniform(0.01, 0.03);
    s.techAptitude = rng.uniform(0.5, 2.0);
    s.techProgress = rng.chance(spacefaringShare) ? 1.0 : rng.uniform(0.0, 0.9);

    body.minerals = std::max(body.minerals, rng.uniform(8000, 20000));

    Colony c;
    c.id = static_cast<Id>(w.colonies.size());
    c.body = bodyId;
    c.species = s.id;
    c.population = habitability(s, body) * kWorldCapacity * rng.uniform(0.3, 0.6);
    body.colony = c.id;

    w.species.push_back(std::move(s));
    w.colonies.push_back(c);
  }

  // Someone should already be in space when the story starts.
  const bool anySpacefaring =
      std::any_of(w.species.begin(), w.species.end(), [](const Species& s) { return s.techProgress >= 1.0; });
  if (!anySpacefaring && !w.species.empty()) w.species.front().techProgress = 1.0;
}

}  // namespace

std::optional<GalaxySize> parseGalaxySize(const std::string& s) {
  if (s == "cluster") return GalaxySize::Cluster;
  if (s == "small") return GalaxySize::Small;
  if (s == "medium") return GalaxySize::Medium;
  if (s == "large") return GalaxySize::Large;
  if (s == "huge") return GalaxySize::Huge;
  return std::nullopt;
}

const char* galaxySizeName(GalaxySize s) {
  switch (s) {
    case GalaxySize::Cluster: return "cluster";
    case GalaxySize::Small: return "small";
    case GalaxySize::Medium: return "medium";
    case GalaxySize::Large: return "large";
    case GalaxySize::Huge: return "huge";
  }
  return "?";
}

World generateGalaxy(const GalaxyParams& params) {
  World w;
  w.seed = params.seed;

  Rng root(params.seed);
  Rng nameRng = root.fork(1);
  Rng starRng = root.fork(2);
  Rng bodyRng = root.fork(3);
  Rng laneRng = root.fork(4);
  Rng lifeRng = root.fork(5);
  NameGen names(nameRng);

  const SizeSpec spec = sizeSpec(params.size);
  placeStars(w, starRng, names, spec.stars);
  generateBodies(w, bodyRng);
  connectLanes(w, laneRng);
  seedSpecies(w, lifeRng, names, lifeRng.range(spec.minSpecies, spec.maxSpecies), params.spacefaringShare);
  return w;
}
