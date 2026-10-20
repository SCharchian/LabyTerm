#pragma once
#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <memory>

#include "render/FrameRenderer.hpp"

namespace laby {

// The CRT filter (applyCrt in FrameRenderer.cpp) on the GPU. The CPU still draws the cell grid into a Frame; this uploads
// it, runs beam spread, bloom (a thresholded half-resolution copy and a quarter-resolution one, each blurred), raster
// lines and vignette as fragment shaders, and leaves the finished picture in output(). The CPU filter stays as the
// fallback when no GPU device is available, and as the reference the parity test compares against.
// The torch's settings for the GPU pass (std140 layout of torch.frag's uniform block); see renderTorch for what they mean.
struct TorchGpu {
  float rect[4];
  float a[4];
  float b[4];
  float dark[4];
  std::int32_t blk[4];
  float holes[32][4];
};
// Fills `out` from the grid's torch settings; false if the torch is off (or has nothing to darken).
// `rest`: the torch as it is when calm and not breathing (what the cached glow is worked out from).
bool makeTorchGpu(const CellGrid& grid, const Atlas& atlas, TorchGpu& out, bool rest = false);

class GpuCrt {
 public:
  // nullptr if the shaders or pipelines cannot be created on this device.
  static std::unique_ptr<GpuCrt> create(SDL_GPUDevice* device);
  ~GpuCrt();

  // Uploads `frame` and filters it: submits its own command buffer. With `crt` all zero the frame passes through.
  // `torch` darkens the picture's edges first. `upload` false keeps the frame uploaded by the last call (when only the
  // torch or the CRT settings changed), so `frame` is not read then (unless the size changed: then it is uploaded anyway). The glow is worked out from the frame dimmed by `rest` and
  // reused until the frame, `rest` or the CRT settings change.
  // `glow`: the cells with 3D object ink (see GlowMask), uploaded with the frame; null = none.
  bool process(const Frame& frame, int cellH, const CrtParams& crt, const PixelRect& viewport, const TorchGpu* torch = nullptr, bool upload = true, const TorchGpu* rest = nullptr,
               const GlowMask* glow = nullptr);

  // The finished frame (frame-sized, B8G8R8A8), valid after process().
  SDL_GPUTexture* output() const { return out_; }
  int width() const { return w_; }
  int height() const { return h_; }

  // Copies the finished frame back to the CPU (tests and screenshots; stalls until the GPU is done).
  bool readback(Frame& out);

  struct Impl;

 private:
  GpuCrt() = default;
  std::unique_ptr<Impl> impl_;
  SDL_GPUDevice* dev_ = nullptr;
  SDL_GPUTexture* out_ = nullptr;
  int w_ = 0, h_ = 0;
};

}  // namespace laby
