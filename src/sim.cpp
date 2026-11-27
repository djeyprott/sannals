#include "sim.h"

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace {

template <class T>
T& at(std::vector<T>& v, Id id) {
  return v[static_cast<std::size_t>(id)];
}
template <class T>
const T& at(const std::vector<T>& v, Id id) {
  return v[static_cast<std::size_t>(id)];
}

const char* const kGovernmentForms[] = {"Hegemony", "Republic",     "Dominion",  "Concord",
                                        "Directorate", "Union",     "Imperium",  "Commonwealth",
                                        "Collective",  "Ascendancy", "Compact",  "Sovereignty"};
// Whether each form above passes power by bloodline (true) or by council election.
const bool kHereditary[] = {true, false, true, false, false, false, true, false, false, true, false, true};

// Total population thresholds (millions) that earn a chronicle entry.
const double kPopulationMilestones[] = {1e4, 3e4, 1e5, 3e5, 1e6, 3e6};
constexpr int kMilestoneCount = static_cast<int>(std::size(kPopulationMilestones));

}  // namespace

Sim::Sim(World& world, SimConfig config)
    : w_(world),
      cfg_(config),
      rng_(Rng(world.seed).fork(0x5157)),
      nameRng_(Rng(world.seed).fork(0x4E414D45)),
      names_(nameRng_) {}

void Sim::start() { checkEmergence(); }

void Sim::stepDay() {
  dailyTick();
  ++w_.day;
  if (w_.day % kDaysPerMonth == 0) monthlyTick();
  if (w_.day % kDaysPerYear == 0) yearlyTick();
}

void Sim::emit(EventType type, Importance importance, std::string text, std::vector<EntityRef> refs) {
  w_.log.add(Event{w_.day, type, importance, std::move(text), std::move(refs)});
}

// ---- Daily -----------------------------------------------------------------

void Sim::dailyTick() {
  for (ColonyShip& ship : w_.ships) {
    if (ship.leg + 1 >= ship.route.size()) {
      arrive(ship);
      ship.done = true;
      continue;
    }
    const StarSystem& from = at(w_.systems, ship.route[ship.leg]);
    const StarSystem& to = at(w_.systems, ship.route[ship.leg + 1]);
    const double laneLength = std::max(distance(from, to), 1.0);
    ship.progress += cfg_.shipSpeedLyPerDay / laneLength;
    if (ship.progress >= 1.0) {
      ship.progress = 0.0;
      ++ship.leg;
    }
  }
  std::erase_if(w_.ships, [](const ColonyShip& s) { return s.done; });
}

void Sim::arrive(const ColonyShip& ship) {
  Body& body = at(w_.bodies, ship.targetBody);
  Empire& empire = at(w_.empires, ship.empire);
  if (body.colony != kNone) {
    emit(EventType::ColonyShipLost, Importance::Minor,
         "A colony ship of the {0} reaches {1} to find it already settled; its colonists are lost.",
         {{EntityKind::Empire, empire.id}, {EntityKind::Body, body.id}});
    return;
  }
  Colony c;
  c.id = static_cast<Id>(w_.colonies.size());
  c.body = body.id;
  c.species = empire.species;
  c.empire = empire.id;
  c.population = ship.colonists;
  c.foundedDay = w_.day;
  body.colony = c.id;
  empire.colonies.push_back(c.id);
  w_.colonies.push_back(c);
  emit(EventType::ColonyFounded, Importance::Minor, "Settlers of the {0} found a colony on {1}.",
       {{EntityKind::Empire, empire.id}, {EntityKind::Body, body.id}});
}

// ---- Monthly ---------------------------------------------------------------

void Sim::monthlyTick() {
  for (Colony& c : w_.colonies) {
    Body& body = at(w_.bodies, c.body);
    const Species& species = at(w_.species, c.species);

    // Logistic growth toward what the world can support for this species.
    const double capacity = std::max(habitability(species, body) * kWorldCapacity, 1.0);
    c.population += c.population * (species.growthRate / kMonthsPerYear) * (1.0 - c.population / capacity);
    c.population = std::max(c.population, 0.01);

    if (c.empire == kNone) continue;  // pre-spaceflight peoples don't feed an empire

    // Mining: output scales with population and runs the deposit down.
    // A capable governor runs the mines better; an empty seat runs them badly.
    const double management =
        c.governor == kNone ? 0.7 : 0.7 + 0.03 * at(w_.figures, c.governor).administration;
    const double wanted = c.population * 0.002 * management;
    const double mined = std::min(wanted, body.minerals);
    body.minerals -= mined;
    at(w_.empires, c.empire).minerals += mined;
    if (body.minerals <= 0.0 && !body.depletionReported) {
      body.depletionReported = true;
      emit(EventType::DepositDepleted, Importance::Notable, "The mines of {0} run dry, a blow to the {1}.",
           {{EntityKind::Body, body.id}, {EntityKind::Empire, c.empire}});
    }
  }
}

// ---- Yearly ----------------------------------------------------------------

void Sim::yearlyTick() {
  for (Species& s : w_.species) {
    if (!s.spacefaring) s.techProgress += s.techAptitude * rng_.uniform(0.5, 1.5) / 150.0;
  }
  checkEmergence();

  for (std::size_t i = 0; i < w_.empires.size(); ++i) {
    const Id id = static_cast<Id>(i);
    peopleTick(id);
    planExpansion(w_.empires[i]);
    checkMilestones(w_.empires[i]);
  }
}

