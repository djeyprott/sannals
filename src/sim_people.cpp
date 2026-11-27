// People: figures, noble houses, offices, succession, rivalries and plots.
//
// Only notable people are simulated: members of the great houses and anyone
// raised to office. Everyone else stays part of a colony's population number.

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

#include "sim.h"

namespace {

template <class T>
T& at(std::vector<T>& v, Id id) {
  return v[static_cast<std::size_t>(id)];
}
template <class T>
const T& at(const std::vector<T>& v, Id id) {
  return v[static_cast<std::size_t>(id)];
}

EntityRef figureRef(Id id) { return {EntityKind::Figure, id}; }
EntityRef empireRef(Id id) { return {EntityKind::Empire, id}; }
EntityRef houseRef(Id id) { return {EntityKind::Organization, id}; }

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

std::int64_t yearsToDays(double years) { return static_cast<std::int64_t>(years * kDaysPerYear); }

constexpr double kAdultShare = 0.2;           // of lifespan
constexpr double kFertileUntilShare = 0.6;    // of lifespan
constexpr int kMaxLivingPerHouse = 10;
constexpr std::size_t kMaxMemories = 32;
constexpr double kRivalryThreshold = -40.0;
constexpr double kPlotThreshold = 0.65;  // disaffection needed before anyone plots
constexpr double kPlotCooldownYears = 15.0;
constexpr int kNewRulerGraceYears = 3;   // a new regime gets a short honeymoon

}  // namespace

// ---- Queries ---------------------------------------------------------------

bool Sim::isAdult(const Figure& f) const {
  const double life = at(w_.species, f.species).lifespanYears;
  return ageInYears(f, w_.day) >= life * kAdultShare;
}

Id Sim::houseHead(Id house) const {
  if (house == kNone) return kNone;
  Id head = kNone;
  bool headAdult = false;
  for (Id m : at(w_.organizations, house).members) {
    const Figure& f = at(w_.figures, m);
    if (!f.alive()) continue;
    const bool adult = isAdult(f);
    // Eldest adult leads; a house of children is led by its eldest child.
    if (head == kNone || (adult && !headAdult) ||
        (adult == headAdult && f.bornDay < at(w_.figures, head).bornDay)) {
      head = m;
      headAdult = adult;
    }
  }
  return head;
}

bool Sim::isProminent(const Figure& f) const {
  if (!f.alive() || !isAdult(f)) return false;
  return f.role != Role::None || (f.house != kNone && houseHead(f.house) == f.id);
}

double Sim::housePrestige(const Figure& f) const {
  return f.house == kNone ? 0.0 : at(w_.organizations, f.house).prestige;
}

std::vector<Id> Sim::livingAdults(Id empire) const {
  std::vector<Id> out;
  for (const Figure& f : w_.figures) {
    if (f.empire == empire && f.alive() && isAdult(f)) out.push_back(f.id);
  }
  return out;
}

double Sim::opinion(Id who, Id of) const {
  if (who == kNone || of == kNone) return 0.0;
  for (const Opinion& o : at(w_.figures, who).opinions) {
    if (o.of == of) return o.value;
  }
  return 0.0;
}

// ---- Creating people and houses ---------------------------------------------

Id Sim::newFigure(Id empire, Id house, Id parentId, std::int64_t bornDay) {
  Figure f;
  f.id = static_cast<Id>(w_.figures.size());
  f.empire = empire;
  f.species = at(w_.empires, empire).species;
  f.house = house;
  f.parent = parentId;
  f.bornDay = bornDay;
  const std::string surname = house != kNone ? at(w_.organizations, house).surname : names_.word(2, 2);
  f.name = names_.word(1, 2) + " " + surname;

  // Children take after their parent, with plenty of room to differ.
  const bool hasParent = parentId != kNone;
  const Figure parent = hasParent ? at(w_.figures, parentId) : Figure{};
  auto talent = [&](int fromParent) {
    int v = rng_.range(0, 14) + rng_.range(0, 6);
    if (hasParent) v = (v + fromParent) / 2 + rng_.range(-2, 2);
    return std::clamp(v, 0, 20);
  };
  auto temper = [&](double fromParent) {
    double v = rng_.uniform();
    if (hasParent) v = 0.5 * v + 0.5 * fromParent + rng_.uniform(-0.1, 0.1);
    return clamp01(v);
  };
  f.administration = talent(parent.administration);
  f.military = talent(parent.military);
  f.intrigue = talent(parent.intrigue);
  f.ambition = temper(parent.ambition);
  f.loyalty = temper(parent.loyalty);
  f.ruthlessness = temper(parent.ruthlessness);

  w_.figures.push_back(std::move(f));
  const Id id = w_.figures.back().id;
  if (house != kNone) at(w_.organizations, house).members.push_back(id);
  if (hasParent) at(w_.figures, parentId).children.push_back(id);
  return id;
}

