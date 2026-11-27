#pragma once

#include <cstdint>
#include <vector>

#include "world.h"

// Who claims each patch of space. Every settled system projects a claim of a
// few light-years; bigger and capital systems reach further. Where claims
// overlap, the empire with the relatively closer claim wins, so borders fall
// between empires the way a weighted Voronoi diagram would place them.
//
// Lives in the core rather than the viewer because later milestones (border
// friction, diplomacy, "borders over time" maps) will read it too.
struct Territory {
  double originX = 0, originY = 0;  // world position of cell (0, 0)'s corner
  double cellSize = 1;              // light-years per cell
  int width = 0, height = 0;
  std::vector<Id> owner;  // width * height, row-major; kNone for unclaimed

  Id at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return kNone;
    return owner[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
  }
};

// Cheap fingerprint of everything the territory depends on. Recompute the
// territory only when this changes.
std::uint64_t territorySignature(const World& w);

// maxCells is the resolution along the galaxy's longer side.
Territory computeTerritory(const World& w, int maxCells = 768);
