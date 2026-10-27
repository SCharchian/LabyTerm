#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#include "core/DataFile.hpp"
#include "core/Tuning.hpp"
#include "render/Backend.hpp"
#include "render/FrameRenderer.hpp"
#include "render/GlyphAtlas.hpp"
#ifdef LABYTERM_GPU_CRT
#include "render/GpuCrt.hpp"
#endif

namespace laby {
namespace {

Key mapKey(SDL_Keycode k) {
  switch (k) {
    case SDLK_UP: return Key::Up;
    case SDLK_DOWN: return Key::Down;
    case SDLK_LEFT: return Key::Left;
    case SDLK_RIGHT: return Key::Right;
    case SDLK_RETURN: case SDLK_KP_ENTER: return Key::Enter;
    case SDLK_ESCAPE: return Key::Escape;
    case SDLK_BACKSPACE: return Key::Backspace;
    case SDLK_Z: return Key::A;
    case SDLK_X: return Key::B;
    default: return Key::Other;
  }
}

bool sameCrt(const CrtParams& a, const CrtParams& b) {
  return a.softness == b.softness && a.bloom == b.bloom && a.viewportBloom == b.viewportBloom && a.scanlines == b.scanlines && a.vignette == b.vignette && a.objectBloom == b.objectBloom && a.monsterBloom == b.monsterBloom && a.glowFloor == b.glowFloor;
}

// Owns the window and draws frames itself: the cell grid is rendered on the CPU with our own glyph
// atlas, optionally run through the CRT filter, and shown as one texture. (It is a turn-based game,
// so frames are only re-rendered when something changed.)
class SdlBackend final : public Backend {
 public:
  SdlBackend(int w, int h, const char* title) : gridW_(w), gridH_(h), fonts_(findFonts(dataDir() + "/fonts")) {
    if (const char* e = std::getenv("LABYTERM_CRT")) crt_ = std::clamp(std::atoi(e), 0, 3);
    if (const char* e = std::getenv("LABYTERM_TEXT")) textSixths_ = std::clamp(std::atoi(e), 6, 12);
    if (fonts_.empty())
      std::fprintf(stderr, "labyterm: no TrueType font found; text will be blank (block/box/mosaic glyphs still work)\n");
    loadFont(0);
    cellH_ = 12;  // opens at the quarter-screen size of the design grid; auto-fit adjusts on the first resize event
    if (const char* e = std::getenv("LABYTERM_CELL")) cellH_ = startCell_ = std::clamp(std::atoi(e) & ~1, 8, 96);  // a launcher picks the window size
    snap_ = std::getenv("LABYTERM_SNAP") != nullptr;
    atlas_ = buildGlyphAtlas(cellH_ / 2, cellH_, faceChain(), textSixths_);
    atlasDirty_ = false;

    if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
    // SDL sizes are in "points"; ask for the real pixel size we want by dividing by the display scale
    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0) scale = 1.0f;
    // A launcher can ask for a border of theme background around the grid (window effects such as glow can cover the window's edge)
    if (const char* e = std::getenv("LABYTERM_MARGIN")) marginLogical_ = std::clamp(std::atoi(e), 0, 64);
    const int m0 = static_cast<int>(std::lround(marginLogical_ * scale));
    window_ = SDL_CreateWindow(title, static_cast<int>((gridW_ * atlas_.cellW() + 2 * m0) / scale),
                               static_cast<int>((gridH_ * cellH_ + 2 * m0) / scale),
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
    initGpu();  // the CRT filter on the GPU, if there is one (LABYTERM_CPU_CRT=1 forces the CPU filter)
    if (!gpuActive()) {
      renderer_ = SDL_CreateRenderer(window_, nullptr);
      if (!renderer_) throw std::runtime_error(std::string("SDL_CreateRenderer: ") + SDL_GetError());
      SDL_SetRenderVSync(renderer_, 1);
    }
    SDL_StartTextInput(window_);
  }

  ~SdlBackend() override {
#ifdef LABYTERM_GPU_CRT
    gpu_.reset();
    if (gpuDev_) {
      SDL_ReleaseWindowFromGPUDevice(gpuDev_, window_);
      SDL_DestroyGPUDevice(gpuDev_);
    }
#endif
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
  }