Id Sim::raiseCommoner(Id empire) {
  const double life = at(w_.species, at(w_.empires, empire).species).lifespanYears;
  const Id id = newFigure(empire, kNone, kNone, w_.day - yearsToDays(life * rng_.uniform(0.25, 0.5)));
  // Nobody rises from obscurity without some gift.
  Figure& f = at(w_.figures, id);
  f.administration = std::max(f.administration, rng_.range(8, 16));
  f.military = std::max(f.military, rng_.range(6, 16));
  f.ambition = std::max(f.ambition, rng_.uniform(0.4, 0.9));
  emit(EventType::FigureBorn, Importance::Minor, "{0} rises from obscurity in the {1}.",
       {figureRef(id), empireRef(empire)});
  return id;
}

Id Sim::foundHouse(Id empire, Id founder) {
  Figure& f = at(w_.figures, founder);
  Organization o;
  o.id = static_cast<Id>(w_.organizations.size());
  o.kind = OrgKind::NobleHouse;
  o.surname = f.name.substr(f.name.rfind(' ') + 1);
  o.name = "House " + o.surname;
  o.empire = empire;
  o.founder = founder;
  o.foundedDay = w_.day;
  o.prestige = 15.0;
  o.members.push_back(founder);
  f.house = o.id;
  at(w_.empires, empire).houses.push_back(o.id);
  w_.organizations.push_back(std::move(o));
  const Id id = w_.organizations.back().id;
  emit(EventType::HouseFounded, Importance::Notable, "{0} founds {1}.", {figureRef(founder), houseRef(id)});
  return id;
}

void Sim::seedElite(Id empire) {
  const double life = at(w_.species, at(w_.empires, empire).species).lifespanYears;
  const int houseCount = rng_.range(3, 5);
  for (int h = 0; h < houseCount; ++h) {
    Organization o;
    o.id = static_cast<Id>(w_.organizations.size());
    o.surname = names_.unique(2, 3);
    o.name = "House " + o.surname;
    o.empire = empire;
    o.foundedDay = w_.day - yearsToDays(life * rng_.uniform(1.0, 4.0));  // old families
    o.prestige = rng_.uniform(10, 50);
    w_.organizations.push_back(o);
    at(w_.empires, empire).houses.push_back(o.id);

    // A head of house, their children, and perhaps grandchildren.
    const Id head = newFigure(empire, o.id, kNone, w_.day - yearsToDays(life * rng_.uniform(0.45, 0.65)));
    at(w_.organizations, o.id).founder = head;
    const int children = rng_.range(1, 3);
    for (int c = 0; c < children; ++c) {
      const std::int64_t born = at(w_.figures, head).bornDay + yearsToDays(life * rng_.uniform(0.2, 0.32));
      if (born > w_.day) continue;
      const Id child = newFigure(empire, o.id, head, born);
      if (rng_.chance(0.5)) {
        const std::int64_t grandBorn = born + yearsToDays(life * rng_.uniform(0.2, 0.3));
        if (grandBorn <= w_.day) newFigure(empire, o.id, child, grandBorn);
      }
    }
  }
  succeed(empire, kNone);
  fillOffices(empire);
}

// ---- Memories and opinions --------------------------------------------------

