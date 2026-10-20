#include "render/GpuCrt.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "crt_blur.frag.spv.inc"
#include "crt_bright.frag.spv.inc"
#include "crt_down.frag.spv.inc"
#include "crt_final.frag.spv.inc"
#include "fullscreen.vert.spv.inc"

#include "core/Tuning.hpp"

namespace laby {

namespace {

constexpr SDL_GPUTextureFormat kFrameFormat = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;  // little-endian 0xAARRGGBB
constexpr SDL_GPUTextureFormat kGlowFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;  // glow values are small: 8 bits would band

// std140 layouts of the shaders' uniform blocks
struct BrightUniforms { float viewport[4]; float bloom[4]; std::int32_t size[4]; float obj[4]; };
struct BlurUniforms { float dir[2]; float sigma; std::int32_t radius; };
struct FinalUniforms { float look[4]; std::int32_t size[4]; };

// A box of radius r run three times has the variance of a gaussian with this sigma: the CPU filter's look.
float boxSigma(int r) { return std::sqrt(3.0f * (static_cast<float>((2 * r + 1) * (2 * r + 1)) - 1.0f) / 12.0f); }

}  // namespace

struct GlowKey {
  CrtParams crt;
  PixelRect vp;
  int cellH, w, h;
  TorchGpu rest;
  bool operator==(const GlowKey& o) const {
    return std::memcmp(&crt, &o.crt, sizeof crt) == 0 && vp.x0 == o.vp.x0 && vp.y0 == o.vp.y0 && vp.x1 == o.vp.x1 && vp.y1 == o.vp.y1 && cellH == o.cellH && w == o.w && h == o.h && std::memcmp(&rest, &o.rest, sizeof rest) == 0;
  }
};

struct GpuCrt::Impl {
  GlowKey glowKey{};
  bool haveGlow = false;
  SDL_GPUDevice* dev = nullptr;
  SDL_GPUShader *vert = nullptr, *bright = nullptr, *down = nullptr, *blur = nullptr, *fin = nullptr;
  SDL_GPUGraphicsPipeline *pBright = nullptr, *pDown = nullptr, *pBlur = nullptr, *pFinal = nullptr;
  SDL_GPUSampler *nearest = nullptr, *linear = nullptr;
  SDL_GPUTransferBuffer *upload = nullptr, *download = nullptr;
  Uint32 transferSize = 0;
  SDL_GPUTexture *src = nullptr, *half[2] = {nullptr, nullptr}, *quarter[2] = {nullptr, nullptr};
  int lw2 = 0, lh2 = 0, lw4 = 0, lh4 = 0;
  SDL_GPUTexture* maskTex = nullptr;  // the glow mask: one texel per cell (1x1 and empty when there is none)
  SDL_GPUTransferBuffer* maskUpload = nullptr;
  int maskW = 0, maskH = 0, maskCellW = 0, maskCellH = 0;
  bool maskAny = false;

  // Uploads the mask (or an empty 1x1 one) in its own copy pass of `cmd`.
  bool uploadMask(SDL_GPUCommandBuffer* cmd, const GlowMask* mask) {
    const bool use = mask && mask->any && mask->cols > 0 && mask->rows > 0;
    const int w = use ? mask->cols : 1, h = use ? mask->rows : 1;
    if (!maskTex || w != maskW || h != maskH) {
      release(maskTex);
      if (maskUpload) SDL_ReleaseGPUTransferBuffer(dev, maskUpload);
      SDL_GPUTextureCreateInfo ci{};
      ci.type = SDL_GPU_TEXTURETYPE_2D;
      ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
      ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
      ci.width = static_cast<Uint32>(w);
      ci.height = static_cast<Uint32>(h);
      ci.layer_count_or_depth = 1;
      ci.num_levels = 1;
      maskTex = SDL_CreateGPUTexture(dev, &ci);
      SDL_GPUTransferBufferCreateInfo ti{};
      ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
      ti.size = static_cast<Uint32>(w) * static_cast<Uint32>(h) * 4;
      maskUpload = SDL_CreateGPUTransferBuffer(dev, &ti);
      maskW = w;
      maskH = h;
      if (!maskTex || !maskUpload) return false;
    }
    void* mapped = SDL_MapGPUTransferBuffer(dev, maskUpload, true);
    if (!mapped) return false;
    const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    if (use) std::memcpy(mapped, mask->rgba.data(), bytes);
    else std::memset(mapped, 0, bytes);
    SDL_UnmapGPUTransferBuffer(dev, maskUpload);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo from{maskUpload, 0, 0, 0};
    SDL_GPUTextureRegion to{};
    to.texture = maskTex;
    to.w = static_cast<Uint32>(w);
    to.h = static_cast<Uint32>(h);
    to.d = 1;
    SDL_UploadToGPUTexture(cp, &from, &to, true);
    SDL_EndGPUCopyPass(cp);
    maskAny = use;
    maskCellW = use ? mask->cellW : 0;
    maskCellH = use ? mask->cellH : 0;
    return true;
  }

