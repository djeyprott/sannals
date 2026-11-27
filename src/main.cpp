// Stellar Annals - milestone 1: headless simulation.
//
// Generates a galaxy from a seed, runs it forward day by day, and prints the
// event log as it happens. Runs are endless by default; pass --years to stop
// (useful for batch testing).

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>

#include "galaxy_gen.h"
#include "sim.h"

namespace {

struct Options {
  std::uint64_t seed = 0;
  bool seedGiven = false;
  GalaxySize size = GalaxySize::Small;
  long long years = 0;  // 0 = run forever
  Importance minImportance = Importance::Notable;
  LinkStyle links = LinkStyle::Plain;
  bool summary = false;
};

void printUsage() {
  std::cerr << "Usage: stellar_annals [options]\n"
               "  --seed N             world seed (random if omitted; always printed)\n"
               "  --size S             cluster | small | medium | large | huge  (default small)\n"
               "  --years N            stop after N years (default: run forever)\n"
               "  --detail D           minor | notable | major  (default notable)\n"
               "  --links              print names as markdown links, e.g. [Kordan](system/12)\n"
               "  --summary            print a world summary when the run stops\n";
}

bool parseArgs(int argc, char** argv, Options& o) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
    if (arg == "--seed") {
      const char* v = value();
      if (!v) return false;
      o.seed = std::strtoull(v, nullptr, 10);
      o.seedGiven = true;
    } else if (arg == "--size") {
      const char* v = value();
      const auto size = v ? parseGalaxySize(v) : std::nullopt;
      if (!size) return false;
      o.size = *size;
    } else if (arg == "--years") {
      const char* v = value();
      if (!v) return false;
      o.years = std::strtoll(v, nullptr, 10);
    } else if (arg == "--detail") {
      const char* v = value();
      if (!v) return false;
      const std::string d = v;
      if (d == "minor") o.minImportance = Importance::Minor;
      else if (d == "notable") o.minImportance = Importance::Notable;
      else if (d == "major") o.minImportance = Importance::Major;
      else return false;
    } else if (arg == "--links") {
      o.links = LinkStyle::Markdown;
    } else if (arg == "--summary") {
      o.summary = true;
    } else {
      return false;
    }
  }
  return true;
}

void printSummary(const World& w) {
  std::printf("\n=== Summary at %s ===\n", formatDate(w.day).c_str());
  std::printf("Events recorded: %zu\n", w.log.events().size());

  std::vector<const Empire*> empires;
  for (const Empire& e : w.empires) empires.push_back(&e);
  auto population = [&w](const Empire& e) {
    double total = 0;
    for (Id c : e.colonies) total += w.colonies[static_cast<std::size_t>(c)].population;
    return total;
  };
  std::sort(empires.begin(), empires.end(),
            [&](const Empire* a, const Empire* b) { return population(*a) > population(*b); });

  std::printf("\n%-32s %8s %14s %12s  %s\n", "Empire", "Worlds", "Population (B)", "Minerals", "Ruler");
  for (const Empire* e : empires) {
    const std::string ruler = e->ruler == kNone ? "-" : w.figures[static_cast<std::size_t>(e->ruler)].name;
    std::printf("%-32s %8zu %14.1f %12.0f  %s\n", e->name.c_str(), e->colonies.size(), population(*e) / 1000.0,
                e->minerals, ruler.c_str());
  }
  std::printf("\nFigures recorded: %zu, noble houses: %zu\n", w.figures.size(), w.organizations.size());

  int preSpaceflight = 0;
  for (const Species& s : w.species) preSpaceflight += s.spacefaring ? 0 : 1;
  std::printf("\nSpecies still before spaceflight: %d of %zu\n", preSpaceflight, w.species.size());
}

}  // namespace

int main(int argc, char** argv) {
  Options opts;
  if (!parseArgs(argc, argv, opts)) {
    printUsage();
    return 1;
  }
  if (!opts.seedGiven) {
    std::random_device rd;
    opts.seed = (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
  }

  World world = generateGalaxy({opts.size, opts.seed});
  std::printf("Stellar Annals " STELLAR_VERSION " | seed %llu | %s galaxy | %zu systems, %zu bodies, %zu species\n\n",
              static_cast<unsigned long long>(opts.seed), galaxySizeName(opts.size), world.systems.size(),
              world.bodies.size(), world.species.size());

  const NameResolver resolve = [&world](EntityRef ref) { return world.nameOf(ref); };
  world.log.setListener([&](const Event& e) {
    if (e.importance < opts.minImportance) return;
    std::cout << formatDate(e.day) << "  " << renderEvent(e, resolve, opts.links) << std::endl;
  });

  Sim sim(world);
  sim.start();
  const long long endDay = opts.years > 0 ? opts.years * kDaysPerYear : -1;
  while (endDay < 0 || world.day < endDay) sim.stepDay();

  if (opts.summary) printSummary(world);
  return 0;
}