void Sim::remember(Id who, MemoryKind kind, Id about, int weight) {
  if (who == kNone || about == kNone || who == about) return;
  Figure& f = at(w_.figures, who);
  if (!f.alive()) return;

  auto it = std::find_if(f.opinions.begin(), f.opinions.end(), [about](const Opinion& o) { return o.of == about; });
  if (it == f.opinions.end()) {
    f.opinions.push_back({about, 0.0});
    it = std::prev(f.opinions.end());
  }
  it->value = std::clamp(it->value + weight, -100.0, 100.0);

  f.memories.push_back({w_.day, kind, about, weight});
  if (f.memories.size() > kMaxMemories) {
    // Forget the faintest old memory first; grudges and debts last.
    auto faintest = std::min_element(f.memories.begin(), f.memories.end() - 4, [](const Memory& a, const Memory& b) {
      return std::abs(a.weight) < std::abs(b.weight);
    });
    f.memories.erase(faintest);
  }
}

// ---- Offices ------------------------------------------------------------------

void Sim::vacate(Id figure) {
  Figure& f = at(w_.figures, figure);
  Empire& e = at(w_.empires, f.empire);
  switch (f.role) {
    case Role::Ruler:
      if (e.ruler == figure) e.ruler = kNone;
      e.lastRuler = figure;
      break;
    case Role::Governor:
      if (f.seat != kNone && at(w_.colonies, f.seat).governor == figure) at(w_.colonies, f.seat).governor = kNone;
      break;
    case Role::Admiral:
      if (e.admiral == figure) e.admiral = kNone;
      break;
    case Role::None: break;
  }
  f.role = Role::None;
  f.seat = kNone;
}

void Sim::appoint(Id figure, Role role, Id seat) {
  vacate(figure);
  Figure& f = at(w_.figures, figure);
  Empire& e = at(w_.empires, f.empire);
  f.role = role;
  f.seat = seat;
  if (role == Role::Governor) {
    at(w_.colonies, seat).governor = figure;
    emit(EventType::Appointed, Importance::Minor, "{0} is appointed governor of {1}.",
         {figureRef(figure), {EntityKind::Body, at(w_.colonies, seat).body}});
  } else if (role == Role::Admiral) {
    e.admiral = figure;
    emit(EventType::Appointed, Importance::Notable, "{0} is named High Admiral of the {1}.",
         {figureRef(figure), empireRef(f.empire)});
  }
  if (e.ruler != kNone) remember(figure, MemoryKind::Favoured, e.ruler, 15);
}

void Sim::fillOffices(Id empire) {
  std::vector<Id> pool;
  for (Id id : livingAdults(empire)) {
    if (at(w_.figures, id).role == Role::None) pool.push_back(id);
  }

  auto fill = [&](Role role, Id seat, auto talentOf) {
    const Id ruler = at(w_.empires, empire).ruler;
    const Id rulerHouse = ruler == kNone ? kNone : at(w_.figures, ruler).house;
    Id best = kNone, second = kNone;
    double bestScore = -1e9, secondScore = -1e9;
    for (Id id : pool) {
      const Figure& f = at(w_.figures, id);
      double score = talentOf(f) + housePrestige(f) * 0.05 + opinion(ruler, id) * 0.05 + rng_.uniform(0, 4);
      if (rulerHouse != kNone && f.house == rulerHouse) score += 3;  // rulers favour their own
      if (score > bestScore) {
        second = best;
        secondScore = bestScore;
        best = id;
        bestScore = score;
      } else if (score > secondScore) {
        second = id;
        secondScore = score;
      }
    }

    // A thin bench, or sometimes just a ruler who likes new blood.
    if (best == kNone || (talentOf(at(w_.figures, best)) < 6 && rng_.chance(0.5))) {
      second = best;
      best = raiseCommoner(empire);
    }
    if (second != kNone && at(w_.figures, second).ambition > 0.65) {
      remember(second, MemoryKind::PassedOver, best, -15);
      remember(second, MemoryKind::PassedOver, ruler, -8);
    }
    pool.erase(std::remove(pool.begin(), pool.end(), best), pool.end());
    appoint(best, role, seat);
  };

  if (at(w_.empires, empire).admiral == kNone) {
    fill(Role::Admiral, kNone, [](const Figure& f) { return static_cast<double>(f.military); });
  }
  const std::vector<Id> colonies = at(w_.empires, empire).colonies;
  for (Id c : colonies) {
    if (at(w_.colonies, c).governor == kNone) {
      fill(Role::Governor, c, [](const Figure& f) { return static_cast<double>(f.administration); });
    }
  }
}

