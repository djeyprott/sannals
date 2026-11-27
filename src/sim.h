#pragma once

#include <optional>
#include <string>
#include <vector>

#include "names.h"
#include "rng.h"
#include "world.h"

struct SimConfig {
  double shipSpeedLyPerDay = 0.5;
  double colonyShipCost = 200.0;   // minerals
  double colonistsPerShip = 50.0;  // millions
  int maxRouteJumps = 8;
};

// Advances the world one day at a time. Systems update on tiered ticks:
//   daily   - ship movement (later: detection, battles)
//   monthly - population growth, mining
//   yearly  - spaceflight emergence, people (births, deaths, offices,
//             succession, rivalries, plots), expansion, milestones
class Sim {
 public:
  explicit Sim(World& world, SimConfig config = {});

  void start();    // founds empires for species that begin already spacefaring
  void stepDay();

 private:
  struct Target {
    Id body;
    std::vector<Id> route;
  };

  // sim.cpp: worlds and ships
  void dailyTick();
  void monthlyTick();
  void yearlyTick();
  void checkEmergence();
  void foundEmpire(Species& s);
  void planExpansion(Empire& e);
  std::optional<Target> chooseTarget(const Empire& e, double drive) const;
  bool isTargeted(Id body) const;
  void arrive(const ColonyShip& ship);
  void checkMilestones(Empire& e);
  double effectiveDrive(const Empire& e) const;

  // sim_people.cpp: figures and houses
  void seedElite(Id empire);
  void peopleTick(Id empire);
  void deaths(Id empire);
  void births(Id empire);
  void fillOffices(Id empire);
  void socialize(Id empire);
  void plots(Id empire);
  void updateHouses(Id empire);

  Id newFigure(Id empire, Id house, Id parent, std::int64_t bornDay);
  Id raiseCommoner(Id empire);
  Id foundHouse(Id empire, Id founder);
  void kill(Id figure, Id killer, bool executed);
  void appoint(Id figure, Role role, Id seat);
  void vacate(Id figure);
  void succeed(Id empire, Id previous);
  Id chooseSuccessor(Id empire, Id previous, double& legitimacy, Id& slighted);
  void remember(Id who, MemoryKind kind, Id about, int weight);

  double opinion(Id who, Id of) const;
  bool isAdult(const Figure& f) const;
  bool isProminent(const Figure& f) const;
  double housePrestige(const Figure& f) const;
  std::vector<Id> livingAdults(Id empire) const;
  Id houseHead(Id house) const;

  void emit(EventType type, Importance importance, std::string text, std::vector<EntityRef> refs);

  World& w_;
  SimConfig cfg_;
  Rng rng_;
  Rng nameRng_;
  NameGen names_;  // must follow nameRng_, which it references
};
