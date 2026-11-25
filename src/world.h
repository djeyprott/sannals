#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "event_log.h"
#include "ids.h"

// Calendar: a 360-day standard year of 12 thirty-day months keeps tick math simple.
inline constexpr int kDaysPerMonth = 30;
inline constexpr int kMonthsPerYear = 12;
inline constexpr int kDaysPerYear = kDaysPerMonth * kMonthsPerYear;

// A perfectly habitable world holds this many million people.
inline constexpr double kWorldCapacity = 10000.0;

enum class StarClass { O, B, A, F, G, K, M };
enum class BodyKind { Rocky, GasGiant, IceWorld, AsteroidBelt };

struct Body {
  Id id = kNone;
  Id system = kNone;
  std::string name;
  BodyKind kind = BodyKind::Rocky;
  double temperatureK = 0;
  double gravity = 0;  // in standard g
  bool atmosphere = false;
  double minerals = 0;  // abstract mineral units
  bool depletionReported = false;
  Id colony = kNone;
};

struct StarSystem {
  Id id = kNone;
  std::string name;
  double x = 0, y = 0;  // light-years from the galactic core
  StarClass star = StarClass::M;
  std::vector<Id> bodies;
  std::vector<Id> lanes;  // jump-lane neighbours
};

struct Species {
  Id id = kNone;
  std::string name;
  Id homeworld = kNone;
  // Biology comes from homeworld conditions.
  double prefTempK = 288, tempTolK = 35, prefGravity = 1.0;
  double growthRate = 0.02;    // per year at low density
  double techAptitude = 1.0;   // speed toward spaceflight
  double techProgress = 0.0;   // reaches 1.0 at spaceflight
  bool spacefaring = false;
  Id empire = kNone;
};

struct Colony {
  Id id = kNone;
  Id body = kNone;
  Id species = kNone;
  Id empire = kNone;  // kNone for a pre-spaceflight homeworld
  double population = 0;  // millions
  std::int64_t foundedDay = 0;
};

struct Empire {
  Id id = kNone;
  std::string name;
  Id species = kNone;
  Id capital = kNone;  // colony id
  std::vector<Id> colonies;
  double minerals = 0;
  double expansionDrive = 0.5;  // 0..1, a stand-in for culture until milestone 2
  std::int64_t foundedDay = 0;
  int populationMilestone = 0;
};

struct ColonyShip {
  Id empire = kNone;
  Id targetBody = kNone;
  std::vector<Id> route;  // systems, from origin to destination
  std::size_t leg = 0;
  double progress = 0;  // 0..1 along the current lane
  double colonists = 0;
  bool done = false;
};

struct World {
  std::uint64_t seed = 0;
  std::int64_t day = 0;
  std::vector<StarSystem> systems;
  std::vector<Body> bodies;
  std::vector<Species> species;
  std::vector<Colony> colonies;
  std::vector<Empire> empires;
  std::vector<ColonyShip> ships;
  EventLog log;

  std::string nameOf(EntityRef ref) const;
};

std::string formatDate(std::int64_t day);
const char* starClassName(StarClass c);
double distance(const StarSystem& a, const StarSystem& b);
double habitability(const Species& s, const Body& b);  // 0..1