// ---- Succession ---------------------------------------------------------------

Id Sim::chooseSuccessor(Id empire, Id previous, double& legitimacy, Id& slighted) {
  const Empire& e = at(w_.empires, empire);
  slighted = kNone;

  auto eldest = [&](const std::vector<Id>& ids) {
    Id best = kNone;
    for (Id id : ids) {
      const Figure& f = at(w_.figures, id);
      if (!f.alive() || !isAdult(f)) continue;
      if (best == kNone || f.bornDay < at(w_.figures, best).bornDay) best = id;
    }
    return best;
  };
  // The most ambitious head of another house may resent a bloodline succession.
  auto resentful = [&](Id heir) {
    Id who = kNone;
    double most = 0.75;
    for (Id id : livingAdults(empire)) {
      const Figure& f = at(w_.figures, id);
      if (id == heir || !isProminent(f) || f.house == at(w_.figures, heir).house) continue;
      if (f.ambition > most) {
        most = f.ambition;
        who = id;
      }
    }
    return who;
  };

  if (e.hereditary && previous != kNone) {
    const Figure& prev = at(w_.figures, previous);
    if (const Id child = eldest(prev.children); child != kNone) {
      legitimacy = 80;
      slighted = resentful(child);
      return child;
    }
    if (prev.house != kNone) {
      if (const Id kin = eldest(at(w_.organizations, prev.house).members); kin != kNone) {
        legitimacy = 55;
        slighted = resentful(kin);
        return kin;
      }
    }
  }
  if (e.hereditary && previous == kNone) {
    // The first ruler comes from the most prestigious house.
    Id bestHead = kNone;
    double bestPrestige = -1;
    for (Id h : e.houses) {
      const Id head = houseHead(h);
      if (head == kNone || !isAdult(at(w_.figures, head))) continue;
      if (at(w_.organizations, h).prestige > bestPrestige) {
        bestPrestige = at(w_.organizations, h).prestige;
        bestHead = head;
      }
    }
    if (bestHead != kNone) {
      legitimacy = 75;
      return bestHead;
    }
  }

  // Election by the great and powerful (also the fallback when a line dies out).
  std::vector<Id> candidates;
  for (Id id : livingAdults(empire)) {
    if (isProminent(at(w_.figures, id))) candidates.push_back(id);
  }
  if (candidates.empty()) candidates = livingAdults(empire);
  if (candidates.empty()) {
    legitimacy = 40;
    return raiseCommoner(empire);
  }
  Id best = kNone, second = kNone;
  double bestScore = -1e9, secondScore = -1e9;
  for (Id id : candidates) {
    const Figure& f = at(w_.figures, id);
    const double score = f.administration + f.intrigue * 0.5 + housePrestige(f) * 0.15 + rng_.uniform(0, 6);
    if (score > bestScore) {
      second = best;
      secondScore = bestScore;
      best = id;
      bestScore = score;
    } else if (score > secondScore) {
      second = id;
      secondScore = score;
    }
  }
  legitimacy = e.hereditary ? 45 : 70;  // a broken bloodline is a weaker claim
  if (second != kNone && bestScore - secondScore < 3 && at(w_.figures, second).ambition > 0.6) slighted = second;
  return best;
}

void Sim::succeed(Id empire, Id previous) {
  double legitimacy = 50;
  Id slighted = kNone;
  const Id heir = chooseSuccessor(empire, previous, legitimacy, slighted);

  vacate(heir);
  Figure& h = at(w_.figures, heir);
  h.role = Role::Ruler;
  Empire& e = at(w_.empires, empire);
  e.ruler = heir;
  e.rulerSinceDay = w_.day;
  e.legitimacy = legitimacy;

  if (previous == kNone) {
    emit(EventType::Succession, Importance::Major, "{0} becomes the first ruler of the {1}.",
         {figureRef(heir), empireRef(empire)});
  } else {
    emit(EventType::Succession, Importance::Major, "{0} succeeds {1} as ruler of the {2}.",
         {figureRef(heir), figureRef(previous), empireRef(empire)});
  }
  if (at(w_.figures, heir).house == kNone) foundHouse(empire, heir);
  if (slighted != kNone) {
    remember(slighted, MemoryKind::PassedOver, heir, -30);
    emit(EventType::Rivalry, Importance::Notable, "{0} resents being passed over for the throne of the {1}.",
         {figureRef(slighted), empireRef(empire)});
  }
}

