#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "event_log.h"
#include "sim.h"
#include "world.h"

struct ImVec2;

// The viewer only reads the world and asks the sim to step. It never changes
// world state directly; god powers will be the one deliberate exception.
class Viewer {
 public:
  Viewer(World& world, Sim& sim, std::string title);

  // Call once per frame between ImGui::NewFrame() and ImGui::Render().
  void frame(float dt);

 private:
  struct Selection {
    EntityKind kind = EntityKind::System;
    Id id = kNone;
    bool valid() const { return id != kNone; }
  };

  // Simulation clock
  void advance(float dt);

  // Panels
  void drawMap();
  void drawControls();
  void drawEventFeed();
  void drawEmpires();
  void drawDetails();
  void drawSystemDetails(const StarSystem& sys);
  void drawBodyDetails(const Body& body);
  void drawSpeciesDetails(const Species& sp);
  void drawEmpireDetails(const Empire& e);
  void drawHistory();

  // Helpers
  void select(EntityRef ref);
  void focusOn(EntityRef ref);
  void entityLink(EntityRef ref);
  void eventRow(const Event& e, std::size_t uid);
  bool eventMentionsSelection(const Event& e) const;
  Id systemOf(EntityRef ref) const;
  void fitView(float width, float height);
  void recomputeOwnership();
  double empirePopulation(const Empire& e) const;

  World& w_;
  Sim& sim_;
  std::string title_;
  NameResolver resolve_;

  // Clock
  bool paused_ = false;
  int speedIndex_ = 2;
  double dayAccumulator_ = 0.0;

  // Map camera, in light-years
  double camX_ = 0.0, camY_ = 0.0;
  double scale_ = 1.0;  // pixels per light-year
  bool viewFitted_ = false;
  bool mapDragged_ = false;
  bool showLanes_ = true;
  bool showShips_ = true;
  bool showLabels_ = true;
  std::vector<Id> systemOwner_;  // empire id per system, kNone if unclaimed

  // Event feed
  int feedMinImportance_ = 1;
  std::size_t feedScanned_ = 0;
  std::vector<std::size_t> feed_;  // indices into the event log
  bool feedFollow_ = true;

  Selection selection_;
};
