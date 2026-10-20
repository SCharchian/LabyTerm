#include "render/FrameRenderer.hpp"

#include "core/Tuning.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <future>
#include <thread>

namespace laby {

void renderTorch(const CellGrid& g, const Atlas& atlas, Frame& out);

namespace {
// Where `im` lands when renderGrid draws it with cells `cw` x `ch` pixels: its top-left corner, the size of one image pixel, and the pixel
// rectangle it covers (clipped to its clip cells).
struct ImagePlace {
  float ox, oy, step;
  int x0, y0, x1, y1;
};
ImagePlace placeImage(const ImageRun& im, int cw, int ch) {
  const float spp = static_cast<float>(cw) / 2.0f;  // pixels per sub-pixel
  ImagePlace p;
  p.step = spp * im.scale / static_cast<float>(im.srcScale);  // destination pixels per image pixel
  p.ox = static_cast<float>(im.cx * cw) + static_cast<float>(im.sx) * spp;
  p.oy = static_cast<float>(im.cy * ch) + static_cast<float>(im.sy) * spp;  // rows are sub-pixel tall too: ch / 4 == cw / 2
  p.x0 = std::max(im.clipX0 * cw, static_cast<int>(std::floor(p.ox)));
  p.x1 = std::min(im.clipX1 * cw, static_cast<int>(std::ceil(p.ox + static_cast<float>(im.img->w) * p.step)));
  p.y0 = std::max(im.clipY0 * ch, static_cast<int>(std::floor(p.oy)));
  p.y1 = std::min(im.clipY1 * ch, static_cast<int>(std::ceil(p.oy + static_cast<float>(im.img->h) * p.step)));
  return p;
}
}  // namespace

void renderGrid(const CellGrid& g, const Atlas& atlas, Frame& out, bool withTorch) {
  const int cw = atlas.cellW(), ch = atlas.cellH();
  out.w = g.width() * cw;
  out.h = g.height() * ch;
  out.px.assign(static_cast<size_t>(out.w) * static_cast<size_t>(out.h), 0xFF000000u);
  auto at = [&](int x, int y) -> std::uint32_t& { return out.px[static_cast<size_t>(y) * static_cast<size_t>(out.w) + static_cast<size_t>(x)]; };
  const auto blend = [](std::uint32_t under, Rgb c, int a) {
    const int r = static_cast<int>((under >> 16) & 255), gg = static_cast<int>((under >> 8) & 255), b = static_cast<int>(under & 255);
    return 0xFF000000u | static_cast<std::uint32_t>(r + (c.r - r) * a / 255) << 16 | static_cast<std::uint32_t>(gg + (c.g - gg) * a / 255) << 8 |
           static_cast<std::uint32_t>(b + (c.b - b) * a / 255);
  };

  // 1. the cell grid: one tile per cell
  for (int cy = 0; cy < g.height(); ++cy)
    for (int cx = 0; cx < g.width(); ++cx) {
      const Cell& c = g.at(cx, cy);
      const std::uint8_t* tile = atlas.tile(c.ch);
      for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x) at(cx * cw + x, cy * ch + y) = blend(0xFF000000u | static_cast<std::uint32_t>(c.bg.r) << 16 | static_cast<std::uint32_t>(c.bg.g) << 8 | c.bg.b, c.fg, tile ? tile[y * cw + x] : 0);
    }