// ---- Life and death -----------------------------------------------------------

void Sim::kill(Id figure, Id killer, bool executed) {
  Figure& f = at(w_.figures, figure);
  if (!f.alive()) return;
  const Role role = f.role;
  const Id seat = f.seat;
  const Id empire = f.empire;
  const int age = ageInYears(f, w_.day);
  const bool headOfHouse = f.house != kNone && houseHead(f.house) == figure;
  f.diedDay = w_.day;
  vacate(figure);

  // Rulers' deaths are major; admirals' and heads of houses' notable; the rest minor.
  const Importance importance =
      role == Role::Ruler ? Importance::Major
                          : (role == Role::Admiral || headOfHouse || killer != kNone ? Importance::Notable : Importance::Minor);
  const std::string ageText = std::to_string(age);
  if (executed) {
    emit(EventType::FigureDied, importance, "{0} is executed on the orders of {1}.", {figureRef(figure), figureRef(killer)});
  } else if (killer != kNone) {
    emit(EventType::FigureDied, importance, "{0} is killed by the followers of {1}.", {figureRef(figure), figureRef(killer)});
  } else if (role == Role::Ruler) {
    emit(EventType::FigureDied, importance, "{0}, ruler of the {1}, dies at the age of " + ageText + ".",
         {figureRef(figure), empireRef(empire)});
  } else if (role == Role::Governor) {
    emit(EventType::FigureDied, importance, "{0}, governor of {1}, dies at the age of " + ageText + ".",
         {figureRef(figure), {EntityKind::Body, at(w_.colonies, seat).body}});
  } else if (role == Role::Admiral) {
    emit(EventType::FigureDied, importance, "{0}, High Admiral of the {1}, dies at the age of " + ageText + ".",
         {figureRef(figure), empireRef(empire)});
  } else {
    emit(EventType::FigureDied, importance, "{0} dies at the age of " + ageText + ".", {figureRef(figure)});
  }

  if (killer != kNone) {
    const Figure victim = at(w_.figures, figure);
    if (victim.parent != kNone) remember(victim.parent, MemoryKind::KinKilled, killer, -70);
    for (Id c : victim.children) remember(c, MemoryKind::KinKilled, killer, -70);
  }
}

void Sim::deaths(Id empire) {
  const double life = at(w_.species, at(w_.empires, empire).species).lifespanYears;
  for (std::size_t i = 0; i < w_.figures.size(); ++i) {
    const Figure& f = w_.figures[i];
    if (f.empire != empire || !f.alive()) continue;
    // Gompertz-style mortality: low through adulthood, climbing steeply near the lifespan.
    const double share = ageInYears(f, w_.day) / life;
    const double risk = 0.002 + 0.5 * std::exp((share - 1.0) * 8.0);
    if (rng_.chance(std::min(risk, 1.0))) kill(f.id, kNone, false);
  }
}

void Sim::births(Id empire) {
  const double life = at(w_.species, at(w_.empires, empire).species).lifespanYears;
  const std::vector<Id> houses = at(w_.empires, empire).houses;
  for (Id h : houses) {
    if (at(w_.organizations, h).endedDay >= 0) continue;
    int living = 0;
    for (Id m : at(w_.organizations, h).members) living += at(w_.figures, m).alive() ? 1 : 0;

    const std::vector<Id> members = at(w_.organizations, h).members;
    int born = 0;
    for (Id m : members) {
      if (living >= kMaxLivingPerHouse || born >= 2) break;
      const Figure& parent = at(w_.figures, m);
      if (!parent.alive() || !isAdult(parent) || ageInYears(parent, w_.day) > life * kFertileUntilShare) continue;
      if (!rng_.chance(0.07)) continue;
      const Id child = newFigure(empire, h, m, w_.day);
      ++living;
      ++born;
      const bool royal = at(w_.figures, m).role == Role::Ruler;
      emit(EventType::FigureBorn, royal ? Importance::Notable : Importance::Minor,
           royal ? "{0} is born to {1}, ruler of the {2}." : "{0} is born to {1}.",
           {figureRef(child), figureRef(m), empireRef(empire)});
    }
  }
}