void Sim::checkEmergence() {
  for (Species& s : w_.species) {
    if (!s.spacefaring && s.techProgress >= 1.0) foundEmpire(s);
  }
}

void Sim::foundEmpire(Species& s) {
  Empire e;
  e.id = static_cast<Id>(w_.empires.size());
  const std::size_t form = rng_.next() % std::size(kGovernmentForms);
  e.government = kGovernmentForms[form];
  e.hereditary = kHereditary[form];
  e.name = s.name + " " + e.government;
  e.species = s.id;
  e.capital = at(w_.bodies, s.homeworld).colony;
  e.colonies = {e.capital};
  e.expansionDrive = rng_.uniform(0.2, 1.0);
  e.foundedDay = w_.day;

  at(w_.colonies, e.capital).empire = e.id;
  s.spacefaring = true;
  s.empire = e.id;
  w_.empires.push_back(std::move(e));

  const Id id = w_.empires.back().id;
  emit(EventType::EmpireFounded, Importance::Major, "The {0} of {1} reach the stars and found the {2}.",
       {{EntityKind::Species, s.id}, {EntityKind::Body, s.homeworld}, {EntityKind::Empire, id}});
  seedElite(id);
}

// Half culture, half whoever is in charge: an ambitious ruler pushes outward.
double Sim::effectiveDrive(const Empire& e) const {
  if (e.ruler == kNone) return e.expansionDrive;
  return 0.5 * e.expansionDrive + 0.5 * at(w_.figures, e.ruler).ambition;
}

void Sim::planExpansion(Empire& e) {
  const double drive = effectiveDrive(e);
  const int maxLaunches = 1 + static_cast<int>(drive * 2.0);
  for (int n = 0; n < maxLaunches && e.minerals >= cfg_.colonyShipCost; ++n) {
    const std::optional<Target> target = chooseTarget(e, drive);
    if (!target) break;

    Colony& capital = at(w_.colonies, e.capital);
    const double colonists = std::min(cfg_.colonistsPerShip, capital.population * 0.05);
    capital.population -= colonists;
    e.minerals -= cfg_.colonyShipCost;

    ColonyShip ship;
    ship.empire = e.id;
    ship.targetBody = target->body;
    ship.route = target->route;
    ship.colonists = colonists;
    w_.ships.push_back(std::move(ship));

    emit(EventType::ColonyShipLaunched, Importance::Minor, "The {0} launches a colony ship toward {1}.",
         {{EntityKind::Empire, e.id}, {EntityKind::Body, target->body}});
  }
}

bool Sim::isTargeted(Id body) const {
  return std::any_of(w_.ships.begin(), w_.ships.end(), [body](const ColonyShip& s) { return s.targetBody == body; });
}

// Breadth-first search over jump lanes from the capital, scoring every free
// body by habitability discounted by distance in jumps.
std::optional<Sim::Target> Sim::chooseTarget(const Empire& e, double drive) const {
  const Species& species = at(w_.species, e.species);
  const Id start = at(w_.bodies, at(w_.colonies, e.capital).body).system;

  std::vector<int> depth(w_.systems.size(), -1);
  std::vector<Id> parent(w_.systems.size(), kNone);
  std::vector<Id> frontier{start};
  depth[static_cast<std::size_t>(start)] = 0;
  for (std::size_t i = 0; i < frontier.size(); ++i) {
    const Id sys = frontier[i];
    if (depth[static_cast<std::size_t>(sys)] >= cfg_.maxRouteJumps) continue;
    for (Id next : at(w_.systems, sys).lanes) {
      if (depth[static_cast<std::size_t>(next)] >= 0) continue;
      depth[static_cast<std::size_t>(next)] = depth[static_cast<std::size_t>(sys)] + 1;
      parent[static_cast<std::size_t>(next)] = sys;
      frontier.push_back(next);
    }
  }

  const double threshold = 0.45 - 0.25 * drive;  // bolder empires settle worse worlds
  double bestScore = 0.0;
  Id bestBody = kNone;
  for (Id sys : frontier) {
    for (Id bodyId : at(w_.systems, sys).bodies) {
      const Body& body = at(w_.bodies, bodyId);
      if (body.colony != kNone || isTargeted(bodyId)) continue;
      const double h = habitability(species, body);
      if (h < threshold) continue;
      const double score = h / (1.0 + 0.35 * depth[static_cast<std::size_t>(sys)]);
      if (score > bestScore) {
        bestScore = score;
        bestBody = bodyId;
      }
    }
  }
  if (bestBody == kNone) return std::nullopt;

  Target t;
  t.body = bestBody;
  for (Id sys = at(w_.bodies, bestBody).system; sys != kNone; sys = parent[static_cast<std::size_t>(sys)]) {
    t.route.push_back(sys);
  }
  std::reverse(t.route.begin(), t.route.end());
  return t;
}

void Sim::checkMilestones(Empire& e) {
  double total = 0.0;
  for (Id c : e.colonies) total += at(w_.colonies, c).population;

  int reached = e.populationMilestone;
  while (reached < kMilestoneCount && total >= kPopulationMilestones[reached]) ++reached;
  if (reached == e.populationMilestone) return;
  e.populationMilestone = reached;

  char amount[32];
  std::snprintf(amount, sizeof amount, "%g", kPopulationMilestones[reached - 1] / 1000.0);
  emit(EventType::PopulationMilestone, Importance::Notable,
       "The {0} now numbers over " + std::string(amount) + " billion souls across " +
           std::to_string(e.colonies.size()) + " worlds.",
       {{EntityKind::Empire, e.id}});
}
