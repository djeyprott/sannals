#include "world.h"

#include <cmath>
#include <cstdio>

std::string World::nameOf(EntityRef ref) const {
  switch (ref.kind) {
    case EntityKind::System: return systems[static_cast<std::size_t>(ref.id)].name;
    case EntityKind::Body: return bodies[static_cast<std::size_t>(ref.id)].name;
    case EntityKind::Species: return species[static_cast<std::size_t>(ref.id)].name;
    case EntityKind::Empire: return empires[static_cast<std::size_t>(ref.id)].name;
    case EntityKind::Colony: {
      const Colony& c = colonies[static_cast<std::size_t>(ref.id)];
      return "the colony on " + bodies[static_cast<std::size_t>(c.body)].name;
    }
  }
  return "?";
}

std::string formatDate(std::int64_t day) {
  const long long year = day / kDaysPerYear;
  const int month = static_cast<int>((day % kDaysPerYear) / kDaysPerMonth);
  const int dayOfMonth = static_cast<int>(day % kDaysPerMonth);
  char buf[32];
  std::snprintf(buf, sizeof buf, "%04lld.%02d.%02d", year, month + 1, dayOfMonth + 1);
  return buf;
}

const char* starClassName(StarClass c) {
  switch (c) {
    case StarClass::O: return "O";
    case StarClass::B: return "B";
    case StarClass::A: return "A";
    case StarClass::F: return "F";
    case StarClass::G: return "G";
    case StarClass::K: return "K";
    case StarClass::M: return "M";
  }
  return "?";
}

double distance(const StarSystem& a, const StarSystem& b) { return std::hypot(a.x - b.x, a.y - b.y); }

double habitability(const Species& s, const Body& b) {
  switch (b.kind) {
    case BodyKind::GasGiant: return 0.0;
    case BodyKind::AsteroidBelt: return 0.01;  // habitats only
    case BodyKind::IceWorld: return 0.02;
    case BodyKind::Rocky: break;
  }
  if (!b.atmosphere) return 0.03;  // domed settlements
  const double dt = (b.temperatureK - s.prefTempK) / s.tempTolK;
  const double dg = (b.gravity - s.prefGravity) / 0.6;
  return std::exp(-(dt * dt + dg * dg));
}
