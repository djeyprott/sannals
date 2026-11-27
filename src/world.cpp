#include "world.h"

#include <cmath>
#include <cstdio>

std::string World::nameOf(EntityRef ref) const {
  switch (ref.kind) {
    case EntityKind::System: return systems[static_cast<std::size_t>(ref.id)].name;
    case EntityKind::Body: return bodies[static_cast<std::size_t>(ref.id)].name;
    case EntityKind::Species: return species[static_cast<std::size_t>(ref.id)].name;
    case EntityKind::Empire: return empires[static_cast<std::size_t>(ref.id)].name;
    case EntityKind::Figure: return figures[static_cast<std::size_t>(ref.id)].name;
    case EntityKind::Organization: return organizations[static_cast<std::size_t>(ref.id)].name;
    case EntityKind::Colony: {
      const Colony& c = colonies[static_cast<std::size_t>(ref.id)];
      return "the colony on " + bodies[static_cast<std::size_t>(c.body)].name;
    }
  }
  return "?";
}

std::string formatDate(std::int64_t day) {
  // Floor division so dates before year 0 (births of founding figures) read correctly.
  long long year = day / kDaysPerYear;
  if (day % kDaysPerYear < 0) --year;
  const std::int64_t inYear = day - year * kDaysPerYear;
  const int month = static_cast<int>(inYear / kDaysPerMonth);
  const int dayOfMonth = static_cast<int>(inYear % kDaysPerMonth);
  char buf[48];
  std::snprintf(buf, sizeof buf, "%04lld.%02d.%02d", year, month + 1, dayOfMonth + 1);
  return buf;
}

int ageInYears(const Figure& f, std::int64_t day) {
  const std::int64_t end = f.alive() ? day : f.diedDay;
  return static_cast<int>((end - f.bornDay) / kDaysPerYear);
}

const char* roleName(Role r) {
  switch (r) {
    case Role::None: return "None";
    case Role::Ruler: return "Ruler";
    case Role::Governor: return "Governor";
    case Role::Admiral: return "High Admiral";
  }
  return "?";
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
