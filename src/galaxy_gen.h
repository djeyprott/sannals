#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "world.h"

enum class GalaxySize { Cluster, Small, Medium, Large, Huge };

struct GalaxyParams {
  GalaxySize size = GalaxySize::Small;
  std::uint64_t seed = 1;
  double spacefaringShare = 0.4;  // share of species that start already in space
};

std::optional<GalaxySize> parseGalaxySize(const std::string& s);
const char* galaxySizeName(GalaxySize s);

// Builds the galaxy in layers: stars, bodies, jump lanes, then species.
World generateGalaxy(const GalaxyParams& params);