  bool pollEvent(Event& out) override {
    if (tuningWatcher_.poll(SDL_GetTicks())) frameWanted_ = true;
    SDL_Event e;
    while (SDL_PollEvent(&e))
      if (translate(e, out)) return true;
    return false;
  }

  bool waitEvent(Event& out, int timeoutMs) override {
    SDL_Event e;
    const bool got = timeoutMs < 0 ? SDL_WaitEvent(&e) : SDL_WaitEventTimeout(&e, timeoutMs);
    if (tuningWatcher_.poll(SDL_GetTicks())) frameWanted_ = true;  // a changed tuning file asks for a new frame
    return got && translate(e, out);
  }

  bool needsFrame() const override { return frameWanted_; }

  void present(const CellGrid& g) override {
    if (!g.sameAs(last_)) {
      if (!g.sameAsExceptTorch(last_)) baseDirty_ = true;  // (a torch-only change keeps the rendered picture)
      last_ = g;
      dirty_ = true;
    }
    frameWanted_ = false;
    const CrtParams crtNow = withBackground(effectiveCrtParams(crt_), last_.background());
    if (!sameCrt(crtNow, lastCrt_)) {
      lastCrt_ = crtNow;
      dirty_ = true;  // the tuning file changed the screen look
    }
    if (dirty_) {
      const Uint64 t0 = SDL_GetTicksNS();
      bool rendered = false;
      if (baseDirty_ || baseFrame_.w != last_.width() * atlas_.cellW() || baseFrame_.h != last_.height() * atlas_.cellH()) {
        renderGrid(last_, atlas_, baseFrame_, false);
        baseDirty_ = false;
        rendered = true;
      }
#ifdef LABYTERM_GPU_CRT
      if (gpu_) {
        // crt_ == 0 runs the same shaders with every effect at zero, which passes the frame through unchanged
        TorchGpu tg;  // the torch's darkness is drawn on the GPU, so a torch-only change uploads nothing
        TorchGpu rest;
        const GlowMask glowMask = rendered || uploadAgain_ ? makeGlowMask(last_, atlas_) : GlowMask{};  // (only read when the frame is uploaded)
        const bool torch = makeTorchGpu(last_, atlas_, tg) && makeTorchGpu(last_, atlas_, rest, true);
        gpuOk_ = gpu_->process(baseFrame_, cellH_, crt_ > 0 ? crtNow : CrtParams::level(0), viewportPixels(last_, atlas_), torch ? &tg : nullptr, rendered || uploadAgain_, torch ? &rest : nullptr, &glowMask);
        uploadAgain_ = !gpuOk_;
      } else
#endif
      {
        frame_ = baseFrame_;
        renderTorch(last_, atlas_, frame_);
        const GlowMask glowMask = makeGlowMask(last_, atlas_);
        if (crt_ > 0) applyCrt(frame_, cellH_, crtNow, viewportPixels(last_, atlas_), &glowMask);
        if (!texture_ || texW_ != frame_.w || texH_ != frame_.h) {
          if (texture_) SDL_DestroyTexture(texture_);
          texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, frame_.w, frame_.h);
          texW_ = frame_.w;
          texH_ = frame_.h;
        }
        SDL_UpdateTexture(texture_, nullptr, frame_.px.data(), frame_.w * 4);
      }
      dirty_ = false;
      if (std::getenv("LABYTERM_DEBUG")) std::fprintf(stderr, "frame rendered in %.1f ms (%s filter)\n", static_cast<double>(SDL_GetTicksNS() - t0) / 1e6, gpuActive() ? "GPU" : "CPU");
    }
#ifdef LABYTERM_GPU_CRT
    if (gpu_) {
      showGpu();
      return;
    }
#endif
    int ow = 0, oh = 0;
    SDL_GetCurrentRenderOutputSize(renderer_, &ow, &oh);
    const Rgb m = last_.background();
    SDL_SetRenderDrawColor(renderer_, m.r, m.g, m.b, 255);
    SDL_RenderClear(renderer_);
    // 1:1 when it fits (the atlas was sized to fit); only a forced oversize cell is scaled down
    const float k = std::min(1.0f, std::min(static_cast<float>(ow) / static_cast<float>(texW_), static_cast<float>(oh) / static_cast<float>(texH_)));
    SDL_SetTextureScaleMode(texture_, k < 1.0f ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    const float dw = static_cast<float>(texW_) * k, dh = static_cast<float>(texH_) * k;
    const SDL_FRect dst{std::floor((static_cast<float>(ow) - dw) / 2), std::floor((static_cast<float>(oh) - dh) / 2), dw, dh};
    SDL_RenderTexture(renderer_, texture_, nullptr, &dst);
    SDL_RenderPresent(renderer_);
  }