  SDL_GPUShader* shader(const unsigned char* code, size_t len, SDL_GPUShaderStage stage, Uint32 samplers, Uint32 uniforms) {
    SDL_GPUShaderCreateInfo ci{};
    ci.code = code;
    ci.code_size = len;
    ci.entrypoint = "main";
    ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    ci.stage = stage;
    ci.num_samplers = samplers;
    ci.num_uniform_buffers = uniforms;
    return SDL_CreateGPUShader(dev, &ci);
  }

  SDL_GPUGraphicsPipeline* pipeline(SDL_GPUShader* frag, SDL_GPUTextureFormat format) {
    SDL_GPUColorTargetDescription target{};
    target.format = format;
    SDL_GPUGraphicsPipelineCreateInfo ci{};
    ci.vertex_shader = vert;
    ci.fragment_shader = frag;
    ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    ci.target_info.color_target_descriptions = &target;
    ci.target_info.num_color_targets = 1;
    return SDL_CreateGPUGraphicsPipeline(dev, &ci);
  }

  SDL_GPUTexture* target(int w, int h, SDL_GPUTextureFormat format, bool sampled = true) {
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = format;
    ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | (sampled ? SDL_GPU_TEXTUREUSAGE_SAMPLER : 0);
    ci.width = static_cast<Uint32>(w);
    ci.height = static_cast<Uint32>(h);
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    return SDL_CreateGPUTexture(dev, &ci);
  }

  void release(SDL_GPUTexture*& t) {
    if (t) SDL_ReleaseGPUTexture(dev, t);
    t = nullptr;
  }

  // One full-screen pass of `pipe` into `dst`, sampling `inputs`.
  void pass(SDL_GPUCommandBuffer* cmd, SDL_GPUGraphicsPipeline* pipe, SDL_GPUTexture* dst, std::initializer_list<std::pair<SDL_GPUTexture*, SDL_GPUSampler*>> inputs,
            const void* uniforms, Uint32 uniformSize, const void* uniforms1 = nullptr, Uint32 uniformSize1 = 0) {
    SDL_GPUColorTargetInfo ct{};
    ct.texture = dst;
    ct.load_op = SDL_GPU_LOADOP_DONT_CARE;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(rp, pipe);
    SDL_GPUTextureSamplerBinding binds[3]{};
    int n = 0;
    for (const auto& in : inputs) binds[n++] = {in.first, in.second};
    SDL_BindGPUFragmentSamplers(rp, 0, binds, static_cast<Uint32>(n));
    if (uniforms) SDL_PushGPUFragmentUniformData(cmd, 0, uniforms, uniformSize);
    if (uniforms1) SDL_PushGPUFragmentUniformData(cmd, 1, uniforms1, uniformSize1);
    SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
    SDL_EndGPURenderPass(rp);
  }