  // 2. smooth pictures (monsters, objects), sampled bilinearly so they can be any resolution
  for (const ImageRun& im : g.imageRuns()) {
    if (!im.img || im.img->empty()) continue;
    const Image8& I = *im.img;
    const ImagePlace pl = placeImage(im, cw, ch);
    const int x0 = pl.x0, y0 = pl.y0, x1 = pl.x1, y1 = pl.y1;
    const float step = pl.step, ox = pl.ox, oy = pl.oy;
    const auto sample = [&](const std::vector<std::uint8_t>& plane, float u, float v) {
      if (!im.bilinear) return static_cast<float>(plane[static_cast<size_t>(std::min(static_cast<int>(v), I.h - 1)) * static_cast<size_t>(I.w) + static_cast<size_t>(std::min(static_cast<int>(u), I.w - 1))]);
      const float fu = std::clamp(u - 0.5f, 0.0f, static_cast<float>(I.w - 1)), fv = std::clamp(v - 0.5f, 0.0f, static_cast<float>(I.h - 1));
      const int iu = static_cast<int>(fu), iv = static_cast<int>(fv), ju = std::min(iu + 1, I.w - 1), jv = std::min(iv + 1, I.h - 1);
      const float tu = fu - static_cast<float>(iu), tv = fv - static_cast<float>(iv);
      const auto P = [&](int x, int y) { return static_cast<float>(plane[static_cast<size_t>(y) * static_cast<size_t>(I.w) + static_cast<size_t>(x)]); };
      return (P(iu, iv) * (1 - tu) + P(ju, iv) * tu) * (1 - tv) + (P(iu, jv) * (1 - tu) + P(ju, jv) * tu) * tv;
    };
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x) {
        const float u = (static_cast<float>(x) + 0.5f - ox) / step, v = (static_cast<float>(y) + 0.5f - oy) / step;
        if (u < 0 || v < 0 || u >= static_cast<float>(I.w) || v >= static_cast<float>(I.h)) continue;
        const float a = sample(I.alpha, u, v) / 255.0f;
        if (a <= 0.003f) continue;
        const float t = sample(I.tone, u, v) / 255.0f;
        const Rgb col{static_cast<std::uint8_t>(im.dark.r + (im.light.r - im.dark.r) * t + 0.5f), static_cast<std::uint8_t>(im.dark.g + (im.light.g - im.dark.g) * t + 0.5f),
                      static_cast<std::uint8_t>(im.dark.b + (im.light.b - im.dark.b) * t + 0.5f)};
        at(x, y) = blend(at(x, y), col, static_cast<int>(a * 255.0f + 0.5f));
      }
  }

  // 3. overlay text at the text pitch, blended over everything
  const Atlas& ta = atlas.textAtlas();
  const int tw = ta.cellW(), th = ta.cellH();
  for (const TextRun& t : g.textRuns()) {
    const std::uint8_t* tile = ta.tile(t.ch);
    if (!tile) continue;
    if (t.scale == 1.0f) {
      const int px = t.cx * cw + t.col * tw, py = t.cy * ch + t.row * th + static_cast<int>(std::lround(t.dy * ch));
      for (int y = 0; y < th; ++y) {
        if (py + y < 0 || py + y >= out.h) continue;
        for (int x = 0; x < tw; ++x) {
          const int a = tile[y * tw + x];
          if (a && px + x >= 0 && px + x < out.w) at(px + x, py + y) = blend(at(px + x, py + y), t.fg, a);
        }
      }
      continue;
    }
    // a bigger glyph: the tile resampled (bilinear) to scale x its size, at scaled pitch
    const int sw = static_cast<int>(std::lround(tw * t.scale)), sh = static_cast<int>(std::lround(th * t.scale));
    const int px = t.cx * cw + static_cast<int>(std::lround(t.col * tw * t.scale)), py = t.cy * ch + static_cast<int>(std::lround(t.row * th * t.scale)) + static_cast<int>(std::lround(t.dy * ch));
    for (int y = 0; y < sh; ++y) {
      if (py + y < 0 || py + y >= out.h) continue;
      const float fy = std::clamp((y + 0.5f) * th / sh - 0.5f, 0.0f, th - 1.0f);
      const int y0 = static_cast<int>(fy), y1 = std::min(y0 + 1, th - 1);
      const float wy = fy - y0;
      for (int x = 0; x < sw; ++x) {
        if (px + x < 0 || px + x >= out.w) continue;
        const float fx = std::clamp((x + 0.5f) * tw / sw - 0.5f, 0.0f, tw - 1.0f);
        const int x0 = static_cast<int>(fx), x1 = std::min(x0 + 1, tw - 1);
        const float wx = fx - x0;
        const float v = (tile[y0 * tw + x0] * (1 - wx) + tile[y0 * tw + x1] * wx) * (1 - wy) + (tile[y1 * tw + x0] * (1 - wx) + tile[y1 * tw + x1] * wx) * wy;
        const int a = static_cast<int>(v + 0.5f);
        if (a) at(px + x, py + y) = blend(at(px + x, py + y), t.fg, a);
      }
    }
  }
  if (withTorch) renderTorch(g, atlas, out);
}

