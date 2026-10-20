// Parity and speed of the GPU CRT filter against the CPU one, headless (no window).
//   gpu_crt_test [cellH=32] [crt=2] [out_prefix]   -> exit 1 if the two differ visibly
#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "stb_image_write.h"

#include "core/DataFile.hpp"
#include "render/GlyphAtlas.hpp"
#include "render/GpuCrt.hpp"
#include "ui/ScreenLayout.hpp"
#include "ui/WalkSession.hpp"

using namespace laby;
using clk = std::chrono::steady_clock;
static double msSince(clk::time_point t) { return std::chrono::duration<double, std::milli>(clk::now() - t).count(); }

static void writePng(const std::string& path, const Frame& f) {
  std::vector<unsigned char> rgb(f.px.size() * 3);
  for (size_t i = 0; i < f.px.size(); ++i) {
    rgb[i * 3] = static_cast<unsigned char>(f.px[i] >> 16);
    rgb[i * 3 + 1] = static_cast<unsigned char>(f.px[i] >> 8);
    rgb[i * 3 + 2] = static_cast<unsigned char>(f.px[i]);
  }
  stbi_write_png(path.c_str(), f.w, f.h, 3, rgb.data(), f.w * 3);
}

int main(int argc, char** argv) {
  const int cellH = (argc > 1 ? std::atoi(argv[1]) : 32) & ~1, crt = argc > 2 ? std::atoi(argv[2]) : 2;
  WalkSession session(42);
  session.handle(Key::Up);
  session.debugGoNextTo(TileClass::TREASURE);  // a chest in view: its cells exercise the object glow
  if (std::getenv("GPU_FIGHT")) { session.debugFight(false, ""); session.tick(1000); session.tick(2000); session.tick(4000); }  // a monster instead (its glow, and the enemy bar above it)
  if (const char* t = std::getenv("GPU_TITLE_MS")) {  // the start screen at this time of the storm instead of the game
    session.startTitle();
    session.tick(static_cast<std::uint64_t>(std::atoll(t)));
  }
  CellGrid grid(kGridCols, kGridRows);
  session.draw(grid, loadTheme(), "gpu_crt_test");
  const auto fonts = findFonts(dataDir() + "/fonts");
  std::vector<FontFace> faces(fonts.size());
  std::vector<const FontFace*> chain;
  for (size_t i = 0; i < fonts.size(); ++i)
    if (faces[i].load(fonts[i].path)) chain.push_back(&faces[i]);
  const Atlas atlas = buildGlyphAtlas(cellH / 2, cellH, chain, 11);
  Frame base;
  renderGrid(grid, atlas, base);  // with the torch drawn by the CPU: the reference
  Frame bare;
  renderGrid(grid, atlas, bare, false);  // without: the GPU draws the torch itself
  TorchGpu tg;
  TorchGpu rest;
  const bool torch = makeTorchGpu(grid, atlas, tg) && makeTorchGpu(grid, atlas, rest, true);
  const CrtParams p = withBackground(effectiveCrtParams(crt), grid.background());
  const PixelRect vp = viewportPixels(grid, atlas);
  const GlowMask glow = makeGlowMask(grid, atlas);
  std::printf("glow cells: %s\n", glow.any ? "yes" : "NONE (the test scene shows no object)");

  SDL_Init(SDL_INIT_VIDEO);
  SDL_GPUDevice* dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
  if (!dev) { std::fprintf(stderr, "no GPU device: %s\n", SDL_GetError()); return 2; }
  std::printf("GPU driver %s, frame %dx%d (%.1f Mpx)\n", SDL_GetGPUDeviceDriver(dev), base.w, base.h, base.w * double(base.h) / 1e6);
  auto gpu = GpuCrt::create(dev);
  if (!gpu) { std::fprintf(stderr, "GpuCrt::create failed: %s\n", SDL_GetError()); return 2; }

  Frame cpuOut = base, gpuOut;
  auto t = clk::now();
  applyCrt(cpuOut, cellH, p, vp, &glow);
  const double cpuMs = msSince(t);
  gpu->process(bare, cellH, p, vp, torch ? &tg : nullptr, true, torch ? &rest : nullptr, &glow);  // first call builds the textures
  const int N = 20;
  t = clk::now();
  for (int i = 0; i < N; ++i) gpu->process(bare, cellH, p, vp, torch ? &tg : nullptr, true, torch ? &rest : nullptr, &glow);
  SDL_WaitForGPUIdle(dev);
  const double gpuMs = msSince(t) / N;
  t = clk::now();
  gpu->readback(gpuOut);
  const double rbMs = msSince(t);

  double sum = 0;
  int maxd = 0;
  size_t maxAt = 0;
  size_t over = 0;
  for (size_t i = 0; i < cpuOut.px.size(); ++i)
    for (int sh : {0, 8, 16}) {
      const int d = std::abs(int((cpuOut.px[i] >> sh) & 255) - int((gpuOut.px[i] >> sh) & 255));
      sum += d;
      if (d > maxd) { maxd = d; maxAt = i; }
      over += d > 3;
    }
  std::printf("CPU applyCrt %.1f ms | GPU process (upload+filter, pipelined) %.2f ms | readback %.1f ms\n", cpuMs, gpuMs, rbMs);
  std::printf("diff: mean %.3f levels, max %d, >3 levels: %.4f%% of channels\n", sum / (3.0 * cpuOut.px.size()), maxd, 100.0 * over / (3.0 * cpuOut.px.size()));
  std::printf("worst pixel at (%d, %d) = cell (%d, %d): cpu %06x gpu %06x\n", int(maxAt % cpuOut.w), int(maxAt / cpuOut.w), int(maxAt % cpuOut.w) / atlas.cellW(),
              int(maxAt / cpuOut.w) / atlas.cellH(), cpuOut.px[maxAt] & 0xFFFFFF, gpuOut.px[maxAt] & 0xFFFFFF);
  if (argc > 3) { writePng(std::string(argv[3]) + "_cpu.png", cpuOut); writePng(std::string(argv[3]) + "_gpu.png", gpuOut); }
  gpu.reset();
  SDL_DestroyGPUDevice(dev);
  // edge pixels differ a little (the CPU blur is three box passes, the GPU one a gaussian): judge the whole picture
  return (sum / (3.0 * cpuOut.px.size()) > 0.5 || over > cpuOut.px.size() * 3 / 100) ? 1 : 0;
}