  bool resize(int w, int h, SDL_GPUTexture*& out) {
    lw2 = (w + 1) / 2;
    lh2 = (h + 1) / 2;
    lw4 = (w + 3) / 4;
    lh4 = (h + 3) / 4;
    release(src);
    release(out);
    for (auto& t : half) release(t);
    for (auto& t : quarter) release(t);
    src = target(w, h, kFrameFormat);  // also a target so the pass-through case could copy; cheap
    out = target(w, h, kFrameFormat);
    for (auto& t : half) t = target(lw2, lh2, kGlowFormat);
    for (auto& t : quarter) t = target(lw4, lh4, kGlowFormat);
    if (upload) SDL_ReleaseGPUTransferBuffer(dev, upload);
    if (download) SDL_ReleaseGPUTransferBuffer(dev, download);
    transferSize = static_cast<Uint32>(w) * static_cast<Uint32>(h) * 4;
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.size = transferSize;
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    upload = SDL_CreateGPUTransferBuffer(dev, &ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    download = SDL_CreateGPUTransferBuffer(dev, &ti);
    bool ok = src && out && upload && download;
    for (auto t : half) ok = ok && t;
    for (auto t : quarter) ok = ok && t;
    return ok;
  }

  ~Impl() {
    if (!dev) return;
    SDL_WaitForGPUIdle(dev);
    release(src);
    release(maskTex);
    if (maskUpload) SDL_ReleaseGPUTransferBuffer(dev, maskUpload);
    for (auto& t : half) release(t);
    for (auto& t : quarter) release(t);
    if (upload) SDL_ReleaseGPUTransferBuffer(dev, upload);
    if (download) SDL_ReleaseGPUTransferBuffer(dev, download);
    for (auto p : {pBright, pDown, pBlur, pFinal})
      if (p) SDL_ReleaseGPUGraphicsPipeline(dev, p);
    for (auto s : {vert, bright, down, blur, fin})
      if (s) SDL_ReleaseGPUShader(dev, s);
    if (nearest) SDL_ReleaseGPUSampler(dev, nearest);
    if (linear) SDL_ReleaseGPUSampler(dev, linear);
  }
};

std::unique_ptr<GpuCrt> GpuCrt::create(SDL_GPUDevice* device) {
  if (!device || !(SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV)) return nullptr;
  std::unique_ptr<GpuCrt> g(new GpuCrt);
  g->dev_ = device;
  g->impl_ = std::make_unique<Impl>();
  Impl& m = *g->impl_;
  m.dev = device;
  m.vert = m.shader(fullscreen_vert_spv, sizeof fullscreen_vert_spv, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
  m.bright = m.shader(crt_bright_frag_spv, sizeof crt_bright_frag_spv, SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 2);
  m.down = m.shader(crt_down_frag_spv, sizeof crt_down_frag_spv, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
  m.blur = m.shader(crt_blur_frag_spv, sizeof crt_blur_frag_spv, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
  m.fin = m.shader(crt_final_frag_spv, sizeof crt_final_frag_spv, SDL_GPU_SHADERSTAGE_FRAGMENT, 3, 2);
  if (!m.vert || !m.bright || !m.down || !m.blur || !m.fin) {
    std::fprintf(stderr, "labyterm: GPU CRT shaders failed: %s\n", SDL_GetError());
    return nullptr;
  }
  m.pBright = m.pipeline(m.bright, kGlowFormat);
  m.pDown = m.pipeline(m.down, kGlowFormat);
  m.pBlur = m.pipeline(m.blur, kGlowFormat);
  m.pFinal = m.pipeline(m.fin, kFrameFormat);
  SDL_GPUSamplerCreateInfo si{};
  si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  si.min_filter = si.mag_filter = SDL_GPU_FILTER_NEAREST;
  m.nearest = SDL_CreateGPUSampler(device, &si);
  si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
  m.linear = SDL_CreateGPUSampler(device, &si);
  if (!m.pBright || !m.pDown || !m.pBlur || !m.pFinal || !m.nearest || !m.linear) {
    std::fprintf(stderr, "labyterm: GPU CRT pipelines failed: %s\n", SDL_GetError());
    return nullptr;
  }
  return g;
}

GpuCrt::~GpuCrt() {
  if (impl_ && dev_) {
    SDL_WaitForGPUIdle(dev_);
    impl_->release(out_);
  }
}

bool GpuCrt::process(const Frame& f, int cellH, const CrtParams& p, const PixelRect& vp, const TorchGpu* torch, bool upload, const TorchGpu* rest, const GlowMask* mask) {
  if (upload && (f.w <= 0 || f.h <= 0)) return false;
  Impl& m = *impl_;
  if (upload && (f.w != w_ || f.h != h_)) {
    SDL_WaitForGPUIdle(dev_);
    if (!m.resize(f.w, f.h, out_)) return false;
    w_ = f.w;
    h_ = f.h;
  }
  if (!m.src) return false;
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
  if (!cmd) return false;
  if (upload) {
    void* mapped = SDL_MapGPUTransferBuffer(dev_, m.upload, true);
    if (!mapped) {
      SDL_CancelGPUCommandBuffer(cmd);
      return false;
    }
    std::memcpy(mapped, f.px.data(), m.transferSize);
    SDL_UnmapGPUTransferBuffer(dev_, m.upload);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo from{m.upload, 0, 0, 0};
    SDL_GPUTextureRegion to{};
    to.texture = m.src;
    to.w = static_cast<Uint32>(w_);
    to.h = static_cast<Uint32>(h_);
    to.d = 1;
    SDL_UploadToGPUTexture(cp, &from, &to, true);
    SDL_EndGPUCopyPass(cp);
  }
  if ((upload || !m.maskTex) && !m.uploadMask(cmd, mask)) {
    SDL_CancelGPUCommandBuffer(cmd);
    return false;
  }
  const float s = p.softness * 0.25f;
  const bool glow = p.bloom > 0 || p.viewportBloom > 0 || p.objectBloom > 0 || p.monsterBloom > 0;
  // The glow depends only on the frame and the CRT settings (the torch dims it in the final pass), so a step of the
  // torch's breathing reuses the blurred copies from the last call.
  TorchGpu none{};  // blk[3] == 0: no torch
  const TorchGpu& restT = rest ? *rest : none;
  GlowKey key{p, vp, cellH, w_, h_, restT};
  const bool glowFresh = upload || !m.haveGlow || !(key == m.glowKey);
  if (glow && glowFresh) {
    BrightUniforms bu{{static_cast<float>(std::max(0, vp.x0)), static_cast<float>(std::max(0, vp.y0)), static_cast<float>(vp.x1), static_cast<float>(vp.y1)},
                      {p.bloom, p.viewportBloom, p.viewportThreshold, s},
                      {w_, h_, m.maskAny ? m.maskCellW : 0, m.maskAny ? m.maskCellH : 0},
                      {p.objectBloom, kObjectGlowFloor, p.glowFloor, p.monsterBloom}};
    m.pass(cmd, m.pBright, m.half[0], {{m.src, m.nearest}, {m.maskTex, m.nearest}}, &bu, sizeof bu, &restT, sizeof restT);
    m.pass(cmd, m.pDown, m.quarter[0], {{m.half[0], m.linear}}, nullptr, 0);
    const int tight = std::max(2, cellH / 6), wide = std::max(4, cellH / 2);
    const int rTight = std::max(1, tight / 2), rWide = std::max(1, wide / 4);
    const auto blur2 = [&](SDL_GPUTexture* a, SDL_GPUTexture* b, int r) {  // a -> b -> a
      BlurUniforms bh{{1, 0}, boxSigma(r), static_cast<std::int32_t>(std::ceil(3 * boxSigma(r)))};
      BlurUniforms bv{{0, 1}, bh.sigma, bh.radius};
      m.pass(cmd, m.pBlur, b, {{a, m.linear}}, &bh, sizeof bh);
      m.pass(cmd, m.pBlur, a, {{b, m.linear}}, &bv, sizeof bv);
    };
    blur2(m.half[0], m.half[1], rTight);
    blur2(m.quarter[0], m.quarter[1], rWide);
  }
  m.glowKey = key;
  m.haveGlow = true;
  const int pitch = std::max(2, static_cast<int>(std::lround(cellH / 6.0)));
  const float gap = p.scanlines * std::clamp((static_cast<float>(cellH) - 8.0f) / 12.0f, 0.3f, 1.0f);
  FinalUniforms fu{{s, gap, p.vignette, glow ? 1.0f : 0.0f}, {w_, h_, pitch, 0}};
  const TorchGpu& tu = torch ? *torch : none;
  m.pass(cmd, m.pFinal, out_, {{m.src, m.nearest}, {m.half[0], m.linear}, {m.quarter[0], m.linear}}, &fu, sizeof fu, &tu, sizeof tu);
  return SDL_SubmitGPUCommandBuffer(cmd);
}

bool makeTorchGpu(const CellGrid& g, const Atlas& atlas, TorchGpu& o, bool rest) {
  const CellGrid::Torch& tc = g.torch();
  const auto& k = tuning().torch;
  if (!tc.on || !k.enabled) return false;
  const int cw = atlas.cellW(), ch = atlas.cellH();
  const int x0 = tc.rect.x0 * cw, y0 = tc.rect.y0 * ch, x1 = tc.rect.x1 * cw, y1 = tc.rect.y1 * ch;
  if (x1 <= x0 || y1 <= y0) return false;
  const float hw = static_cast<float>(x1 - x0) / 2.0f;
  const float rect[4] = {static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1), static_cast<float>(y1)};
  std::memcpy(o.rect, rect, sizeof rect);
  const float a[4] = {static_cast<float>(std::max(1, k.bands)), static_cast<float>(std::max(1.0, k.boxiness)), static_cast<float>(k.dither),
                      static_cast<float>(std::clamp(k.darkness + (rest ? 0.0 : static_cast<double>(tc.pulse)), 0.0, 1.0))};
  std::memcpy(o.a, a, sizeof a);
  const float b[4] = {static_cast<float>(k.radius + (rest ? 0.0 : static_cast<double>(tc.breath))), static_cast<float>(std::max(0.01, k.softness)), rest ? 0.0f : tc.shift * hw, 0.0f};
  std::memcpy(o.b, b, sizeof b);
  const float dk[4] = {tc.dark.r / 255.0f, tc.dark.g / 255.0f, tc.dark.b / 255.0f, 0.0f};
  std::memcpy(o.dark, dk, sizeof dk);
  o.blk[0] = std::max(1, cw / 2);
  o.blk[1] = std::max(1, ch / 4);
  o.blk[2] = static_cast<std::int32_t>(std::min<size_t>(32, tc.holes.size()));
  o.blk[3] = 1;
  std::memset(o.holes, 0, sizeof o.holes);
  for (int i = 0; i < o.blk[2]; ++i) {
    const auto& r = tc.holes[static_cast<size_t>(i)];
    const float h[4] = {static_cast<float>(r.x0 * cw), static_cast<float>(r.y0 * ch), static_cast<float>(r.x1 * cw), static_cast<float>(r.y1 * ch)};
    std::memcpy(o.holes[i], h, sizeof h);
  }
  return true;
}

bool GpuCrt::readback(Frame& out) {
  Impl& m = *impl_;
  if (!out_) return false;
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
  if (!cmd) return false;
  SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureRegion from{};
  from.texture = out_;
  from.w = static_cast<Uint32>(w_);
  from.h = static_cast<Uint32>(h_);
  from.d = 1;
  SDL_GPUTextureTransferInfo to{m.download, 0, 0, 0};
  SDL_DownloadFromGPUTexture(cp, &from, &to);
  SDL_EndGPUCopyPass(cp);
  SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  if (!fence) return false;
  SDL_WaitForGPUFences(dev_, true, &fence, 1);
  SDL_ReleaseGPUFence(dev_, fence);
  const void* mapped = SDL_MapGPUTransferBuffer(dev_, m.download, false);
  if (!mapped) return false;
  out.w = w_;
  out.h = h_;
  out.px.resize(static_cast<size_t>(w_) * static_cast<size_t>(h_));
  std::memcpy(out.px.data(), mapped, m.transferSize);
  SDL_UnmapGPUTransferBuffer(dev_, m.download);
  return true;
}

}  // namespace laby