  std::uint64_t ticksMs() const override { return SDL_GetTicks(); }

  bool hasFocus() const override { return (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0; }

  FontInfo fontInfo() const override {
    FontInfo f;
    f.name = face_.loaded() ? face_.name() : "(none: synthesized glyphs only)";
    f.fontIndex = fontIndex_;
    f.fontCount = static_cast<int>(fonts_.size()) + 1;
    f.cellW = cellH_ / 2;
    f.cellH = cellH_;
    f.autoFit = forcedH_ == 0;
    SDL_GetWindowSizeInPixels(window_, &f.windowW, &f.windowH);
    f.displayScale = SDL_GetWindowDisplayScale(window_);
    f.crtLevel = crt_;
    f.textSixths = textSixths_;
    return f;
  }

  void nextFont() override {
    loadFont((fontIndex_ + 1) % (static_cast<int>(fonts_.size()) + 1));
    rebuild(cellH_);
  }

  void setCellHeight(int px) override {
    forcedH_ = px <= 0 ? 0 : std::clamp(px & ~1, 8, 96);
    if (forcedH_ == 0) autoFit();
    else rebuild(forcedH_);
  }

  int textSixths() const override { return textSixths_; }
  void setTextSixths(int n) override {
    textSixths_ = std::clamp(n, 6, 12);
    atlasDirty_ = true;
    rebuild(cellH_);
  }

  int crtLevel() const override { return crt_; }
  void setCrtLevel(int level) override {
    crt_ = std::clamp(level, 0, 3);
    dirty_ = true;
  }

 private:
  bool gpuActive() const {
#ifdef LABYTERM_GPU_CRT
    return gpu_ != nullptr;
#else
    return false;
#endif
  }

  void initGpu() {
#ifdef LABYTERM_GPU_CRT
    if (std::getenv("LABYTERM_CPU_CRT")) return;
    gpuDev_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
    if (gpuDev_ && SDL_ClaimWindowForGPUDevice(gpuDev_, window_)) {
      // A frame is drawn only when something changed, a few times a second at most: no need to wait for the display's refresh (that wait costs CPU)
      if (SDL_WindowSupportsGPUPresentMode(gpuDev_, window_, SDL_GPU_PRESENTMODE_MAILBOX)) SDL_SetGPUSwapchainParameters(gpuDev_, window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_MAILBOX);
      gpu_ = GpuCrt::create(gpuDev_);
    }
    if (!gpu_ && gpuDev_) {
      std::fprintf(stderr, "labyterm: GPU filter unavailable (%s); using the CPU filter\n", SDL_GetError());
      SDL_ReleaseWindowFromGPUDevice(gpuDev_, window_);
      SDL_DestroyGPUDevice(gpuDev_);
      gpuDev_ = nullptr;
    }
#endif
  }

#ifdef LABYTERM_GPU_CRT
  // Shows the filtered frame in the window: 1:1 and centred when it fits, scaled down when a forced cell size is oversize.
  void showGpu() {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpuDev_);
    if (!cmd) return;
    SDL_GPUTexture* swap = nullptr;
    Uint32 sw = 0, sh = 0;
    if (gpuOk_ && SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window_, &swap, &sw, &sh) && swap) {
      const float k = std::min(1.0f, std::min(static_cast<float>(sw) / static_cast<float>(gpu_->width()), static_cast<float>(sh) / static_cast<float>(gpu_->height())));
      const float dw = static_cast<float>(gpu_->width()) * k, dh = static_cast<float>(gpu_->height()) * k;
      const Rgb m = last_.background();
      SDL_GPUBlitInfo bi{};
      bi.source.texture = gpu_->output();
      bi.source.w = static_cast<Uint32>(gpu_->width());
      bi.source.h = static_cast<Uint32>(gpu_->height());
      bi.destination.texture = swap;
      bi.destination.x = static_cast<Uint32>(std::floor((static_cast<float>(sw) - dw) / 2));
      bi.destination.y = static_cast<Uint32>(std::floor((static_cast<float>(sh) - dh) / 2));
      bi.destination.w = static_cast<Uint32>(dw);
      bi.destination.h = static_cast<Uint32>(dh);
      bi.load_op = SDL_GPU_LOADOP_CLEAR;
      bi.clear_color = {m.r / 255.0f, m.g / 255.0f, m.b / 255.0f, 1.0f};
      bi.filter = k < 1.0f ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
      SDL_BlitGPUTexture(cmd, &bi);
    }
    SDL_SubmitGPUCommandBuffer(cmd);  // also when minimized (no swapchain texture): a command buffer must always be submitted
  }
#endif

