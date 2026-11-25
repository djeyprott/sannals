#pragma once

#include <cstdint>

// Every entity lives in a vector inside World and is addressed by its index.
using Id = std::int32_t;
inline constexpr Id kNone = -1;
