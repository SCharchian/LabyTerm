#include "ui/ViewPainter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

#include "core/DataFile.hpp"
#include "platform/Theme.hpp"

namespace laby {

void drawViewLayers(CellGrid& g, int cx, int cy, const ViewLayers& layers, Rgb accent, Rgb bg, const Rgb* objects, const ObjectColors* colors) {
  const Rgb hi = accent;
  // Text characters ink about half of their cell, so the same tone looks about half as bright as a solid octant block: the tones are lifted to match.
  const auto tone = [&](Rgb c, double amount) { return mix(bg, c, layers.density > 1 ? std::pow(amount, kTextToneGamma) : amount); };
  const Rgb groupColor[kTints] = {colors ? colors->treasure : hi, colors ? colors->lift : hi, colors ? colors->water : hi,
                                  objects ? *objects : colors ? colors->lair : hi};
  std::vector<MosaicLayer> stack{{&layers.objects, objects ? *objects : hi, true}};  // (objects glow in the CRT filter; their shaded parts and the walls do not)
  for (int i = 0; i < kTints; ++i)
    if (layers.tints[i].lit.w > 0) stack.push_back({&layers.tints[i].lit, groupColor[i], true});
  stack.push_back({&layers.walls, tone(hi, 0.95)});
  stack.push_back({&layers.shade, tone(hi, kShadeMix)});
  for (int i = 0; i < kTints; ++i)
    if (layers.tints[i].shade.w > 0) stack.push_back({&layers.tints[i].shade, tone(groupColor[i], kShadeMix)});
  stack.push_back({&layers.background, colors && layers.native ? tone(colors->surround, 1.0) : tone(hi, layers.native ? 0.5 : 0.40)});  // the procedural view dims floor and ceiling by density, not colour
  drawMosaicStack(g, cx, cy, stack, bg, /*invert=*/true, layers.density);
}

bool isObjectSprite(const SpriteDef& s) {
  return s.id.find("Boss") != std::string::npos || s.id.find("Chest") != std::string::npos || s.id.find("Elevator") != std::string::npos;
}

namespace {
bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
}  // namespace

Bitmap1 loadOriginalArt(const std::string& name) {
  std::string rel;
  if (startsWith(name, "Town/")) rel = "town/" + name.substr(5);
  else if (startsWith(name, "Monsters/")) rel = "lair/" + name.substr(9);
  else if (name == "Laby-Title" || startsWith(name, "TitleCloud")) rel = "title/" + name;
  else if (startsWith(name, "TownPortal")) rel = "scenes/" + name;
  if (!rel.empty()) {
    Bitmap1 b = Bitmap1::loadPng(dataDir() + "/art/" + rel + ".png");
    if (b.w > 0) return b;
  }
  // Not bundled: the picture is drawn blank. Say so once, so a broken install is not silent.
  static std::set<std::string> warned;
  if (warned.insert(name).second) std::fprintf(stderr, "labyterm: picture \"%s\" not found in %s/art\n", name.c_str(), dataDir().c_str());
  return Bitmap1();
}

ViewPainter::ViewPainter(const WallView& view) : view_(view) {}

const Bitmap1& ViewPainter::image(const std::string& name) const {
  auto it = cache_.find(name);
  if (it == cache_.end()) it = cache_.emplace(name, loadOriginalArt(name)).first;
  return it->second;
}

const LairTextures& ViewPainter::lair() const {
  if (!lairLoaded_) {
    lair_ = {image("Monsters/1LevelBoss"), image("Monsters/1LevelBossLeft"), image("Monsters/1LevelBossRight")};
    lairLoaded_ = true;
  }
  return lair_;
}

ViewLayers ViewPainter::paint(const WallView::Output& out, bool combat) const {
  ViewLayers L{Bitmap1(kWidth, kHeight), Bitmap1(kWidth, kHeight), Bitmap1(kWidth, kHeight)};
  // the original clears its canvas to white, then draws the floor/ceiling image over it
  std::fill(L.background.px.begin(), L.background.px.end(), Bitmap1::Light);
  L.background.blit(image(view_.background()), 0, 0);

  for (const SpriteDef* s : out.sprites) {
    const Bitmap1& bmp = image(s->image);
    if (isObjectSprite(*s)) {
      L.objects.blit(bmp, s->x, s->y);
    } else {
      L.walls.blit(bmp, s->x, s->y);
      L.objects.eraseWhere(bmp, s->x, s->y);  // a wall drawn later covers an object drawn earlier
    }
  }

  // hard vertical edges at the corners, drawn over everything (DrawHardWallEdge)
  const auto& he = view_.hardEdge();
  const int top = combat ? he.yTopCombat : he.yTop;
  const auto edge = [&](int x) {
    for (int y = top; y <= he.yBottom; ++y) {
      L.walls.px[static_cast<size_t>(y) * kWidth + static_cast<size_t>(x)] = Bitmap1::Dark;
      L.objects.px[static_cast<size_t>(y) * kWidth + static_cast<size_t>(x)] = Bitmap1::Clear;
    }
  };
  if (out.hardLeft) edge(he.leftX);
  if (out.hardRight) edge(he.rightX);
  return L;
}

}  // namespace laby
