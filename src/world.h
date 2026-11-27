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
  double lifespanYears = 100;  // typical natural lifespan
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
  Id governor = kNone;  // figure
};

struct Empire {
  Id id = kNone;
  std::string name;
  Id species = kNone;
  Id capital = kNone;  // colony id
  std::vector<Id> colonies;
  double minerals = 0;
  double expansionDrive = 0.5;  // 0..1, the culture's appetite for expansion
  std::int64_t foundedDay = 0;
  int populationMilestone = 0;

  // Politics
  std::string government;    // "Hegemony", "Republic", ...
  bool hereditary = true;    // succession by bloodline, or by council election
  Id ruler = kNone;          // figure
  Id lastRuler = kNone;      // most recent ruler to leave office, for succession
  std::int64_t rulerSinceDay = 0;
  Id admiral = kNone;        // figure: High Admiral
  double legitimacy = 70.0;  // 0..100: how accepted the current ruler is
  std::vector<Id> houses;    // organizations, including extinct ones
};

// ---- People ------------------------------------------------------------------

enum class Role { None, Ruler, Governor, Admiral };

// Things that happened to a figure that colour how they feel about someone.
enum class MemoryKind {
  PassedOver,     // someone else got the office they wanted
  Favoured,       // was appointed or championed by them
  Quarrel,        // a falling-out
  Friendship,     // a bond formed
  KinKilled,      // they killed a parent or child
  Overthrown,     // they overthrew this figure, or this figure's ruler-kin
  PlotAgainstMe,  // they plotted against this figure
  Spared,         // they could have executed this figure and didn't
};

struct Memory {
  std::int64_t day;
  MemoryKind kind;
  Id about;   // figure
  int weight;  // opinion change it caused
};

struct Opinion {
  Id of;         // figure
  double value;  // -100..100
};

struct Figure {
  Id id = kNone;
  std::string name;
  Id species = kNone;
  Id empire = kNone;
  Id house = kNone;  // organization, or kNone for a commoner
  Id parent = kNone;
  std::vector<Id> children;
  std::int64_t bornDay = 0;
  std::int64_t diedDay = -1;  // -1 while alive

  // Talents, 0..20, fixed at birth.
  int administration = 0, military = 0, intrigue = 0;
  // Temperament, 0..1.
  double ambition = 0.5, loyalty = 0.5, ruthlessness = 0.5;

  Role role = Role::None;
  Id seat = kNone;  // colony governed, when role is Governor
  std::int64_t lastPlotDay = -1000000;  // plotters lie low for years after a try

  std::vector<Opinion> opinions;
  std::vector<Memory> memories;
  std::vector<Id> rivals;  // declared rivalries, both sides record them

  bool alive() const { return diedDay < 0; }
};

enum class OrgKind { NobleHouse };

struct Organization {
  Id id = kNone;
  OrgKind kind = OrgKind::NobleHouse;
  std::string name;     // "House Vessar"
  std::string surname;  // "Vessar", carried by members
  Id empire = kNone;
  Id founder = kNone;
  std::int64_t foundedDay = 0;
  std::int64_t endedDay = -1;  // set when the last member dies
  std::vector<Id> members;     // everyone ever born into or joining it
  double prestige = 0;
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
  std::vector<Figure> figures;
  std::vector<Organization> organizations;
  EventLog log;

  std::string nameOf(EntityRef ref) const;
};

std::string formatDate(std::int64_t day);
int ageInYears(const Figure& f, std::int64_t day);  // age at day, or at death
const char* roleName(Role r);
const char* starClassName(StarClass c);
double distance(const StarSystem& a, const StarSystem& b);
double habitability(const Species& s, const Body& b);  // 0..1
