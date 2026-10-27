#include "ui/TownArt.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <map>
#include <vector>

namespace laby {

namespace {

struct Sign {
  const char* image;
  int x, y;  // where the highlight sprite sits on the sign post picture
};
constexpr Sign kSigns[] = {{"Town/TempleSign_Inverted", 72, 3}, {"Town/BlacksmithSign_Inverted", 66, 28}, {"Town/BeastMartSign_Inverted", 78, 58},
                           {"Town/LabyrinthSign_Inverted", 79, 79}, {"Town/SaveSign_Inverted", 182, 84}};
constexpr const char* kSignNames[] = {"Temple", "Blacksmith", "BeastMart", "Labyrinth", "Save & Exit"};  // as written on the signs (the Text style prints them)
constexpr int kSquareLeft = 49;  // the sign post picture is 241 wide, the view 186: lose 49 at the left (the houses), 6 at the right

void box(Bitmap1& layer, int x0, int y0, int x1, int y1) {  // a 2 px outline
  for (int t = 0; t < 2; ++t)
    for (int x = x0; x <= x1; ++x) {
      layer.px[static_cast<size_t>(y0 + t) * static_cast<size_t>(layer.w) + static_cast<size_t>(x)] = Bitmap1::Light;
      layer.px[static_cast<size_t>(y1 - t) * static_cast<size_t>(layer.w) + static_cast<size_t>(x)] = Bitmap1::Light;
    }
  for (int t = 0; t < 2; ++t)
    for (int y = y0; y <= y1; ++y) {
      layer.px[static_cast<size_t>(y) * static_cast<size_t>(layer.w) + static_cast<size_t>(x0 + t)] = Bitmap1::Light;
      layer.px[static_cast<size_t>(y) * static_cast<size_t>(layer.w) + static_cast<size_t>(x1 - t)] = Bitmap1::Light;
    }
}

// A rough wooden plank shaped like an arrow, w x h pixels (what is not the plank is clear): a body with ragged top, bottom and tail,
// and a pointed tip at the right end (`right`) or the left. The Text style draws the signs from it: the sprites' own outlines are too
// fine to trace (Labyrinth's is nearly a rectangle) and say nothing about which way the sign points.
Bitmap1 arrowPlank(int w, int h, bool right, unsigned seed) {
  Bitmap1 b(w, h);
  const int tip = h * 9 / 10, start = w - tip;
  const auto jitter = [&](int n, unsigned k) {  // 0..2, the same each time
    unsigned v = static_cast<unsigned>(n) * 73856093u ^ (k + seed) * 19349663u;
    v ^= v >> 13, v *= 0x5bd1e995u, v ^= v >> 15;
    return static_cast<int>(v % 3);
  };
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const int tx = right ? x : w - 1 - x;  // along the arrow, 0 at the tail
      bool in;
      if (tx < start) in = y >= jitter(tx / 3, 1) && y <= h - 1 - jitter(tx / 3, 2) && tx >= jitter(y / 2, 3);
      else in = std::abs(y - (h - 1) / 2.0) <= (h / 2.0) * (w - 1 - tx) / tip;
      if (in) b.px[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = Bitmap1::Light;
    }
  return b;
}

// A 1-bit sign enlarged to kSignDetail x its size and softened, as a smooth two-tone picture. `shape` says where the sign is (its clear
// pixels are outside), `look` what each pixel of it is (light or dark). Both are blurred as they are enlarged, so the stair-steps of the
// original's pixels become smooth curves, and the result's edge is feathered; the tone is then pushed a little harder than the blur
// alone, to keep the lettering crisp enough to read. The picture has a margin of kPad pixels all round for the feathering.
constexpr int kPad = 2;
std::shared_ptr<const Image8> smoothSign(const Bitmap1& shape, const Bitmap1& look) {
  const int S = kSignDetail, w = (shape.w + 2 * kPad) * S, h = (shape.h + 2 * kPad) * S;
  const auto blur = [&](const std::vector<double>& in, double sigma) {  // separable gaussian, the edge clamped
    const int r = static_cast<int>(std::ceil(sigma * 2.5));
    std::vector<double> k(static_cast<size_t>(2 * r + 1));
    double sum = 0;
    for (int i = -r; i <= r; ++i) sum += k[static_cast<size_t>(i + r)] = std::exp(-0.5 * i * i / (sigma * sigma));
    for (double& v : k) v /= sum;
    std::vector<double> tmp(in.size()), out(in.size());
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        double a = 0;
        for (int i = -r; i <= r; ++i) a += in[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(std::clamp(x + i, 0, w - 1))] * k[static_cast<size_t>(i + r)];
        tmp[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = a;
      }
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        double a = 0;
        for (int i = -r; i <= r; ++i) a += tmp[static_cast<size_t>(std::clamp(y + i, 0, h - 1)) * static_cast<size_t>(w) + static_cast<size_t>(x)] * k[static_cast<size_t>(i + r)];
        out[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = a;
      }
    return out;
  };
  std::vector<double> cover(static_cast<size_t>(w) * static_cast<size_t>(h)), ink(cover.size());
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const int sx = x / S - kPad, sy = y / S - kPad;
      const bool in = shape.at(sx, sy) != Bitmap1::Clear;
      cover[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = in ? 1.0 : 0.0;
      ink[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = in && look.at(sx, sy) != Bitmap1::Dark ? 1.0 : 0.0;
    }
  // the outline is softened more than the lettering: the letters keep hard, only slightly rounded edges
  const std::vector<double> c = blur(cover, 0.3 * S), ct = blur(cover, 0.1 * S), t = blur(ink, 0.1 * S);
  const auto smooth = [](double lo, double hi, double v) {
    const double u = std::clamp((v - lo) / (hi - lo), 0.0, 1.0);
    return u * u * (3 - 2 * u);
  };
  auto img = std::make_shared<Image8>();
  img->w = w;
  img->h = h;
  img->tone.resize(c.size());
  img->alpha.resize(c.size());
  for (size_t i = 0; i < c.size(); ++i) {
    img->alpha[i] = static_cast<std::uint8_t>(smooth(0.25, 0.6, c[i]) * 255 + 0.5);
    img->tone[i] = static_cast<std::uint8_t>(smooth(0.4, 0.6, ct[i] > 0.01 ? t[i] / ct[i] : 0.0) * 255 + 0.5);
  }
  return img;
}

// The white ground of a picture made clear: the white that is joined to its edge. White inside the figure stays.
void clearGround(Bitmap1& b) {
  std::vector<int> todo;
  const auto at = [&](int x, int y) -> std::uint8_t& { return b.px[static_cast<size_t>(y) * static_cast<size_t>(b.w) + static_cast<size_t>(x)]; };
  const auto push = [&](int x, int y) {
    if (x >= 0 && y >= 0 && x < b.w && y < b.h && at(x, y) == Bitmap1::Light) {
      at(x, y) = Bitmap1::Clear;
      todo.push_back(y * b.w + x);
    }
  };
  for (int x = 0; x < b.w; ++x) push(x, 0), push(x, b.h - 1);
  for (int y = 0; y < b.h; ++y) push(0, y), push(b.w - 1, y);
  while (!todo.empty()) {
    const int i = todo.back(), x = i % b.w, y = i / b.w;
    todo.pop_back();
    push(x + 1, y), push(x - 1, y), push(x, y + 1), push(x, y - 1);
  }
}

// Behind the Blacksmith: his workshop. A beam with tongs, a hammer and a horseshoe on chains, a shield, a rack of blades, an anvil on
// its stump and a bucket on a dithered stone floor. Drawn in the dim background tone; the figure stands in front.
void workshop(Bitmap1& layer, int x1) {
  const int W = layer.w, H = layer.h, floorY = H - 12;
  const auto set = [&](int x, int y, std::uint8_t v = Bitmap1::Light) {
    if (x >= 0 && x <= x1 && y >= 0 && y < H && x < W) layer.px[static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)] = v;
  };
  const auto rect = [&](int xa, int ya, int xb, int yb, std::uint8_t v = Bitmap1::Light) {
    for (int y = ya; y <= yb; ++y)
      for (int x = xa; x <= xb; ++x) set(x, y, v);
  };
  const auto line = [&](int xa, int ya, int xb, int yb, int t = 1) {
    const int n = std::max(std::abs(xb - xa), std::abs(yb - ya));
    for (int i = 0; i <= n; ++i) {
      const int x = n ? xa + (xb - xa) * i / n : xa, y = n ? ya + (yb - ya) * i / n : ya;
      for (int d = 0; d < t; ++d) set(x + d, y);
    }
  };
  const auto ring = [&](int cx, int cy, double r, double thick) {
    for (int y = cy - static_cast<int>(r) - 1; y <= cy + r + 1; ++y)
      for (int x = cx - static_cast<int>(r) - 1; x <= cx + r + 1; ++x) {
        const double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
        if (d <= r && d > r - thick) set(x, y);
      }
  };
  const auto chain = [&](int x, int ya, int yb) {
    for (int y = ya; y <= yb; y += 3) rect(x, y, x + 1, std::min(y + 1, yb));
  };
  // the beam, with the floor's stones
  rect(0, 0, x1, 3);
  static const int kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  for (int y = floorY; y < H; ++y)
    for (int x = 0; x <= x1; ++x)
      if (y == floorY || kBayer[y & 3][x & 3] < 6) set(x, y);
  // tongs on a chain
  chain(6, 4, 14);
  ring(7, 17, 3.5, 1.5);
  line(5, 20, 2, 42);
  line(9, 20, 12, 42);
  line(2, 42, 6, 47);
  line(12, 42, 8, 47);
  // a hammer
  chain(63, 4, 14);
  rect(62, 14, 64, 40);
  rect(56, 14, 70, 22);
  // a horseshoe
  chain(77, 4, 11);
  ring(78, 19, 7, 2.5);
  rect(74, 22, 82, 28, Bitmap1::Dark);
  // a round shield on the wall
  ring(7, 62, 9, 2);
  line(7, 54, 7, 70);
  line(-1, 62, 15, 62);
  // a rack of blades
  rect(63, 50, 81, 51);
  rect(63, 88, 81, 89);
  for (int x : {66, 71, 76}) {
    line(x, 52, x, 87, 2);
    rect(x - 2, 80, x + 3, 81);
  }
  // an anvil on its stump
  rect(64, floorY - 14, 79, floorY - 1);                 // the stump
  rect(63, floorY - 16, 80, floorY - 15);                // its foot
  rect(66, floorY - 22, 78, floorY - 17);                // the waist
  rect(60, floorY - 28, 81, floorY - 23);                // the face
  line(60, floorY - 27, 55, floorY - 25, 3);             // the horn
  for (int y = floorY - 13; y < floorY; y += 4) rect(64, y, 79, y, Bitmap1::Dark);  // the stump's grain
  // a bucket
  rect(0, floorY - 18, 12, floorY - 1);
  rect(0, floorY - 13, 12, floorY - 12, Bitmap1::Dark);
  rect(0, floorY - 6, 12, floorY - 5, Bitmap1::Dark);
}

}  // namespace

namespace {
TownPicture paintTownCore(const ViewPainter& painter, TownScene scene, int cols, int rows, int selected, int flame, int density) {
  const int W = cols * 2, H = rows * 4;
  TownPicture out;
  ViewLayers& L = out.layers;
  L = ViewLayers{Bitmap1(W, H), Bitmap1(W, H), Bitmap1(W, H), Bitmap1(W, H)};
  L.native = true;
  const auto img = [&](const std::string& name) { return painter.image(name); };  // as the original has them: white is lit
  // The Text style draws ink as characters, so its pictures are the original's drawing (black lines on white): inverted, the lines are the
  // ink and the white ground stays empty. (The mosaic keeps the original's look: a lit ground with the drawing dark in it.)
  const auto art = [&](const Bitmap1& b) { return density > 1 ? b.inverted() : b; };
  switch (scene) {
    case TownScene::Square: {
      // The post is 116 pixels tall. A view shorter than that (the terminal's) gets the whole scene scaled down to fit, so nothing scrolls;
      // the caption takes the last row. Everything below works in the scaled size.
      const double f = std::min(1.0, (H - 4) / 116.0);
      const auto fit = [&](const Bitmap1& b) {
        return f < 1.0 ? b.scaledTo(std::max(1, static_cast<int>(std::lround(b.w * f))), std::max(1, static_cast<int>(std::lround(b.h * f)))) : b;
      };
      const auto scl = [&](int v) { return static_cast<int>(std::lround(v * f)); };
      Sign sg[5];
      for (int i = 0; i < 5; ++i) sg[i] = {kSigns[i].image, scl(kSigns[i].x), scl(kSigns[i].y)};
      const Sign& cur = sg[std::clamp(selected, 0, 4)];
      const int squareLeft = scl(kSquareLeft);
      const int left = squareLeft - std::clamp(W - scl(2 * 93), 0, squareLeft) / 2 * 2 + std::max(0, (cur.x + fit(img(cur.image)).w + 2 - squareLeft - W + 1) / 2 * 2);  // (and the Save sign, off to the right, is brought in)
      const Bitmap1 base = fit(img("Town/SignPost")).cropped(left, 0, W, H);
      (density > 1 ? L.shade : L.walls).blit(art(base), 0, 0);  // in Text the scenery is the dim tone, so the signs stand out from it
      // The signs are drawn twice: as part of the mosaic (what is left where a smooth sign does not reach), and over it as smooth
      // pictures, the chosen one at full brightness (as the picture has it) and the others in their negative.
      static std::map<int, std::shared_ptr<const Image8>> smoothed;  // by sign, +5 for the chosen one's look
      for (int i = 0; i < 5; ++i) {
        const Sign& s = sg[i];
        const Bitmap1 spr = fit(img(s.image));
        const bool chosen = i == selected;
        static const int kPointsRight[] = {1, 0, 1, 0, 0};  // Temple and BeastMart point right, Blacksmith and Labyrinth left; Save & Exit is a box
        const Bitmap1 shape = density > 1 && i < 4 ? arrowPlank(spr.w, spr.h, kPointsRight[i], static_cast<unsigned>(i)) : spr;  // (Text: its own arrow)
        if (density > 1) {  // Text: the lettering is too fine to be traced into characters, so a sign is an empty plank (solid when chosen) with its name as real text
          for (int y = 0; y < shape.h; ++y)
            for (int x = 0; x < shape.w; ++x) {
              if (shape.at(x, y) == Bitmap1::Clear) continue;
              const int tx = s.x - left + x, ty = s.y + y;
              if (tx < 0 || ty < 0 || tx >= W || ty >= H) continue;
              bool edge = false;  // within three pixels of the outside
              for (int d = 1; d <= 3; ++d) edge = edge || shape.at(x - d, y) == Bitmap1::Clear || shape.at(x + d, y) == Bitmap1::Clear || shape.at(x, y - d) == Bitmap1::Clear || shape.at(x, y + d) == Bitmap1::Clear;
              auto& dst = chosen ? L.objects.px[static_cast<size_t>(ty) * static_cast<size_t>(W) + static_cast<size_t>(tx)] : L.walls.px[static_cast<size_t>(ty) * static_cast<size_t>(W) + static_cast<size_t>(tx)];
              dst = chosen || edge ? Bitmap1::Light : Bitmap1::Dark;
            }
          static const int kRowShift[] = {1, 0, 1, 1, 0};  // the planks whose body sits lower than the middle of their outline
          const int tipLen = i < 4 ? shape.h * 9 / 10 : 0, toTail = (kPointsRight[i] ? -1 : 1) * tipLen / 4;  // the text sits in the plank's body, not the tip
          const int midRow = (s.y + shape.h / 2) / 4 + kRowShift[i], midCol = (s.x - left + shape.w / 2 + toTail) / 2;
          const auto inside = [&](int cx, int cy) {  // is this cell filled completely by the plank
            for (int y = 0; y < 4; ++y)
              for (int x = 0; x < 2; ++x)
                if (shape.at(cx * 2 + x - (s.x - left), cy * 4 + y - s.y) == Bitmap1::Clear) return false;
            return true;
          };
          if (chosen) {  // the chosen plank is solid colour inside (the cells it fills completely), so the text's bar has no edge of its own
            for (int cy = s.y / 4; cy * 4 < s.y + shape.h; ++cy) {  // each band of two cell rows: its longest run of cells that are inside in both
              int best = 0, bestX = 0, run = 0;
              for (int cx = (s.x - left) / 2 - 1; cx <= (s.x - left + shape.w) / 2 + 1; ++cx) {
                run = inside(cx, cy) && inside(cx, cy + 1) ? run + 1 : 0;
                if (run > best) best = run, bestX = cx - run + 1;
              }
              if (!best) continue;
              SceneLabel fill;
              fill.filled = true;
              fill.cx = bestX;
              fill.cy = cy;
              fill.w = best;
              out.labels.push_back(fill);
            }
          }
          {  // distressing: a few small marks (nicks, knots, grain) well inside the plank, faint beside the name, and clear of it
            static const char kMarks[] = {'.', ',', '\'', '`', '-', '~', ':'};
            const int textHalf = (static_cast<int>(std::ceil(static_cast<double>(std::string(i == 4 ? "Save &" : kSignNames[i]).size()) * 11 * 1.4 / 6)) + 2) / 2 + 2;
            for (int cy = s.y / 4; cy * 4 < s.y + shape.h; ++cy)
              for (int cx = (s.x - left) / 2 - 1; cx <= (s.x - left + shape.w) / 2 + 1; ++cx) {
                if (cy >= midRow - 1 && cy <= midRow && std::abs(cx - midCol) <= textHalf) continue;  // (clear of the name)
                if (!(inside(cx, cy) && inside(cx, cy + 1))) continue;  // (a mark must not leave the plank)
                unsigned v = static_cast<unsigned>(cx) * 73856093u ^ static_cast<unsigned>(cy) * 19349663u ^ static_cast<unsigned>(i + 1) * 83492791u;
                v ^= v >> 13, v *= 0x5bd1e995u, v ^= v >> 15;
                if (v % 100 >= 12) continue;
                SceneLabel m;
                m.filled = chosen;
                m.fade = 0.55f;
                m.cx = cx;
                m.cy = cy;
                m.w = 2;
                m.text = std::string(1, kMarks[(v / 100) % sizeof(kMarks)]);
                out.labels.push_back(m);
              }
          }
          const auto name = [&](const std::string& text, int cy) {
            SceneLabel l;
            l.filled = chosen;  // the chosen plank is lit: dark text on a lit bar
            l.scale = 1.4f;
            l.w = static_cast<int>(std::ceil(static_cast<double>(text.size()) * 11 * l.scale / 6)) + 2;
            l.cx = midCol - l.w / 2;
            l.cy = cy;
            l.text = text;
            out.labels.push_back(l);
          };
          if (i == 4) name("Save &", midRow - 2), name("Exit", midRow);
          else name(kSignNames[i], midRow - 1);
        } else if (!chosen) L.walls.blit(spr, s.x - left, s.y);
        else
          for (int y = 0; y < spr.h; ++y)
            for (int x = 0; x < spr.w; ++x)
              if (spr.at(x, y) != Bitmap1::Clear) {
                const int tx = s.x - left + x, ty = s.y + y;
                if (tx >= 0 && ty >= 0 && tx < W && ty < H) L.objects.px[static_cast<size_t>(ty) * static_cast<size_t>(W) + static_cast<size_t>(tx)] = base.at(tx, ty);
              }
        auto& sm = smoothed[i + (chosen ? 5 : 0) + 10 * scl(100)];
        if (!sm) {
          Bitmap1 look = spr;  // the chosen sign takes the picture's own pixels
          if (chosen)
            for (int y = 0; y < spr.h; ++y)
              for (int x = 0; x < spr.w; ++x) look.px[static_cast<size_t>(y) * static_cast<size_t>(spr.w) + static_cast<size_t>(x)] = base.at(s.x - left + x, s.y + y);
          sm = smoothSign(spr, look);
        }
        if (density == 1) out.sprites.push_back({sm, s.x - left - kPad, s.y - kPad, chosen});
      }
      break;
    }
    case TownScene::Temple: {
      Bitmap1 hall = img("Town/Temple");
      for (int y = 40; y <= 47; ++y)  // her forehead: the faint line above her eyes would turn into stray characters, so it is left smooth
        for (int x = 109; x <= 126; ++x) hall.px[static_cast<size_t>(y) * static_cast<size_t>(hall.w) + static_cast<size_t>(x)] = Bitmap1::Light;
      // She takes the candles' warm colour (the gold of the theme) so she stands off the hall: her silhouette moves to a tint layer.
      static const int kHer[][2] = {{100, 29}, {124, 29}, {131, 45}, {132, 70}, {142, 96}, {82, 96}, {94, 70}, {94, 45}};
      const auto inHer = [&](int x, int y) {
        bool in = false;
        for (size_t i = 0, j = std::size(kHer) - 1; i < std::size(kHer); j = i++)
          if ((kHer[i][1] > y) != (kHer[j][1] > y) && x < (kHer[j][0] - kHer[i][0]) * (y - kHer[i][1]) / (kHer[j][1] - kHer[i][1]) + kHer[i][0]) in = !in;
        return in;
      };
      Bitmap1 her(hall.w, hall.h);
      for (int y = 0; y < hall.h; ++y)
        for (int x = 0; x < hall.w; ++x)
          if (inHer(x, y)) {
            const size_t i = static_cast<size_t>(y) * static_cast<size_t>(hall.w) + static_cast<size_t>(x);
            her.px[i] = hall.px[i];
            hall.px[i] = Bitmap1::Clear;
          }
      (density > 1 ? L.shade : L.objects).blit(art(hall.cropped(27, 0, W, H)), 0, 0);  // (Text: the hall is dim scenery, the flames are bright)
      Bitmap1& tint = L.tints[0].lit;
      tint = Bitmap1(W, H);
      tint.blit(art(her.cropped(27, 0, W, H)), 0, 0);
      const Bitmap1 f = img("Town/TOWN_Temple_candle_flame-table-" + std::to_string(flame % 7 + 1));
      L.objects.blit(art(f), 46 - 27, 54);
      L.objects.blit(art(f), 181 - 27, 54);
      break;
    }
    case TownScene::Blacksmith:
    {
      Bitmap1 figure = img("Town/Blacksmith").cropped(0, 0, 82, 118);
      clearGround(figure);
      {  // a rim of light round him, so the dark figure stands off the workshop
        const Bitmap1 body = figure;
        for (int y = 0; y < body.h; ++y)
          for (int x = 0; x < body.w; ++x) {
            if (body.at(x, y) != Bitmap1::Clear) continue;
            for (int dy = -1; dy <= 1; ++dy)
              for (int dx = -1; dx <= 1; ++dx)
                if (body.at(x + dx, y + dy) != Bitmap1::Clear) figure.px[static_cast<size_t>(y) * static_cast<size_t>(figure.w) + static_cast<size_t>(x)] = Bitmap1::Light;
          }
      }
      workshop(L.background, kShopBoxX * 2 - 3);
      L.objects.blit(art(figure), 0, (H - 118) / 2);
      box(L.objects, kShopBoxX * 2, 1, W - 2, H - 2);
      break;
    }
    case TownScene::BeastMart: {
      L.objects.blit(art(img("Town/BeastMart").cropped(0, 0, 80, 116)), 0, (H - 116) / 2);
      box(L.objects, kShopBoxX * 2, 1, W - 2, H - 2);
      for (int t = 0; t < 2; ++t)  // between the two slots
        for (int x = kShopBoxX * 2; x < W - 1; ++x) L.objects.px[static_cast<size_t>(4 * (1 + shopGeom(cols, rows).slotRows) - 1 + t) * static_cast<size_t>(W) + static_cast<size_t>(x)] = Bitmap1::Light;
      break;
    }
  }
  if (density > 1) {  // Text: the same picture on a finer grid
    for (Bitmap1* b : {&L.background, &L.walls, &L.objects, &L.shade, &L.tints[0].lit, &L.tints[0].shade})
      if (b->w > 0) *b = b->scaledTo(W * density, H * density);
    L.density = density;
  }
  return out;
}

}  // namespace

int townDesignCols(TownScene scene) { return scene == TownScene::Square ? 120 : scene == TownScene::Temple ? 100 : 93; }
int townMarginCols(TownScene scene, int cols) { return std::max(0, (cols - townDesignCols(scene)) / 2); }

TownPicture paintTown(const ViewPainter& painter, TownScene scene, int cols, int rows, int selected, int flame, int density) {
  const int inner = std::min(cols, townDesignCols(scene)), ox = townMarginCols(scene, cols);
  TownPicture p = paintTownCore(painter, scene, inner, rows, selected, flame, density);
  if (inner == cols) return p;
  const int d = std::max(1, p.layers.density);
  const auto widen = [&](Bitmap1& b) {
    if (b.w <= 0) return;
    Bitmap1 wide(cols * 2 * d, rows * 4 * d);
    wide.blit(b, ox * 2 * d, 0);
    b = std::move(wide);
  };
  ViewLayers& L = p.layers;
  for (Bitmap1* b : {&L.background, &L.walls, &L.objects, &L.shade, &L.tints[0].lit, &L.tints[0].shade}) widen(*b);
  for (SceneLabel& l : p.labels) l.cx += ox;
  for (TownSprite& t : p.sprites) t.x += ox * 2;
  return p;
}

}  // namespace laby