void Sim::updateHouses(Id empire) {
  const Empire& e = at(w_.empires, empire);
  for (Id h : e.houses) {
    Organization& o = at(w_.organizations, h);
    if (o.endedDay >= 0) continue;
    bool anyAlive = false;
    int offices = 0;
    bool ruling = false;
    for (Id m : o.members) {
      const Figure& f = at(w_.figures, m);
      if (!f.alive()) continue;
      anyAlive = true;
      if (f.role == Role::Ruler) ruling = true;
      else if (f.role != Role::None) ++offices;
    }
    if (!anyAlive) {
      o.endedDay = w_.day;
      emit(EventType::HouseExtinct, Importance::Notable, "{0} has died out.", {houseRef(h)});
      continue;
    }
    o.prestige = o.prestige * 0.95 + (ruling ? 4.0 : 0.0) + offices;
  }
}

// ---- Court life -----------------------------------------------------------------

void Sim::socialize(Id empire) {
  std::vector<Id> prominent;
  for (Id id : livingAdults(empire)) {
    if (isProminent(at(w_.figures, id))) prominent.push_back(id);
  }

  // Old feelings fade, slowly.
  for (Figure& f : w_.figures) {
    if (f.empire != empire || !f.alive()) continue;
    for (Opinion& o : f.opinions) o.value *= 0.97;
  }

  if (prominent.size() < 2) return;
  for (Id p : prominent) {
    const Id q = prominent[rng_.next() % prominent.size()];
    if (q == p) continue;
    const Figure& a = at(w_.figures, p);
    const Figure& b = at(w_.figures, q);
    const double alike = 1.0 - (std::abs(a.ambition - b.ambition) + std::abs(a.ruthlessness - b.ruthlessness)) / 2.0;
    double delta = (alike - 0.5) * 16.0 + rng_.uniform(-12, 12);
    if (a.house != kNone && a.house == b.house) delta += 4;
    if (a.ambition > 0.6 && b.ambition > 0.6) delta -= 8;  // two climbers on one ladder

    const double before = opinion(p, q);
    if (std::abs(delta) >= 12) {
      remember(p, delta > 0 ? MemoryKind::Friendship : MemoryKind::Quarrel, q, static_cast<int>(std::lround(delta)));
    } else {
      // A minor encounter: it shifts feelings but isn't worth remembering.
      Figure& f = at(w_.figures, p);
      auto it = std::find_if(f.opinions.begin(), f.opinions.end(), [q](const Opinion& o) { return o.of == q; });
      if (it == f.opinions.end()) {
        f.opinions.push_back({q, 0.0});
        it = std::prev(f.opinions.end());
      }
      it->value = std::clamp(it->value + delta, -100.0, 100.0);
    }
    auto& rivals = at(w_.figures, p).rivals;
    const bool alreadyRivals = std::find(rivals.begin(), rivals.end(), q) != rivals.end();
    if (!alreadyRivals && before > kRivalryThreshold && opinion(p, q) <= kRivalryThreshold) {
      rivals.push_back(q);
      at(w_.figures, q).rivals.push_back(p);
      emit(EventType::Rivalry, Importance::Notable, "{0} and {1} become bitter rivals.", {figureRef(p), figureRef(q)});
    }
  }
}

