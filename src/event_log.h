#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ids.h"

// Events are stored as structured data, not finished prose: the text holds
// placeholders like {0}, {1} that point at entity references. That keeps every
// name in the chronicle linkable to its legends page later.

enum class EntityKind { System, Body, Species, Empire, Colony, Figure, Organization };

struct EntityRef {
  EntityKind kind;
  Id id;
};

enum class EventType {
  EmpireFounded,
  ColonyShipLaunched,
  ColonyFounded,
  ColonyShipLost,
  DepositDepleted,
  PopulationMilestone,
  // People
  FigureBorn,
  FigureDied,
  Appointed,
  Succession,
  Coup,
  PlotFailed,
  Rivalry,
  HouseFounded,
  HouseExtinct,
};

enum class Importance { Minor = 0, Notable = 1, Major = 2 };

struct Event {
  std::int64_t day;
  EventType type;
  Importance importance;
  std::string text;  // with {n} placeholders into refs
  std::vector<EntityRef> refs;
};

class EventLog {
 public:
  using Listener = std::function<void(const Event&)>;

  void add(Event e);
  const std::vector<Event>& events() const { return events_; }
  void setListener(Listener listener) { listener_ = std::move(listener); }

 private:
  std::vector<Event> events_;
  Listener listener_;
};

enum class LinkStyle { Plain, Markdown };
using NameResolver = std::function<std::string(EntityRef)>;

// One run of event text: plain words (ref == -1) or an entity name (ref = index into Event::refs).
struct EventSegment {
  std::string text;
  int ref = -1;
};

const char* entityPath(EntityKind kind);
std::vector<EventSegment> segmentEvent(const Event& e, const NameResolver& nameOf);
std::string renderEvent(const Event& e, const NameResolver& nameOf, LinkStyle style);
