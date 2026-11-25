#pragma once

#include <optional>
#include <string>
#include <vector>

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
//   yearly  - spaceflight emergence, expansion decisions, milestones
//             (later: politics, figures' life events, culture drift)
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

  void dailyTick();
  void monthlyTick();
  void yearlyTick();

  void checkEmergence();
  void foundEmpire(Species& s);
  void planExpansion(Empire& e);
  std::optional<Target> chooseTarget(const Empire& e) const;
  bool isTargeted(Id body) const;
  void arrive(const ColonyShip& ship);
  void checkMilestones(Empire& e);

  void emit(EventType type, Importance importance, std::string text, std::vector<EntityRef> refs);

  World& w_;
  SimConfig cfg_;
  Rng rng_;
};