CrtParams CrtParams::level(int n) {
  //               softness bloom viewportBloom scanlines vignette
  switch (n) {
    case 1: return {0.25f, 0.28f, 0.22f, 0.15f, 0.05f};
    case 2: return {0.45f, 0.50f, 0.38f, 0.30f, 0.10f};
    case 3: return {0.60f, 0.90f, 0.70f, 0.50f, 0.18f};
    default: return {0, 0, 0, 0, 0};
  }
}

namespace {

// Runs fn(yBegin, yEnd) over bands of rows on all cores. Frames are only rendered when something changed,
// so this is about keeping the delay between a key press and the redraw short at large window sizes.
void parallelRows(int h, const std::function<void(int, int)>& fn) {
  const int threads = std::max(1, std::min(static_cast<int>(std::thread::hardware_concurrency()), h / 32));
  if (threads <= 1) {
    fn(0, h);
    return;
  }
  std::vector<std::future<void>> jobs;
  for (int t = 0; t < threads; ++t) jobs.push_back(std::async(std::launch::async, fn, h * t / threads, h * (t + 1) / threads));
  for (auto& j : jobs) j.get();
}

// Separable box blur with clamped edges, `passes` times (3 passes approximate a gaussian)
void boxBlur(std::vector<float>& a, int w, int h, int r, int passes) {
  if (r < 1) return;
  std::vector<float> tmp(a.size());
  const float inv = 1.0f / static_cast<float>(2 * r + 1);
  for (int pass = 0; pass < passes; ++pass) {
    for (int y = 0; y < h; ++y) {  // horizontal
      const float* in = &a[static_cast<size_t>(y) * static_cast<size_t>(w)];
      float* o = &tmp[static_cast<size_t>(y) * static_cast<size_t>(w)];
      float sum = 0;
      for (int i = -r; i <= r; ++i) sum += in[std::clamp(i, 0, w - 1)];
      for (int x = 0; x < w; ++x) {
        o[x] = sum * inv;
        sum += in[std::min(x + r + 1, w - 1)] - in[std::max(x - r, 0)];
      }
    }
    for (int x = 0; x < w; ++x) {  // vertical
      float sum = 0;
      for (int i = -r; i <= r; ++i) sum += tmp[static_cast<size_t>(std::clamp(i, 0, h - 1)) * static_cast<size_t>(w) + static_cast<size_t>(x)];
      for (int y = 0; y < h; ++y) {
        a[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = sum * inv;
        sum += tmp[static_cast<size_t>(std::min(y + r + 1, h - 1)) * static_cast<size_t>(w) + static_cast<size_t>(x)] -
               tmp[static_cast<size_t>(std::max(y - r, 0)) * static_cast<size_t>(w) + static_cast<size_t>(x)];
      }
    }
  }
}

// Blur `src` (w x h) with a box of about `radius` pixels (3 passes), but computed on a copy shrunk by `d` in each
// direction and scaled back up bilinearly: a glow is low-frequency, and this is ~d*d times cheaper.
void blurDownscaled(const std::vector<float>& src, int w, int h, int radius, int d, std::vector<float>& out) {
  const int lw = (w + d - 1) / d, lh = (h + d - 1) / d;
  std::vector<float> small(static_cast<size_t>(lw) * static_cast<size_t>(lh), 0.0f);
  const float inv = 1.0f / static_cast<float>(d * d);
  for (int y = 0; y < h; ++y) {
    float* row = &small[static_cast<size_t>(y / d) * static_cast<size_t>(lw)];
    const float* in = &src[static_cast<size_t>(y) * static_cast<size_t>(w)];
    for (int x = 0; x < w; ++x) row[x / d] += in[x] * inv;
  }
  boxBlur(small, lw, lh, std::max(1, radius / d), 3);
  out.resize(src.size());
  // bilinear upsample; the column coordinates are the same for every row, so compute them once
  std::vector<int> cx0(static_cast<size_t>(w)), cx1(static_cast<size_t>(w));
  std::vector<float> ctx(static_cast<size_t>(w));
  for (int x = 0; x < w; ++x) {
    const float fx = std::clamp((static_cast<float>(x) + 0.5f) / static_cast<float>(d) - 0.5f, 0.0f, static_cast<float>(lw - 1));
    cx0[static_cast<size_t>(x)] = static_cast<int>(fx);
    cx1[static_cast<size_t>(x)] = std::min(static_cast<int>(fx) + 1, lw - 1);
    ctx[static_cast<size_t>(x)] = fx - static_cast<float>(static_cast<int>(fx));
  }
  for (int y = 0; y < h; ++y) {
    const float fy = std::clamp((static_cast<float>(y) + 0.5f) / static_cast<float>(d) - 0.5f, 0.0f, static_cast<float>(lh - 1));
    const int y0 = static_cast<int>(fy), y1 = std::min(y0 + 1, lh - 1);
    const float ty = fy - static_cast<float>(y0);
    const float* r0 = &small[static_cast<size_t>(y0) * static_cast<size_t>(lw)];
    const float* r1 = &small[static_cast<size_t>(y1) * static_cast<size_t>(lw)];
    float* o = &out[static_cast<size_t>(y) * static_cast<size_t>(w)];
    for (int x = 0; x < w; ++x) {
      const int a0 = cx0[static_cast<size_t>(x)], a1 = cx1[static_cast<size_t>(x)];
      const float tx = ctx[static_cast<size_t>(x)];
      const float top = r0[a0] + (r0[a1] - r0[a0]) * tx, bot = r1[a0] + (r1[a1] - r1[a0]) * tx;
      o[x] = top + (bot - top) * ty;
    }
  }
}

}  // namespace

void renderTorch(const CellGrid& g, const Atlas& atlas, Frame& out) {
  const CellGrid::Torch& tc = g.torch();
  if (!tc.on || !tuning().torch.enabled) return;
  const auto& k = tuning().torch;
  const int cw = atlas.cellW(), ch = atlas.cellH();
  // the picture darkens towards its edges, in chunky steps dithered at the sub-pixel grid of the mosaic art
  const int bw = std::max(1, cw / 2), bh = std::max(1, ch / 4);  // one sub-pixel of the art
  const int px0 = std::max(0, tc.rect.x0 * cw), py0 = std::max(0, tc.rect.y0 * ch), px1 = std::min(out.w, tc.rect.x1 * cw), py1 = std::min(out.h, tc.rect.y1 * ch);
  if (px1 <= px0 || py1 <= py0) return;
  const double hw = (tc.rect.x1 * cw - tc.rect.x0 * cw) / 2.0, hh = (tc.rect.y1 * ch - tc.rect.y0 * ch) / 2.0;
  const double mx = tc.rect.x0 * cw + hw, my = tc.rect.y0 * ch + hh;
  static const int kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  const double bands = std::max(1, k.bands), p = std::max(1.0, k.boxiness);
  const double maxDark = std::clamp(k.darkness + static_cast<double>(tc.pulse), 0.0, 1.0);
  const double r0 = k.radius + static_cast<double>(tc.breath), span = std::max(0.01, k.softness);
  const double cxs = static_cast<double>(tc.shift) * hw;
  const int bx0 = px0 / bw, bx1 = (px1 + bw - 1) / bw, by0 = py0 / bh, by1 = (py1 + bh - 1) / bh;
  // |x|^p per column and |y|^p per row of blocks: a block's distance is then one pow
  std::vector<double> colP(static_cast<size_t>(bx1 - bx0)), rowP(static_cast<size_t>(by1 - by0));
  for (int bx = bx0; bx < bx1; ++bx) colP[static_cast<size_t>(bx - bx0)] = std::pow(std::abs((bx * bw + bw / 2.0 - mx - cxs) / hw), p);
  for (int by = by0; by < by1; ++by) rowP[static_cast<size_t>(by - by0)] = std::pow(std::abs((by * bh + bh / 2.0 - my) / hh), p);
  std::vector<char> hole(static_cast<size_t>(g.width()) * static_cast<size_t>(g.height()), 0);  // cells a panel covers
  for (const auto& r : tc.holes)
    for (int y = std::max(0, r.y0); y < std::min(g.height(), r.y1); ++y)
      for (int x = std::max(0, r.x0); x < std::min(g.width(), r.x1); ++x) hole[static_cast<size_t>(y) * static_cast<size_t>(g.width()) + static_cast<size_t>(x)] = 1;
  const double invP = 1.0 / p;
  const int dr = tc.dark.r, dg = tc.dark.g, db = tc.dark.b;
  parallelRows(by1 - by0, [&](int rowA, int rowB) {
    for (int by = by0 + rowA; by < by0 + rowB; ++by)
      for (int bx = bx0; bx < bx1; ++bx) {
        if (hole[static_cast<size_t>(std::min(g.height() - 1, by * bh / ch)) * static_cast<size_t>(g.width()) + static_cast<size_t>(std::min(g.width() - 1, bx * bw / cw))]) continue;
        const double d = std::pow(colP[static_cast<size_t>(bx - bx0)] + rowP[static_cast<size_t>(by - by0)], invP);
        double t = std::clamp((d - r0) / span, 0.0, 1.0);
        if (t <= 0) continue;
        t = t * t * (3 - 2 * t);
        const double v = t * bands, lo = std::floor(v);
        const double thr = 0.5 + ((kBayer[by & 3][bx & 3] + 0.5) / 16.0 - 0.5) * k.dither;
        const double level = std::min(bands, lo + (v - lo > thr ? 1.0 : 0.0)) / bands;
        const int a = static_cast<int>(level * maxDark * 256.0 + 0.5);  // 0..256
        if (a <= 0) continue;
        const int ya = std::max(py0, by * bh), yb = std::min({py1, (by + 1) * bh}), xa = std::max(px0, bx * bw), xb = std::min({px1, (bx + 1) * bw});
        for (int y = ya; y < yb; ++y) {
          std::uint32_t* row = &out.px[static_cast<size_t>(y) * static_cast<size_t>(out.w)];
          for (int x = xa; x < xb; ++x) {
            const std::uint32_t u = row[x];
            const int r = static_cast<int>((u >> 16) & 255), gg = static_cast<int>((u >> 8) & 255), b = static_cast<int>(u & 255);
            row[x] = 0xFF000000u | static_cast<std::uint32_t>(r + (((dr - r) * a) >> 8)) << 16 | static_cast<std::uint32_t>(gg + (((dg - gg) * a) >> 8)) << 8 | static_cast<std::uint32_t>(b + (((db - b) * a) >> 8));
          }
        }
      }
  });
}

CrtParams effectiveCrtParams(int level) {
  CrtParams p = CrtParams::level(level);
  const auto& d = tuning().display;
  p.bloom *= static_cast<float>(d.glow);
  p.viewportBloom *= static_cast<float>(d.viewportGlow);
  p.viewportThreshold = static_cast<float>(d.viewportGlowThreshold);
  p.objectBloom = p.bloom * static_cast<float>(d.objectGlow);
  p.monsterBloom = p.bloom * static_cast<float>(d.monsterGlow);
  p.scanlines = std::min(1.0f, p.scanlines * static_cast<float>(d.scanlines));
  p.softness = std::min(1.0f, p.softness * static_cast<float>(d.beamSpread));
  p.vignette *= static_cast<float>(d.vignette);
  return p;
}

CrtParams withBackground(CrtParams p, Rgb bg) {
  p.glowFloor = static_cast<float>(std::max({bg.r, bg.g, bg.b})) / 255.0f + 0.04f;
  return p;
}

PixelRect viewportPixels(const CellGrid& g, const Atlas& a) {
  const auto& v = g.viewport();
  return PixelRect{v.x0 * a.cellW(), v.y0 * a.cellH(), v.x1 * a.cellW(), v.y1 * a.cellH()};
}

GlowMask makeGlowMask(const CellGrid& g, const Atlas& a) {
  GlowMask m;
  m.cols = g.width();
  m.rows = g.height();
  m.cellW = a.cellW();
  m.cellH = a.cellH();
  m.rgba.assign(static_cast<size_t>(m.cols) * static_cast<size_t>(m.rows) * 4, 0);
  for (size_t i = 0; i < g.cells().size(); ++i) {
    const Cell& c = g.cells()[i];
    if (!c.glow) continue;
    m.any = true;
    m.rgba[i * 4] = c.bg.r;
    m.rgba[i * 4 + 1] = c.bg.g;
    m.rgba[i * 4 + 2] = c.bg.b;
    m.rgba[i * 4 + 3] = c.glow > 1 ? GlowMask::kObjectText : GlowMask::kObject;
  }
  // monsters: every cell the picture touches (by its pixel extent, as renderGrid places it), keyed to the monster's own body color
  const int cw = a.cellW(), ch = a.cellH();
  for (const ImageRun& im : g.imageRuns()) {
    if (!im.glow || !im.img || im.img->empty()) continue;
    const ImagePlace pl = placeImage(im, cw, ch);
    if (pl.x1 <= pl.x0 || pl.y1 <= pl.y0) continue;
    const Image8& I = *im.img;
    // only the cells the picture's opaque pixels fall in (not its whole bounding box, which holds plain wall cells too)
    for (int v = 0; v < I.h; ++v) {
      const int y = static_cast<int>(pl.oy + (static_cast<float>(v) + 0.5f) * pl.step);
      if (y < pl.y0 || y >= pl.y1) continue;
      for (int u = 0; u < I.w; ++u) {
        if (I.alpha[static_cast<size_t>(v) * static_cast<size_t>(I.w) + static_cast<size_t>(u)] < 128) continue;
        const int x = static_cast<int>(pl.ox + (static_cast<float>(u) + 0.5f) * pl.step);
        if (x < pl.x0 || x >= pl.x1) continue;
        const int cx = x / cw, cy = y / ch;
        if (cx >= m.cols || cy >= m.rows) continue;
        std::uint8_t* px = &m.rgba[(static_cast<size_t>(cy) * static_cast<size_t>(m.cols) + static_cast<size_t>(cx)) * 4];
        if (px[3] == GlowMask::kObject) continue;
        m.any = true;
        px[0] = im.dark.r;
        px[1] = im.dark.g;
        px[2] = im.dark.b;
        px[3] = GlowMask::kMonster;
      }
    }
  }
  return m;
}

void applyCrt(Frame& f, int cellH, const CrtParams& p, const PixelRect& vp, const GlowMask* mask) {
  if (f.w <= 0 || f.h <= 0 || (p.softness <= 0 && p.bloom <= 0 && p.viewportBloom <= 0 && p.objectBloom <= 0 && p.monsterBloom <= 0 && p.scanlines <= 0 && p.vignette <= 0)) return;
  const size_t n = f.px.size();
  std::vector<float> ch[3];
  for (auto& c : ch) c.resize(n);
  parallelRows(f.h, [&](int y0, int y1) {
    for (size_t i = static_cast<size_t>(y0) * static_cast<size_t>(f.w); i < static_cast<size_t>(y1) * static_cast<size_t>(f.w); ++i) {
      ch[0][i] = static_cast<float>((f.px[i] >> 16) & 255) / 255.0f;
      ch[1][i] = static_cast<float>((f.px[i] >> 8) & 255) / 255.0f;
      ch[2][i] = static_cast<float>(f.px[i] & 255) / 255.0f;
    }
  });

  // beam spread: [s, 1-2s, s] along each raster line
  const float s = p.softness * 0.25f;
  if (s > 0) {
    for (auto& c : ch)
      parallelRows(f.h, [&](int y0, int y1) {
        std::vector<float> row(static_cast<size_t>(f.w));
        for (int y = y0; y < y1; ++y) {
          float* line = &c[static_cast<size_t>(y) * static_cast<size_t>(f.w)];
          std::copy(line, line + f.w, row.begin());
          for (int x = 0; x < f.w; ++x)
            line[x] = row[static_cast<size_t>(x)] * (1 - 2 * s) + s * (row[static_cast<size_t>(std::max(x - 1, 0))] + row[static_cast<size_t>(std::min(x + 1, f.w - 1))]);
        }
      });
  }

  // phosphor glow: blur what is bright and add it back. Two scales (a tight halo and a wide one) so thin text
  // strokes visibly glow. The source is weighted and thresholded per region: in the UI anything fairly bright
  // glows; inside the viewport only the brightest things (monsters, objects) do, so mid-bright walls are left
  // alone. The glow is added like a screen blend, so it can brighten but never blow a bar out to white.
  if (p.bloom > 0 || p.viewportBloom > 0 || p.objectBloom > 0 || p.monsterBloom > 0) {
    const float uiThreshold = std::max(0.30f, p.glowFloor);
    std::vector<float> weight(n, p.bloom), threshold(n, uiThreshold);
    for (int y = std::max(0, vp.y0); y < std::min(f.h, vp.y1); ++y)
      for (int x = std::max(0, vp.x0); x < std::min(f.w, vp.x1); ++x) {
        const size_t i = static_cast<size_t>(y) * static_cast<size_t>(f.w) + static_cast<size_t>(x);
        weight[i] = p.viewportBloom;
        threshold[i] = std::max(p.viewportThreshold, p.glowFloor);
      }
    const int tight = std::max(2, cellH / 6), wide = std::max(4, cellH / 2);
    // the three color channels are independent: glow them in parallel
    const bool objects = mask && mask->any && mask->cellW > 0 && mask->cellH > 0 && (p.objectBloom > 0 || p.monsterBloom > 0);
    const auto glowChannel = [&](std::vector<float>& c, int chan) {
      std::vector<float> src(n), a1, a2;
      for (size_t i = 0; i < n; ++i) src[i] = std::max(0.0f, c[i] - threshold[i]) * weight[i];
      if (objects)  // 3D objects: whatever lies above the cell's own background glows, however dim the object's color
        for (int y = 0; y < f.h; ++y) {
          const int cy = std::min(mask->rows - 1, y / mask->cellH);
          for (int x = 0; x < f.w; ++x) {
            const std::uint8_t* m = &mask->rgba[(static_cast<size_t>(cy) * static_cast<size_t>(mask->cols) + static_cast<size_t>(std::min(mask->cols - 1, x / mask->cellW))) * 4];
            if (!m[3]) continue;
            const size_t i = static_cast<size_t>(y) * static_cast<size_t>(f.w) + static_cast<size_t>(x);
            src[i] = std::max(0.0f, c[i] - static_cast<float>(m[chan]) / 255.0f - kObjectGlowFloor) * (m[3] == GlowMask::kObject ? p.objectBloom : m[3] == GlowMask::kObjectText ? p.objectBloom * kTextGlowBoost : p.monsterBloom);
          }
        }
      blurDownscaled(src, f.w, f.h, tight, 2, a1);
      blurDownscaled(src, f.w, f.h, wide, 4, a2);
      for (size_t i = 0; i < n; ++i) {
        const float g = std::clamp(a1[i] * 2.4f + a2[i] * 2.0f, 0.0f, 1.0f);
        c[i] += (1.0f - std::min(c[i], 1.0f)) * g;  // screen blend
      }
    };
    std::vector<std::future<void>> jobs;
    for (int k = 0; k < 3; ++k) jobs.push_back(std::async(std::launch::async, glowChannel, std::ref(ch[k]), k));
    for (auto& j : jobs) j.get();
  }

  // raster lines: every `pitch`-th row is a dark gap. Small cells have thin 1-2 px strokes, so the gaps
  // are eased off there (about 1/3 strength at 12 px tall cells, full strength from 20 px up).
  const int pitch = std::max(2, static_cast<int>(std::lround(cellH / 6.0)));
  const float gap = p.scanlines * std::clamp((static_cast<float>(cellH) - 8.0f) / 12.0f, 0.3f, 1.0f);
  const float cx = f.w * 0.5f, cy = f.h * 0.5f;
  parallelRows(f.h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
      const float line = (y % pitch == pitch - 1) ? 1.0f - gap : 1.0f;
      const float dy = (static_cast<float>(y) - cy) / cy;
      for (int x = 0; x < f.w; ++x) {
        const float dx = (static_cast<float>(x) - cx) / cx;
        const float v = 1.0f - p.vignette * 0.5f * (dx * dx + dy * dy);
        const size_t i = static_cast<size_t>(y) * static_cast<size_t>(f.w) + static_cast<size_t>(x);
        const float k = line * v;
        const auto q = [&](float c) { return static_cast<std::uint32_t>(std::clamp(c * k, 0.0f, 1.0f) * 255.0f + 0.5f); };
        f.px[i] = 0xFF000000u | q(ch[0][i]) << 16 | q(ch[1][i]) << 8 | q(ch[2][i]);
      }
    }
  });
}

}  // namespace laby