  // Turns an SDL event into an app event; returns false if it is not one the app handles (window events are
  // dealt with here and just mark that a new frame is needed).
  bool translate(const SDL_Event& e, Event& out) {
    switch (e.type) {
      case SDL_EVENT_QUIT:
        out = {Event::Type::Quit};
        return true;
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      case SDL_EVENT_WINDOW_RESIZED:
        if (forcedH_ == 0 && startCell_ == 0) autoFit();  // re-renders only if the cell size actually changes
        if (snap_) snapWindow();
        frameWanted_ = true;           // but the window must be shown again (margins moved)
        return false;
      case SDL_EVENT_WINDOW_FOCUS_GAINED:
      case SDL_EVENT_WINDOW_FOCUS_LOST:
      case SDL_EVENT_WINDOW_MINIMIZED:
      case SDL_EVENT_WINDOW_HIDDEN:
        out = Event{};
        out.type = Event::Type::Focus;
        out.focused = e.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        return true;
      case SDL_EVENT_WINDOW_EXPOSED:
      case SDL_EVENT_WINDOW_RESTORED:
      case SDL_EVENT_WINDOW_SHOWN:
        frameWanted_ = true;  // just show the existing frame again; nothing about the picture changed
        return false;
      case SDL_EVENT_KEY_DOWN: {
        const SDL_Keycode k = e.key.key;
        out = {Event::Type::KeyDown, mapKey(k), (k >= 0x20 && k < 0x7F) ? static_cast<char32_t>(k) : 0, e.key.repeat};
        return true;
      }
      case SDL_EVENT_KEY_UP: {
        const SDL_Keycode k = e.key.key;
        out = {Event::Type::KeyUp, mapKey(k), (k >= 0x20 && k < 0x7F) ? static_cast<char32_t>(k) : 0, false};
        return true;
      }
      case SDL_EVENT_TEXT_INPUT:
        if (e.text.text && *e.text.text) {
          out = {Event::Type::Text, Key::None, static_cast<unsigned char>(e.text.text[0])};
          return true;
        }
        return false;
      default: return false;
    }
  }

  void loadFont(int index) {
    fontIndex_ = index;
    atlasDirty_ = true;
    face_ = FontFace();
    if (index < static_cast<int>(fonts_.size())) face_.load(fonts_[static_cast<size_t>(index)].path);
    // the other fonts are fallbacks for symbols the chosen one lacks (loaded once)
    if (fallbacks_.empty())
      for (const FontCandidate& c : fonts_) {
        auto f = std::make_unique<FontFace>();
        if (f->load(c.path)) fallbacks_.push_back(std::move(f));
      }
  }

  // chosen font first, then the fallbacks (skipping the chosen font's own file)
  std::vector<const FontFace*> faceChain() const {
    std::vector<const FontFace*> chain;
    if (face_.loaded()) chain.push_back(&face_);
    for (const auto& f : fallbacks_)
      if (!face_.loaded() || f->path() != face_.path()) chain.push_back(f.get());
    return chain;
  }

  // The margin in physical pixels (the display scale is only known once the window is mapped, so ask each time).
  int marginPx() const { return static_cast<int>(std::lround(marginLogical_ * SDL_GetWindowDisplayScale(window_))); }

