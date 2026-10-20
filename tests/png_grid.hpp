#pragma once
// Test helper: render a CellGrid to a PNG with the same renderer (and optional CRT filter) the window uses.
#include <vector>

#include "stb_image_write.h"

#include "render/FrameRenderer.hpp"

inline void writeGridPng(const char* path, const laby::CellGrid& grid, const laby::Atlas& atlas, int crtLevel) {
  laby::Frame f;
  laby::renderGrid(grid, atlas, f);
  const laby::GlowMask glow = laby::makeGlowMask(grid, atlas);
  if (crtLevel > 0) laby::applyCrt(f, atlas.cellH(), laby::withBackground(laby::effectiveCrtParams(crtLevel), grid.background()), laby::viewportPixels(grid, atlas), &glow);
  std::vector<unsigned char> rgb(f.px.size() * 3);
  for (size_t i = 0; i < f.px.size(); ++i) {
    rgb[i * 3] = static_cast<unsigned char>(f.px[i] >> 16);
    rgb[i * 3 + 1] = static_cast<unsigned char>(f.px[i] >> 8);
    rgb[i * 3 + 2] = static_cast<unsigned char>(f.px[i]);
  }
  stbi_write_png(path, f.w, f.h, 3, rgb.data(), f.w * 3);
}