void Sim::plots(Id empire) {
  const Id ruler = at(w_.empires, empire).ruler;
  if (ruler == kNone) return;
  if (w_.day - at(w_.empires, empire).rulerSinceDay < kNewRulerGraceYears * kDaysPerYear) return;
  const double legitimacy = at(w_.empires, empire).legitimacy;

  // The most disaffected powerful figure is the likeliest plotter.
  Id plotter = kNone;
  double desire = kPlotThreshold;
  const std::vector<Id> adults = livingAdults(empire);
  for (Id id : adults) {
    const Figure& f = at(w_.figures, id);
    if (id == ruler || !isProminent(f)) continue;
    if (w_.day - f.lastPlotDay < yearsToDays(kPlotCooldownYears)) continue;
    const double d = f.ambition * (1.0 - f.loyalty) + std::max(0.0, -opinion(id, ruler)) / 100.0 +
                     (100.0 - legitimacy) / 400.0;
    if (d > desire) {
      desire = d;
      plotter = id;
    }
  }
  if (plotter == kNone || !rng_.chance((desire - kPlotThreshold) * 0.4)) return;
  at(w_.figures, plotter).lastPlotDay = w_.day;

  int supporters = 0, loyalists = 0;
  std::vector<Id> backers;
  for (Id id : adults) {
    if (id == plotter || id == ruler || !isProminent(at(w_.figures, id))) continue;
    if (opinion(id, plotter) > 15 && opinion(id, ruler) < 0) {
      ++supporters;
      backers.push_back(id);
    } else if (opinion(id, ruler) > 20) {
      ++loyalists;
    }
  }

  const Figure& p = at(w_.figures, plotter);
  const Figure& r = at(w_.figures, ruler);
  const Id admiral = at(w_.empires, empire).admiral;
  const double power = p.intrigue + p.military * (p.role == Role::Admiral ? 1.5 : 0.3) + housePrestige(p) * 0.1 +
                       supporters * 3.0;
  double defense = r.intrigue + legitimacy / 8.0 + loyalists * 3.0;
  if (admiral != kNone && admiral != plotter && opinion(admiral, ruler) >= 0) {
    defense += at(w_.figures, admiral).military;  // the fleet stands by the throne
  }
  const bool success = rng_.chance(power / (power + defense));

  if (success) {
    emit(EventType::Coup, Importance::Major, "{0} seizes power in the {1}, overthrowing {2}.",
         {figureRef(plotter), empireRef(empire), figureRef(ruler)});
    const Id rulerHouse = r.house;
    if (rulerHouse != kNone) {
      for (Id m : at(w_.organizations, rulerHouse).members) remember(m, MemoryKind::Overthrown, plotter, -40);
    }
    if (at(w_.figures, plotter).ruthlessness > 0.45) {
      kill(ruler, plotter, false);
    } else {
      vacate(ruler);
      remember(ruler, MemoryKind::Overthrown, plotter, -80);
      remember(ruler, MemoryKind::Spared, plotter, 10);
    }
    vacate(plotter);
    at(w_.figures, plotter).role = Role::Ruler;
    Empire& e = at(w_.empires, empire);
    e.ruler = plotter;
    e.rulerSinceDay = w_.day;
    e.legitimacy = 25;
    if (at(w_.figures, plotter).house == kNone) foundHouse(empire, plotter);
    for (Id b : backers) remember(b, MemoryKind::Favoured, plotter, 10);
  } else {
    remember(ruler, MemoryKind::PlotAgainstMe, plotter, -60);
    at(w_.empires, empire).legitimacy = std::min(90.0, legitimacy + 5);  // survived, and seen to survive
    if (r.ruthlessness > 0.4) {
      emit(EventType::PlotFailed, Importance::Notable, "A plot by {0} against {1} of the {2} is uncovered.",
           {figureRef(plotter), figureRef(ruler), empireRef(empire)});
      kill(plotter, ruler, true);
    } else {
      emit(EventType::PlotFailed, Importance::Notable,
           "{0} plots against {1} of the {2}, fails, and is stripped of office but spared.",
           {figureRef(plotter), figureRef(ruler), empireRef(empire)});
      vacate(plotter);
      remember(plotter, MemoryKind::Spared, ruler, 30);
      Figure& spared = at(w_.figures, plotter);
      spared.loyalty = clamp01(spared.loyalty + 0.15);  // mercy buys some gratitude
    }
  }
}

// ---- Yearly ------------------------------------------------------------------------

void Sim::peopleTick(Id empire) {
  deaths(empire);
  if (at(w_.empires, empire).ruler == kNone) succeed(empire, at(w_.empires, empire).lastRuler);
  births(empire);

  Empire& e = at(w_.empires, empire);
  const bool ableRuler = e.ruler != kNone && at(w_.figures, e.ruler).administration > 12;
  e.legitimacy = std::min(90.0, e.legitimacy + 2.0 + (ableRuler ? 1.0 : 0.0));

  socialize(empire);
  plots(empire);
  if (at(w_.empires, empire).ruler == kNone) succeed(empire, at(w_.empires, empire).lastRuler);
  fillOffices(empire);
  updateHouses(empire);
}