  void autoFit() {
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(window_, &pw, &ph);
    if (pw <= 0 || ph <= 0) return;
    pw -= 2 * marginPx();
    ph -= 2 * marginPx();
    if (pw <= 0 || ph <= 0) return;
    int fit = std::min(ph / gridH_, 2 * (pw / gridW_));  // tallest cell whose grid fits both ways
    fit = std::clamp(fit & ~1, 8, 96);
    rebuild(fit);
  }

  // Asks the compositor for exactly the grid at the current cell size. A Wayland client cannot resize its own mapped window,
  // but a compositor honors its size limits: pinning minimum = maximum = the wanted size makes it resize, and the pin is
  // released again once the window has that size (so the player can drag it on). Each window size is tried once, so a
  // compositor that refuses (a tiled window) is not asked again and again.
  void snapWindow() {
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(window_, &pw, &ph);
    const int wantW = gridW_ * (cellH_ / 2) + 2 * marginPx(), wantH = gridH_ * cellH_ + 2 * marginPx();
    if (std::abs(pw - wantW) <= 1 && std::abs(ph - wantH) <= 1) {  // one pixel of rounding is what a fractional display scale leaves
      startCell_ = 0;  // the requested starting size has been reached: from now on the window size decides the cell size
      snapTriedW_ = snapTriedH_ = 0;
      if (pinned_) {
        SDL_SetWindowMinimumSize(window_, 0, 0);
        SDL_SetWindowMaximumSize(window_, 0, 0);
        pinned_ = false;
      }
      return;
    }
    if (pw == snapTriedW_ && ph == snapTriedH_) return;
    snapTriedW_ = pw;
    snapTriedH_ = ph;
    const float density = SDL_GetWindowPixelDensity(window_);
    const int w = static_cast<int>(std::lround(wantW / density)), h = static_cast<int>(std::lround(wantH / density));
    SDL_SetWindowMinimumSize(window_, w, h);
    SDL_SetWindowMaximumSize(window_, w, h);
    pinned_ = true;
  }

  void rebuild(int cellH) {
    if (std::getenv("LABYTERM_DEBUG")) {
      int pw = 0, ph = 0;
      SDL_GetWindowSizeInPixels(window_, &pw, &ph);
      std::fprintf(stderr, "window %dx%d px (scale %.2f) -> cell %dx%d, grid %dx%d = %dx%d px, crt %d\n", pw, ph,
                   SDL_GetWindowDisplayScale(window_), cellH / 2, cellH, gridW_, gridH_, gridW_ * (cellH / 2), gridH_ * cellH, crt_);
    }
    if (cellH != cellH_ || atlasDirty_) {
      cellH_ = cellH;
      atlas_ = buildGlyphAtlas(cellH_ / 2, cellH_, faceChain(), textSixths_);
      atlasDirty_ = false;
      dirty_ = true;  // new glyphs: the frame has to be rendered again
      baseDirty_ = true;
    }
  }

  int gridW_, gridH_;
  std::vector<FontCandidate> fonts_;
  FontFace face_;
  std::vector<std::unique_ptr<FontFace>> fallbacks_;
  int fontIndex_ = 0;
  bool snap_ = false, pinned_ = false;
  int marginLogical_ = 0;  // LABYTERM_MARGIN: background kept free on every side of the grid
  int snapTriedW_ = 0, snapTriedH_ = 0, startCell_ = 0;
  int cellH_ = 12, forcedH_ = 0, crt_ = 2, textSixths_ = 11;
  bool atlasDirty_ = true, dirty_ = true, frameWanted_ = true;
  Atlas atlas_;
  Frame frame_, baseFrame_;  // baseFrame_: the grid rendered without the torch's darkness
  bool baseDirty_ = true, uploadAgain_ = true;  // uploadAgain_: the GPU has not got the current base frame yet
  CellGrid last_{1, 1};
  CrtParams lastCrt_{};
  TuningWatcher tuningWatcher_;  // the screen-look knobs are live-editable; checked at most twice a second
  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;
  int texW_ = 0, texH_ = 0;
#ifdef LABYTERM_GPU_CRT
  SDL_GPUDevice* gpuDev_ = nullptr;
  std::unique_ptr<GpuCrt> gpu_;
  bool gpuOk_ = false;
#endif
};

}  // namespace

std::unique_ptr<Backend> makeSdlBackend(int w, int h, const char* title) { return std::make_unique<SdlBackend>(w, h, title); }

}  // namespace laby
