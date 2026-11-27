#pragma once

#include <cstdint>

// Minimal OpenGL texture helpers, kept apart so the rest of the viewer
// only talks to Dear ImGui.
namespace gltex {

unsigned create();  // a linear-filtered, edge-clamped RGBA texture
void upload(unsigned texture, int width, int height, const std::uint32_t* rgba);
void destroy(unsigned texture);

}  // namespace gltex
