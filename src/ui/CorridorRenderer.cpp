#include "ui/CorridorRenderer.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <future>
#include <limits>
#include <thread>
#include <vector>

namespace laby {
namespace {

using T = TileClass;
constexpr double kInf = std::numeric_limits<double>::infinity();

// ---------------------------------------------------------------------------------------------------------------
// Textures. Every texture is a function of a point on a surface. `foot` is how big one pixel is at that point, in
// world units: lines are never drawn thinner than that, so far-away detail fades to its average instead of
// shimmering.
// ---------------------------------------------------------------------------------------------------------------

struct Texel {
  bool solid = true;     // false: you can see through it (the doorway of a door frame, a lattice hole)
  double albedo = 1.0;   // 0 = dark ... 1 = light
  bool shade = false;    // drawn in the shade tone (a shadow, or a part of the object that is in shade)
  bool object = false;   // drawn at full brightness, like an object standing in the picture (the fountain in its niche)
};

double hash01(std::int64_t a, std::int64_t b = 0, std::int64_t c = 0) {
  std::uint64_t h = static_cast<std::uint64_t>(a) * 0x9E3779B97F4A7C15ull ^ static_cast<std::uint64_t>(b) * 0xC2B2AE3D27D4EB4Full ^ static_cast<std::uint64_t>(c) * 0x165667B19E3779F9ull;
  h ^= h >> 32;
  h *= 0xD6E8FEB86659FD93ull;
  h ^= h >> 32;
  return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

// A point on a wall face. `along` is the map coordinate along the face (continuous across cells), `v` the height.
struct WallPoint {
  double along, v;
  double footAlong, footV;  // how big one pixel is along the face and up it (world units): lines are never thinner than this
  int cellX, cellY, faceDir;  // the wall cell and which way the face looks (0..3), for per-wall variation
  bool slanted = false;                 // seen at a slant (a side wall): its lines are soft-edged; a front wall's are crisp
  bool edgeLo = false, edgeHi = false;  // draw a dark line at the low / high end of this face (where the wall ends or turns)
};

// How much of a pixel a line covers: the pixel's footprint `f` (world units) is centred `d` from the line's centre, the
// line is `w` wide. Near the viewer the line covers whole pixels (crisp); far away, where a pixel spans more than the
// line is wide, it covers only a fraction, which the dither turns into a fading dotted line instead of a black bar.
double lineCover(double d, double w, double f) {
  const double overlap = std::clamp(w * 0.5 + f * 0.5 - d, 0.0, std::min(w, f));
  return f > 0 ? overlap / f : 0.0;
}

Texel brick(const WallPoint& p, const CorridorStyle& st, bool joints = true) {  // `joints`: false leaves only the courses (no vertical joints)
  const double cellIdx = std::floor(p.along + 0.5);
  const double u = p.along - (cellIdx - 0.5);
  // a dark line where the wall ends or turns (not between flat neighbouring segments)
  const double edge = std::max(st.edgeLine, p.footAlong * 0.75);
  if ((p.edgeLo && u < edge) || (p.edgeHi && u > 1 - edge)) return {true, 0.0};
  // the thin line where the wall meets the floor and the ceiling
  const double base = std::max(st.baseline, p.footV);
  if (p.v < base || p.v > 1 - base) return {true, 0.0};
  // courses (the horizontal joints). A line is drawn by how much of each pixel it covers, so a slanted line steps in
  // soft dithered pixels instead of hard 1-pixel stairs (0.55 is the level the dither treats as half lit).
  const double course = p.v * st.courses;
  const int row = static_cast<int>(std::floor(course));
  const double fy = course - row;
  const double dv = std::min(row > 0 ? fy : 1.0, row < st.courses - 1 ? 1 - fy : 1.0) / st.courses;  // distance to a course line
  if (dv < std::max(st.mortar, p.footV) * 0.5) return {true, 0.0};  // a crisp one-pixel line
  // bricks in a course, alternate courses shifted by half a brick; the pattern is aligned to the cells (a third of a
  // cell per brick, shifted by a sixth) so the smallest piece beside a corner is a half brick, never a sliver
  const double bx = (p.along + (row & 1 ? 0.5 : 0.0) * st.brickLength) / st.brickLength;
  const int bi = static_cast<int>(std::floor(bx));
  const double fx = bx - bi;
  const double mw = std::min(0.45, std::max(st.mortar, p.footAlong) / st.brickLength) * 0.5;
  if (joints && (fx < mw || fx > 1 - mw)) {
    // a joint that falls on the line where the wall ends or turns would double that line: the corner line is the joint
    const double clear = edge + st.mortar * 2;
    if (!((p.edgeLo && u < clear) || (p.edgeHi && u > 1 - clear))) return {true, 0.0};
  }
  return {true, 0.78 + 0.22 * hash01(bi, row, p.cellX * 131 + p.cellY * 7 + p.faceDir)};
}

// ---- The doorway: an opening with an arched top, cut through a wall of real thickness (CorridorStyle::doorDepth). On the
// wall's face it is framed by dressed stone (a ring of radial voussoirs round the arch with a longer keystone, quoins up the
// sides); inside, the jambs and the arch's underside are hewn stone with their own joints; a closed door's planks stand at
// the back of that recess.

// Signed distance inside the doorway's outline (positive inside): a rectangle up to the spring line, capped by a semicircle.
double doorDist(double u, double v, const CorridorStyle& st) {
  if (v < 0 || v > 1) return -1;
  return v > st.doorSpring ? st.doorRadius - std::hypot(u - 0.5, v - st.doorSpring) : st.doorRadius - std::fabs(u - 0.5);
}

// The arch's voussoir that angle `theta` (0..pi, from the right-hand spring line over the top to the left) falls in, and how far
// (in cells, at radius `rad`) it is from the nearest joint between voussoirs.
int voussoir(double theta, double rad, const CorridorStyle& st, double& jointDist) {
  const double step = 3.14159265358979323846 / st.doorVoussoirs;
  const double k = theta / step;
  jointDist = std::fabs(k - std::round(k)) * step * rad;
  return std::min(st.doorVoussoirs - 1, static_cast<int>(std::floor(k)));
}

// The wall's face around the doorway: true (and `out` set) where the dressed stone is.
// `innerRim`: also draw the dark line along the stone's inner edge (the raised frame's front face has one, where it turns in to the
// opening; the dressing flush with the wall already has the opening's own outline there).
bool doorSurround(double u, double v, const WallPoint& p, const CorridorStyle& st, Texel& out, bool innerRim = false) {
  const double x = u - 0.5, r = st.doorRadius, cy = st.doorSpring;
  const double w = std::max(st.mortar, p.footV) * 0.5;  // half a joint line
  const double rim = std::max(st.doorOutline, p.footV);  // the outer edge of the dressed stone: as thick as the outline round the opening
  const std::int64_t seed = p.cellX * 131 + p.cellY * 7 + p.faceDir;
  if (v > cy) {
    const double rad = std::hypot(x, v - cy);
    double joint;
    const int idx = voussoir(std::atan2(v - cy, x), rad, st, joint);
    const double outer = r + st.doorRing * (idx == st.doorVoussoirs / 2 ? st.keystoneGrow : 1.0);
    if (rad < r || rad >= outer) return false;
    out = {true, joint < w || outer - rad < rim || (innerRim && rad - r < rim) ? 0.0 : 0.8 + 0.2 * hash01(idx, 91, seed)};
    return true;
  }
  if (v < 0 || std::fabs(x) < r || std::fabs(x) >= r + st.doorRing) return false;
  const double h = cy / 3;  // quoin courses: three up to the spring line, so the top joint is the spring line itself
  const double course = v / h, fy = course - std::floor(course);
  const bool joint = std::min(fy, 1 - fy) * h < w;
  out = {true, joint || v < std::max(st.baseline, p.footV) || r + st.doorRing - std::fabs(x) < rim || (innerRim && std::fabs(x) - r < rim) ? 0.0 : 0.8 + 0.2 * hash01(static_cast<std::int64_t>(course), 5, seed)};
  return true;
}

// The dressed stone's joints only (no edge lines): the inside of the raised frame is the same stones as its front face, so their
// joints run on across it, to the door.
bool doorSurroundJoints(double u, double v, const WallPoint& p, const CorridorStyle& st, Texel& out) {
  const double x = u - 0.5, cy = st.doorSpring, w = std::max(st.mortar * 0.5, p.footV) * 0.5;  // thinner than the front face's joints
  const std::int64_t seed = p.cellX * 131 + p.cellY * 7 + p.faceDir;
  if (v > cy) {
    const double rad = std::hypot(x, v - cy);
    double joint;
    const int idx = voussoir(std::atan2(v - cy, x), rad, st, joint);
    out = {true, joint < w ? 0.0 : 0.8 + 0.2 * hash01(idx, 91, seed)};
    return true;
  }
  const double h = cy / 3, course = v / h, fy = course - std::floor(course);
  out = {true, std::min(fy, 1 - fy) * h < w ? 0.0 : 0.8 + 0.2 * hash01(static_cast<std::int64_t>(course), 5, seed)};
  return true;
}

// The face of a wall with a doorway: the dark outline round the opening, or brick; not solid inside the opening (the
// tracer goes on into the recess).
Texel doorFront(const WallPoint& p, const CorridorStyle& st) {
  const double u = p.along - (std::floor(p.along + 0.5) - 0.5);
  const double dist = doorDist(u, p.v, st);
  if (dist >= std::max(st.doorOutline, p.footV)) return {false, 0.0};
  if (dist >= 0) return {true, 0.0};  // dark outline round the opening
  // The dressed stone is the raised frame in front of the wall (reliefHit); the wall itself, round the opening, is plain brick.
  // It has courses only: a vertical joint in the narrow strip left between the frame and the door cell's edge would cut it into slivers.
  return brick(p, st, false);
}

// The fountain in a found secret door's niche, in line art: a tall plinth (standing close to the door the view starts a hand's breadth
// above the floor, so the fountain stands on a high pedestal), a wide basin and a smaller one on a stem, and (unless the fountain is
// drained) water standing in them, a jet from the top and its fall from bowl to bowl. `x`: across the niche from its middle, `v`:
// height. Outlines and water are full brightness, the stone bodies a flat shade tone, the rest of the niche dark.
Texel fountainTexel(double x, double v, double foot, bool dry) {
  const double lw = std::max(0.013, foot * 1.1), ax = std::fabs(x);
  Texel t{true, 0.0};
  const auto body = [&] { t = {true, 1.0, true, false}; };
  const auto line = [&] { t = {true, 1.0, false, true}; };
  // a stem or plinth between two heights, its half width going from hw0 (at v0) to hw1 (at v1)
  const auto stem = [&](double v0, double v1, double hw0, double hw1) {
    if (v < v0 || v > v1) return;
    const double hw = hw0 + (hw1 - hw0) * (v - v0) / (v1 - v0);
    if (ax > hw) return;
    if (ax > hw - lw) line(); else body();
    if (v < v0 + lw * 0.6 && v0 > 0.01) line();  // a joint where it meets what is below
  };
  // a bowl: the lower half of an ellipse hanging from its rim at height cy, `fill` deep in water
  const auto bowl = [&](double cy, double rx, double ry, double fill) {
    if (v > cy + lw * 0.5 || ax > rx + lw) return;
    const double e = std::hypot(x / rx, std::min(0.0, v - cy) / ry), edge = lw * 0.5 / std::min(rx, ry);
    if (v <= cy && e <= 1.0) {
      body();
      if (!dry && v > cy - fill && ax < rx * 0.86) line();  // the water
    }
    if (v <= cy + lw * 0.5 && std::fabs(e - 1.0) < edge * 1.3 && v < cy - lw * 0.2) line();  // the outline
    if (std::fabs(v - cy) < lw * 0.55 && ax <= rx) line();                                   // the rim
  };
  // a falling curve: the line at height `vc(x)` with the given slope, as thick as a line across it
  const auto curve = [&](double vc, double slope) { return std::fabs(v - vc) < lw * 0.6 * std::sqrt(1 + slope * slope); };
  stem(0.0, 0.24, 0.13, 0.05);                    // the plinth
  bowl(0.34, 0.23, 0.10, 0.045);                  // the wide basin
  stem(0.34, 0.50, 0.03, 0.03);                   // the stem between the basins
  bowl(0.50, 0.11, 0.06, 0.03);                   // the upper basin
  stem(0.50, 0.60, 0.022, 0.014);                 // the finial
  if (!dry) {
    if (ax < lw * 0.6 && v > 0.60 && v < 0.72) line();                                           // the jet
    if (ax <= 0.10 && curve(0.72 - 22.0 * ax * ax, 44.0 * ax)) line();                           // its fall into the upper basin
    if (ax >= 0.11 && ax <= 0.19 && curve(0.495 - 0.15 * std::pow((ax - 0.11) / 0.08, 2), 3.8 * (ax - 0.11) / 0.08)) line();  // the overflow into the wide basin
  }
  return t;
}

// The leaf of a found secret door: a slab of the wall's own bricks, filling the doorway shut, that slides up and out of sight
// (`slide` 0 = shut, 1 = gone) and so shows the dark niche with its fountain.
Texel secretLeaf(double u, double v, const WallPoint& p, const CorridorStyle& st, double slide, bool dry) {
  const double lift = slide * (st.doorSpring + st.doorRadius + 0.05);  // the slab's lower edge
  if (v >= lift) {
    if (lift > 0 && v - lift < std::max(st.doorOutline, p.footV)) return {true, 0.0};  // its edge
    WallPoint q = p;  // the same brick as the wall round it, carried up with the slab
    q.along = std::floor(p.along + 0.5) - 0.5 + u;
    q.v = v - lift;
    q.edgeLo = q.edgeHi = false;
    return brick(q, st);
  }
  return fountainTexel(u - 0.5, v, p.footV, dry);
}

// The planks of a closed door at the back of the recess. `secret`: the doorway is a found secret door (see secretLeaf).
Texel doorLeaf(double u, double v, const WallPoint& p, const CorridorStyle& st, bool secret, double slide, bool dry) {
  if (doorDist(u, v, st) < std::max(0.02, p.footV)) return {true, 0.0};  // dark where the leaf meets the stone
  if (v < std::max(st.baseline, p.footV)) return {true, 0.0};  // the same dark line where it meets the floor as the wall has, so the door ends where the wall does
  if (secret) return secretLeaf(u, v, p, st, slide, dry);
  const double r = st.doorRadius, k = 7.0;  // 7 planks to a cell
  const double scale = st.doorSpring / 0.60;  // the bands and the handle keep their place on the leaf
  {  // a ring handle, right of centre at about waist height, hanging from a small plate
    const double hx = u - 0.5 - 0.62 * r, hy = v - 0.27, line = std::max(0.019, p.footV);
    if (std::fabs(std::hypot(hx, hy) - 0.05) < line * 0.5 || std::hypot(hx, hy - 0.058) < 0.015) return {true, 0.0};
    if (std::hypot(hx, hy) < 0.05) return {true, 0.8};  // inside the ring the door shows plain, so no seam or dot runs through it
  }
  const double plank = std::fmod(u * k, 1.0);
  if (plank < std::max(0.06, p.footAlong * k)) return {true, 0.1};
  if (std::fabs(v - 0.22 * scale) < 0.025 || std::fabs(v - 0.55 * scale) < 0.025) return {true, 0.25};  // iron bands
  return {true, 0.55 + 0.3 * hash01(static_cast<std::int64_t>(u * k), 3, p.cellX + p.cellY)};
}

// The hewn stone inside the recess. `s`: how far in from the face; `foot`: the size of a pixel there.
Texel doorJamb(double s, double v, double foot, const CorridorStyle& st, const WallPoint& p) {
  const double w = std::max(st.mortar, foot) * 0.5, h = st.doorSpring / 3;
  const double course = v / h, fy = course - std::floor(course);
  const int row = static_cast<int>(std::floor(course));
  if (std::min(fy, 1 - fy) * h < w) return {true, 0.0};
  if ((row & 1) && std::fabs(s - st.doorDepth * 0.5) < w) return {true, 0.0};  // alternate courses are two stones deep
  return {true, 0.78 + 0.22 * hash01(row, 7, p.cellX * 131 + p.cellY * 7 + p.faceDir)};
}
Texel doorSoffit(double theta, double foot, const CorridorStyle& st, const WallPoint& p) {
  double joint;
  const int idx = voussoir(theta, st.doorRadius, st, joint);
  if (joint < std::max(st.mortar, foot) * 0.5) return {true, 0.0};  // the joints run straight through from the face
  return {true, 0.78 + 0.22 * hash01(idx, 92, p.cellX * 131 + p.cellY * 7 + p.faceDir)};
}

// Floor and ceiling. `fpX`, `fpY`: how big one picture pixel is along the map's x and y axes at this point. Brightness is
// how much of the pixel the drawn thing covers (toLayers dithers it), so far detail thins out instead of turning into bars.

// A seam (grout) `d` away from this point. Near the viewer it is a solid hairline; as pixels grow it is widened to cover a
// fixed share of each pixel (so the dither keeps drawing a dotted line rather than dropping it) and that share shrinks
// with distance, so the seam thins out gradually over several more rows instead of vanishing.
double seamCover(double d, const CorridorStyle& st, double f) {
  const double c = lineCover(d, std::max(st.floorGrout, st.seamReach * f), f);
  const double fade = std::clamp((st.seamEnd - f) / (st.seamEnd - st.seamFadeFrom), 0.0, 1.0);
  return std::min(c, 1.0) * (f > st.seamFadeFrom ? fade : 1.0) * 0.99;  // < 1: only a speck is exactly 1 (see toLayers)
}

// Floor: a mosaic of rough-cut stones. Every stone is the patch of floor nearest one jittered seed point (a Voronoi
// cell), so the shapes are irregular polygons; the grout between them is a hairline.
Texel floorTexel(double wx, double wy, double fpX, double fpY, const CorridorStyle& st) {
  const double pitch = st.stone;
  const int gi = static_cast<int>(std::floor(wx / pitch)), gj = static_cast<int>(std::floor(wy / pitch));
  double d1 = 1e9, d2 = 1e9, ax = 0, ay = 0, bx = 0, by = 0;  // the nearest and second-nearest seeds
  for (int j = gj - 1; j <= gj + 1; ++j)
    for (int i = gi - 1; i <= gi + 1; ++i) {
      const double sx = (i + 0.15 + 0.7 * hash01(i, j, 3)) * pitch, sy = (j + 0.15 + 0.7 * hash01(i, j, 4)) * pitch;
      const double d = std::hypot(wx - sx, wy - sy);
      if (d < d1) { d2 = d1; bx = ax; by = ay; d1 = d; ax = sx; ay = sy; }
      else if (d < d2) { d2 = d; bx = sx; by = sy; }
    }
  const double sep = std::hypot(ax - bx, ay - by);
  const double edge = sep > 1e-9 ? (d2 * d2 - d1 * d1) / (2 * sep) : 1.0;  // distance to the seam between the two stones
  return {true, seamCover(edge, st, std::max(fpX, fpY))};
}

// Ceiling: dressed blocks of one fixed size (a cell square, as wide as a corridor), laid in staggered courses, flecked with
// random specks. Seams sit a quarter cell in (0.25 and 0.75 on alternate courses), so none lies under a wall base or
// through the middle of a corridor. `ss`: samples per pixel.
Texel ceilingTexel(double wx, double wy, double fpX, double fpY, const CorridorStyle& st, int ss) {
  const double f = std::max(fpX, fpY);
  const double u = wy - 0.25, course = std::floor(u);
  double cover = seamCover(std::fabs(u - std::round(u)), st, f);  // the course boundary nearest
  const double v = wx - (static_cast<int>(course) & 1 ? 0.75 : 0.25);  // joints: shifted half a block on alternate courses
  cover = std::max(cover, seamCover(std::fabs(v - std::round(v)), st, f));
  // Specks. A speck lights the sample its centre falls in (a whole sample, never a fraction), so as the view moves it
  // slides from pixel to pixel instead of blinking in and out. Far away more specks fall in a pixel than we want, so each
  // speck has a fixed rank and is kept only while its rank is under a cap that falls with the pixel size: a speck that
  // is dropped in the distance stays dropped, and one that shows keeps showing as it comes nearer.
  const double pitch = st.speckPitch;
  const double hx = std::max(st.speckSize, fpX / ss) * 0.5, hy = std::max(st.speckSize, fpY / ss) * 0.5;
  const double fade = std::clamp((st.seamEnd - f) / (st.seamEnd - st.seamFadeFrom), 0.0, 1.0);  // thins out with distance exactly as the seams do
  const double cap = fade * std::min(st.speckDensity, st.speckPerPixel * pitch * pitch / std::max(fpX * fpY, 1e-9));
  const int i0 = static_cast<int>(std::floor((wx - hx) / pitch)) - 1, i1 = std::min(i0 + 8, static_cast<int>(std::floor((wx + hx) / pitch)) + 1);
  const int j0 = static_cast<int>(std::floor((wy - hy) / pitch)) - 1, j1 = std::min(j0 + 8, static_cast<int>(std::floor((wy + hy) / pitch)) + 1);
  for (int j = j0; j <= j1; ++j)
    for (int i = i0; i <= i1; ++i) {
      if (hash01(i, j, 11) > cap) continue;
      const double sx = (i + 0.5 + 0.8 * (hash01(i, j, 12) - 0.5)) * pitch, sy = (j + 0.5 + 0.8 * (hash01(i, j, 13) - 0.5)) * pitch;
      if (std::fabs(wx - sx) <= hx && std::fabs(wy - sy) <= hy) return {true, 1.0};
    }
  return {true, cover};
}

Texel tileTexel(double wx, double wy, double fpX, double fpY, const CorridorStyle& st, bool ceiling, int ss) {
  return ceiling ? ceilingTexel(wx, wy, fpX, fpY, st, ss) : floorTexel(wx, wy, fpX, fpY, st);
}

// ---- Objects are drawn as clean line art: lit bars with a thin dark outline round them, and nothing (see-through)
// between the bars. `x`, `y` are in world units from the face's lower-left corner; `W`, `H` the face's size; `foot` is
// the size of a picture pixel there (no line is ever thinner than it).

// Turns a signed distance to the nearest lit shape into a texel: inside is lit, a thin band round it is dark (the
// outline that separates a lit bar from the lit wall behind it), beyond that nothing is there.
Texel lineArt(double dist, double foot) {
  if (dist <= 0) return {true, 1.0};
  if (dist < std::max(0.016, foot)) return {true, 0.0};
  return {false, 0.0};
}
// A bar of half-width `half` whose centre is `centre` away: signed distance to its edge
double barDist(double centre, double half, double foot) { return std::fabs(centre) - std::max(half, 0.6 * foot); }
// distance from (x, y) to the segment (x0, y0)-(x1, y1), minus the bar's half-width
double segDist(double x, double y, double x0, double y0, double x1, double y1, double half, double foot) {
  const double dx = x1 - x0, dy = y1 - y0;
  const double t = std::clamp(((x - x0) * dx + (y - y0) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
  return std::hypot(x - (x0 + t * dx), y - (y0 + t * dy)) - std::max(half, 0.6 * foot);
}
// distance to the nearest strand of a diagonal lattice family y = slope * (x - k * pitch), minus the strand's half-width
double strandDist(double x, double y, double slope, double pitch, double half, double foot) {
  const double t = (slope * x - y) / (slope * pitch);
  return std::fabs(t - std::round(t)) * std::fabs(slope) * pitch / std::hypot(1.0, slope) - std::max(half, 0.6 * foot);
}
double discDist(double x, double y, double cx, double cy, double r, double foot) { return std::hypot(x - cx, y - cy) - std::max(r, 0.6 * foot); }

// The elevator: an old mine cage of riveted iron. Corner posts and a heavy top frame with corner gussets; a riveted kick
// plate along the bottom; a diamond-lattice guard up to waist height, topped with a guard rail; open above that, with
// two thin uprights. Seen from inside, the back wall carries the controls (a plate with a big switch, and a lever).
// `inside`: this is the far side of the cage seen through the near one, so it is drawn with less detail.
Texel cartFace(int face, double u, double v, double foot, double W, double H, bool inside) {
  const double x = u * W, y = v * H;
  if (face == 5) return {false, 0};  // open top: the roof is a separate object
  if (face == 4) {  // the cage floor, seen from above: solid, with a joint between the planks (not when they are too small to see)
    if (foot < 0.03 && std::fabs(std::fmod(x + 4.0, 0.2) - 0.1) > 0.1 - std::max(0.006, 0.5 * foot)) return {true, 0.0};
    return {true, 1.0};
  }
  const double post = 0.028, rail = 0.018, thin = 0.01;
  const double kick = 0.05, guard = H * 0.50;  // base bar top, guard rail height
  // The frame's bars: distance to the nearest (<= 0 is on a bar). The front is open on its right third (no fence, no base
  // bar): the way in, with a door post where the fence ends.
  const auto frame = [&](double fx, double fy, bool verticalOnly) {
    const bool doorway = !inside && face == 0 && fx > W * 2 / 3;
    double dd = 1e9;
    dd = std::min(dd, barDist(std::min(fx, W - fx) - post, post, foot));                      // corner posts
    if (!verticalOnly) {
      dd = std::min(dd, barDist(fy - (H - 0.022), 0.022, foot));  // heavy top frame ...
      dd = std::min(dd, barDist(fy - (H - 0.085), thin, foot));   // ... and its lower lip
    }
    if (!doorway && !verticalOnly) {
      dd = std::min(dd, barDist(fy - guard, rail, foot));   // guard rail
      dd = std::min(dd, barDist(fy - 0.022, 0.022, foot));  // base bar
    }
    if (!inside && face == 0) dd = std::min(dd, barDist(fx - W * 2 / 3, 0.02, foot));  // the door post
    if (!inside) {  // thin uprights above the rail (near side only)
      dd = std::min(dd, barDist(fx - W / 3, thin, foot) + (fy < guard ? 1e9 : 0));
      dd = std::min(dd, barDist(fx - 2 * W / 3, thin, foot) + (fy < guard ? 1e9 : 0));
    }
    return dd;
  };
  double d = frame(x, y, false);
  const bool doorway = !inside && face == 0 && x > W * 2 / 3;
  // diamond lattice between the base bar and the guard rail
  double lat = 1e9;
  if (!doorway && y > kick - 0.01 && y < guard + 0.01) {
    lat = std::min(strandDist(x, y, 0.45, 0.21, 0.007, foot), strandDist(x, y, -0.45, 0.21, 0.007, foot));
  }
  // The light comes from the right, so the left edge of each upright on the front of the cage (corner posts, door post, thin
  // uprights) is in shade: a strip of the shade tone drawn on the bar itself (at most kShade wide, never more than half of a
  // thin bar), not where a horizontal bar crosses it. It grounds the frame in the scene and scales with distance like
  // everything else.
  if (!inside && face == 0) {
    constexpr double kShade = 0.0225;  // the strip's width, cells
    const auto leftStrip = [&](double c, double hw, bool aboveRailOnly) {
      if (aboveRailOnly && y < guard) return false;
      const double h = std::max(hw, 0.6 * foot);
      return std::fabs(x - c) <= h && x < c - h + std::min(kShade, h);
    };
    const bool inDoorway = x > W * 2 / 3;
    double horizontal = std::min(barDist(y - (H - 0.022), 0.022, foot), barDist(y - (H - 0.085), thin, foot));
    if (!inDoorway) horizontal = std::min({horizontal, barDist(y - guard, rail, foot), barDist(y - 0.022, 0.022, foot)});
    // the far-left strip is one tall unbroken line, from the foot all the way up to the top of the cart (through the crossbars and
    // the heavy top frame); the others fill the stretch of upright between two horizontal bars and stop at the bars
    if (leftStrip(post, post, false)) return {true, 1.0, true};
    if (horizontal > 0 && (leftStrip(W - post, post, false) || leftStrip(W * 2 / 3, 0.02, false) || leftStrip(W / 3, thin, true) ||
                           leftStrip(W * 2 / 3, thin, true)))
      return {true, 1.0, true};
  }
  if (foot > 0.05 && lat < 1e8 && d > 0) return {true, 0.45};  // too far away to resolve the strands: a dither of the lattice
  if (lat <= 0) return {true, 1.0};
  // controls on the back wall, seen from inside
  if (inside && face == 1) {
    const double px = W * 0.30, py = H * 0.72;
    const double plate = std::max(std::fabs(x - px) - 0.095, std::fabs(y - py) - 0.12);
    const double rr = std::hypot(x - px, y - py);
    if (plate < 0) {
      if (rr < 0.045 + 0.0 && rr > 0.0) {  // the knob: a lit disc with a dark pointer
        const double ang = std::fabs((x - px) * 0.8 - (y - py) * 0.6);  // pointer along (0.6, 0.8)
        const bool pointer = ang < std::max(0.009, foot) && ((x - px) * 0.6 + (y - py) * 0.8) > 0.0;
        return {true, pointer ? 0.0 : 1.0};
      }
      if (rr < 0.062 && rr >= 0.045) return {true, 0.0};  // dark ring round the knob
      if (plate > -std::max(0.012, foot)) return {true, 0.0};  // the plate's own outline
      // rivets in the plate's corners
      const double cx = std::fabs(x - px) - 0.07, cy = std::fabs(y - py) - 0.09;
      if (std::hypot(cx, cy) < 0.011) return {true, 0.0};
      return {true, 1.0};
    }
    if (plate < std::max(0.012, foot)) return {true, 0.0};
    // the lever: a pivot box on the floor and a bar to a ball handle, leaning left
    const double lx0 = W * 0.70, ly0 = guard + 0.02, lx1 = W * 0.62, ly1 = H * 0.80;
    const double hub = std::max(std::fabs(x - lx0) - 0.04, std::fabs(y - (guard + 0.01)) - 0.014);
    const double bar = segDist(x, y, lx0, ly0, lx1, ly1, 0.014, foot);
    const double ball = discDist(x, y, lx1, ly1 + 0.015, 0.032, foot);
    const double lev = std::min({hub, bar, ball});
    if (lev <= 0) return {true, 1.0};
    if (lev < std::max(0.012, foot)) return {true, 0.0};
  }
  if (d > 0 && lat < std::max(0.005, 0.7 * foot)) return {true, 0.0};  // a finer dark rim than the frame's, so the strands stay apart
  return lineArt(d, foot);
}

// The hoist cable: a thin lit rope with a dark twist, from the cart up to the ceiling.
Texel cableFace(double z) { return {true, std::fmod(z * 18.0, 1.0) < 0.55 ? 1.0 : 0.2}; }

// Treasure chests. The plain one is what a monster leaves: a lit body with dark line art (an outline, the lid seam, iron straps
// with rivets, planks, a lock plate with a keyhole).
Texel chestFace(int face, double u, double v, double foot, double W, double H) {
  const double x = u * W, y = v * H;
  const double ink = std::max(0.014, foot);  // line width
  const double edge = std::min(std::min(x, W - x), std::min(y, H - y));
  if (edge < ink) return {true, 0.0};  // outline round every face
  if (face == 4) return {true, 0.3};
  if (face == 5) {  // lid: a strap across and the seam round the rim
    if (edge < ink * 3 && edge > ink * 2) return {true, 0.0};
    if (std::fabs(x - W / 2) < ink * 1.5) return {true, 0.0};
    return {true, 1.0};
  }
  if (std::fabs(y - H * 0.58) < ink * 0.8) return {true, 0.0};  // the lid seam
  if (face >= 2) return {true, 1.0};  // a side is blank (straps, plate and planks are on the front)
  for (int i = 0; i < 2; ++i) {  // iron straps: a pair of dark lines each, with rivets between them
    const double cx = (i == 0 ? 0.16 : 0.84) * W, hw = 0.04;
    const double dd = std::fabs(x - cx);
    if (std::fabs(dd - hw) < ink * 0.8) return {true, 0.0};
    if (dd < hw && std::hypot(dd, std::fmod(y + 1.0, 0.12) - 0.06) < 0.018 && y < H * 0.5) return {true, 0.0};  // rivets
  }
  const double lx = std::fabs(x - W / 2), ly = y - H * 0.58;  // lock plate with a keyhole
  if (lx < 0.06 && ly > -0.09 && ly < 0.06) {
    if (std::hypot(lx, ly + 0.01) < 0.016 || (lx < 0.008 && ly < 0.0 && ly > -0.06)) return {true, 1.0};  // the keyhole
    return {true, 0.0};
  }
  if (y < H * 0.58 && std::fabs(std::fmod(y, H * 0.58 / 3.0)) < ink * 0.5 && y > ink * 2) return {true, 0.0};  // plank lines on the body
  return {true, 1.0};
}

// A faceted gem: a lit diamond with a dark outline and a dark cross through it, and a smaller diamond in the shade tone in its
// middle. Returns false if (x, y) is not on the gem.
bool gemTexel(double x, double y, double cx, double cy, double rx, double ry, double ink, Texel& out) {
  const double dx = std::fabs(x - cx), dy = std::fabs(y - cy);
  const double d = (dx / rx + dy / ry - 1.0) * std::min(rx, ry);  // about the distance to the gem's edge
  if (d > 0) return false;
  if (d > -ink * 1.1 || rx < ink * 3.5) { out = {true, d > -ink * 1.1 ? 0.0 : 1.0}; return true; }  // the outline (a tiny gem is just that)
  if (dx < ink * 0.6 || dy < ink * 0.6) { out = {true, 0.0}; return true; }                             // the facet lines
  if (dx / (rx * 0.5) + dy / (ry * 0.5) < 1.0) { out = {true, 1.0, true}; return true; }                  // the table, in shade
  out = {true, 1.0};
  return true;
}

// The level treasure's chest: the regal one. A lit body with recessed panels (the shade tone) between two heavy straps, a shield
// shaped lock plate with a keyhole, a lid with a chevron trim, a row of studs, corner brackets and three gems.
Texel regalFace(int face, double u, double v, double foot, double W, double H) {
  const double x = u * W, y = v * H;
  const double ink = std::max(0.012, foot);
  const double edge = std::min(std::min(x, W - x), std::min(y, H - y));
  if (edge < ink) return {true, 0.0};  // outline round every face
  Texel t;
  if (face == 4) return {true, 1.0, true};
  if (face == 5) {  // the lid from above: a rim, a strap across and a gem in the middle
    if (edge < ink * 3.2 && edge > ink * 2.2) return {true, 0.0};
    if (gemTexel(x, y, W / 2, H / 2, 0.06, 0.06, ink, t)) return t;
    if (std::fabs(y - H / 2) < ink * 1.4 || std::fabs(std::fabs(y - H / 2) - 0.04) < ink * 0.7) return {true, 0.0};
    return {true, 1.0};
  }
  const bool front = face <= 1;                       // the long faces; the sides are the short ones
  const double seam = H * 0.56;                       // the lid seam
  if (!front) return {true, std::fabs(y - seam) < ink * 0.7 ? 0.0 : 1.0};  // a side: just the seam, otherwise blank
  const double strapHalf = 0.026, strapC = front ? 0.07 : 0.06;
  if (std::fabs(y - seam) < ink * 0.7) return {true, 0.0};  // the lid seam
  // the lid, above the seam
  if (y > seam) {
    if (std::fabs(y - (H - 0.03)) < ink * 0.6 && edge > ink * 2) return {true, 0.0};  // a trim line along the top of the lid
    if (front) {
      if (gemTexel(x, y, W / 2, seam + (H - seam) * 0.5, 0.06, 0.06, ink, t)) return t;     // the big gem
      if (gemTexel(x, y, W * 0.26, seam + (H - seam) * 0.5, 0.036, 0.04, ink, t)) return t;  // and one either side
      if (gemTexel(x, y, W * 0.74, seam + (H - seam) * 0.5, 0.036, 0.04, ink, t)) return t;
    } else if (gemTexel(x, y, W / 2, seam + (H - seam) * 0.5, 0.034, 0.036, ink, t)) {
      return t;
    }
  }
  // corner brackets: short dark L-shapes just inside the four corners
  const double cxm = std::min(x, W - x), cym = std::min(y, H - y);
  if ((cxm < 0.055 && std::fabs(cym - 0.03) < ink * 0.6) || (cym < 0.055 && std::fabs(cxm - 0.03) < ink * 0.6)) return {true, 0.0};
  // two heavy straps with rivets
  for (const double sc : {strapC, W - strapC}) {
    const double dd = std::fabs(x - sc);
    if (dd < strapHalf + ink * 0.8) {
      if (std::fabs(dd - strapHalf) < ink * 0.8) return {true, 0.0};
      if (std::hypot(dd, std::fmod(y + 1.0, 0.07) - 0.035) < 0.011 && y < seam - 0.02) return {true, 0.0};  // rivets
      return {true, 1.0};
    }
  }
  // the lid's studs along the seam
  if (y > seam && y < seam + 0.034 && std::hypot(std::fmod(x + 4.0, 0.06) - 0.03, y - (seam + 0.017)) < 0.009 && (!front || std::fabs(x - W / 2) > 0.09)) return {true, 0.0};
  // the lock plate (front only): a shield with a keyhole, over the seam
  if (front) {
    const double lx = std::fabs(x - W / 2);
    const double ly = y - 0.025;                         // from the plate's foot
    const double halfWidth = 0.062 - std::max(0.0, 0.05 - ly) * 0.9;  // narrowing to a point at the bottom
    if (ly >= 0 && y < seam + 0.012 && lx < halfWidth) {
      if (lx > halfWidth - ink * 1.1 || ly < ink * 1.1) return {true, 0.0};
      const double ky = y - (seam - 0.075);
      if (std::hypot(lx, ky) < 0.017 || (lx < 0.007 && ky < 0.0 && ky > -0.05)) return {true, 0.0};  // the keyhole
      return {true, 1.0};
    }
  }
  // the recessed panels between the straps, in the shade tone, with a dark border
  const double px0 = strapC + strapHalf + 0.028, px1 = W - px0, py0 = 0.05, py1 = seam - 0.045;
  if (x > px0 && x < px1 && y > py0 && y < py1) {
    const double pe = std::min(std::min(x - px0, px1 - x), std::min(y - py0, py1 - y));
    return {true, pe < ink * 1.0 ? 0.0 : 1.0, pe >= ink * 1.0};
  }
  return {true, 1.0};
}

// The pedestal under the level treasure: three boxes (a low plinth, a fluted column, a cap slab). `kind` 6 / 7 / 8.
Texel pedestalFace(int kind, int face, double u, double v, double foot, double W, double H) {
  const double x = u * W, y = v * H;
  const double ink = std::max(0.012, foot);
  const double edge = std::min(std::min(x, W - x), std::min(y, H - y));
  if (edge < ink) return {true, 0.0};
  if (face == 4) return {true, 1.0, true};
  if (face == 5) {  // the top: lit, an inset border (on the cap, a plinth for the chest)
    if (kind == 8 && std::fabs(edge - 0.04) < ink * 0.7) return {true, 0.0};
    if (kind == 6) {  // the plinth: a lit rim, then fine lines across it (parallel to the front edge, so perspective draws them)
      if (edge < 0.04 - ink * 0.5) return {true, 1.0};
      if (edge < 0.04 + ink * 0.5) return {true, 0.0};
      return {true, 1.0, std::fmod(x + 4.0, 0.06) < 0.026};  // lines in the shade tone (wide enough to survive the pixel filter)
    }
    return {true, 1.0};
  }
  if (kind == 7) {  // the column: collars at both ends and flutes (grooves in the shade tone)
    if (y < 0.02 || y > H - 0.02) return {true, 0.0};
    const double flutes = std::fabs(std::fmod(x / (W / 5.0) + 10.0, 1.0) - 0.5);
    if (flutes > 0.34 && y > 0.03 && y < H - 0.03) return {true, 1.0, true};
    return {true, 1.0};
  }
  if (kind == 8) {  // the cap: a row of dentils along its face
    if (std::fabs(y - H * 0.45) < ink * 0.6) return {true, 0.0};
    if (y < H * 0.45 && std::fmod(x + 8.0, 0.045) < 0.022) return {true, 0.0};
    return {true, 1.0};
  }
  if (std::fabs(y - H * 0.5) < ink * 0.6) return {true, 0.0};  // the plinth's moulding: a lit upper band over a lower one in the shade tone
  return {true, 1.0, y < H * 0.5};
}

// The boss lair is the original's art (Monsters/1LevelBoss and its side views) stretched over the faces of a box. Each bitmap
// pixel is light (lit line or glowing mouth), dark (the rock: solid, but dark) or clear (see-through: the arch's corners).
// Many bitmap pixels can fall in one picture pixel; they are averaged (a lit pixel counts double, so thin outlines survive).
Texel bitmapTexel(const Bitmap1& bmp, double u, double v, double footU, double footV) {
  const double fx = std::clamp(footU * bmp.w, 1.0, 8.0), fy = std::clamp(footV * bmp.h, 1.0, 8.0);
  const double cx = u * bmp.w, cy = (1.0 - v) * bmp.h;
  const int x0 = static_cast<int>(std::floor(cx - fx / 2 + 0.5)), x1 = std::max(x0, static_cast<int>(std::floor(cx + fx / 2 - 0.5)));
  const int y0 = static_cast<int>(std::floor(cy - fy / 2 + 0.5)), y1 = std::max(y0, static_cast<int>(std::floor(cy + fy / 2 - 0.5)));
  int light = 0, dark = 0, all = 0;
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      const std::uint8_t p = bmp.at(x, y);
      light += p == Bitmap1::Light;
      dark += p == Bitmap1::Dark;
      ++all;
    }
  if (light + dark == 0 || (light + dark) * 2 < all) return {false, 0.0};
  return {true, std::min(1.0, 1.8 * light / (light + dark))};
}

Texel lairFace(const LairTextures* art, int face, double u, double v, double footU, double footV, bool inside) {
  // The lair is a sprite: one picture on a paper-thin slab square to the view (so it turns to face you from any angle, with
  // no seam between a front and a side picture). Only its near face is drawn.
  if (inside || face != 0) return {false, 0.0};
  if (!art) return {true, std::fabs(u - 0.5) < 0.18 && v < 0.6 ? 1.0 : 0.0};
  return bitmapTexel(art->front, u, v, footU, footV);
}

// ---------------------------------------------------------------------------------------------------------------
// The tracer
// ---------------------------------------------------------------------------------------------------------------

bool isSolid(T t) {
  switch (t) {
    case T::FLOOR: case T::O_DOOR: case T::ELEVATOR: case T::BOSS: case T::TREASURE: case T::PLAYER:
    case T::PLAYER_WEST: case T::PLAYER_EAST: case T::PLAYER_SOUTH: case T::PLAYER_NORTH:
      return false;
    default:
      return true;  // wall, closed door, secrets, soil, veins, outside the map
  }
}
bool isOpen(T t) { return !isSolid(t); }

struct BoxSpec {
  bool present = false;
  double halfA = 0, halfR = 0, height = 0;
  int kind = 0;           // 0 cart, 1 chest, 2 boss lair, 3 regal chest, 4 cable, 5 roof, 6 / 7 / 8 pedestal plinth / column / cap
  double zLo = 0;         // its bottom
  double cA = 0, cR = 0;  // its centre, in cells ahead / to the right of the player (set where it is placed)
};

// The boxes in a cell with this tile (the elevator is a cart plus its hoist cable).
int boxesFor(T t, const CorridorStyle& st, BoxSpec out[4]) {
  switch (t) {
    case T::ELEVATOR:
      out[0] = {true, st.cartHalf, st.cartHalf, st.cartHeight, 0, 0};
      out[1] = {true, st.cartHalf, st.cartHalf, st.roofHeight, 5, st.cartHeight};  // the ropes
      out[2] = {true, 0.022, 0.022, 4.0, 4, st.roofHeight};  // the hoist cable: on up through the ceiling, to the top of the picture
      return 3;
    case T::TREASURE:  // the level treasure: its regal chest on a pedestal (plinth, column, cap: 0.24 high in all)
      out[0] = {true, 0.30, 0.40, 0.04, 6, 0};
      out[1] = {true, 0.17, 0.26, 0.19, 7, 0.04};
      out[2] = {true, 0.25, 0.35, 0.24, 8, 0.19};
      out[3] = {true, st.treasureHalfA, st.treasureHalfR, 0.24 + st.treasureHeight, 3, 0.24};
      return 4;
    case T::BOSS: out[0] = {true, st.lairHalfA, st.lairHalfR, st.lairHeight, 2, 0}; return 1;
    default: return 0;
  }
}

struct Hit {
  bool hit = false;
  double t = kInf;
  Surface surface = Surface::Void;
  double albedo = 0;
  bool sideFace = false;
  Tint tint = Tint::None;
};

class Tracer {
 public:
  Tracer(const TileLookup& lookup, const CorridorCamera& cam, const CorridorStyle& st, double focal) : lookup_(lookup), cam_(cam), st_(st), focal_(focal) {}

  Fragment trace(double dr, double dz) {
    const double eye = cam_.eyeHeight;
    const double off = cam_.backOff;  // the eye stands this far back from the middle of its cell: the world is this far further ahead
    const double foot0 = cam_.supersample * cam_.lineScale / focal_;  // a pixel of the picture (not of a sample): lines are never thinner than this
    int ca = off > 0.5 ? -1 : off < -0.5 ? 1 : 0, cr = 0;  // the cell the eye is in: the one behind / ahead of the player's while it is more than half a cell away
    const int ca0 = ca;
    double t = 0;
    // Once the ray has gone up through an empty stretch of ceiling it keeps going, above the walls, looking for only the
    // hoist cable (which runs up through the ceiling to the top of the picture); `ceiling` is what it shows if it finds nothing.
    bool above = false;
    Hit ceiling;
    for (int step = 0; step < 96; ++step) {
      const double tA = ca + 0.5 + off;
      const double tR = dr > 1e-12 ? (cr + 0.5) / dr : dr < -1e-12 ? (cr - 0.5) / dr : kInf;
      const bool crossA = tA <= tR;
      const double tExit = std::min(tA, tR);
      if (t > cam_.drawDistance) return above ? finish(ceiling) : Fragment{};
      const TileInfo here = lookup_(ca, cr);

      // inside this (open) cell: floor, ceiling, and an object standing in it
      Hit best;
      if (!above && dz < -1e-12) {
        const double tf = eye / -dz;
        if (tf >= t && tf < tExit) { const Hit edge = doorFloorEdge(ca, cr, tf, dr); best = edge.hit ? edge : planeHit(tf, dr, eye, false); }
      }
      if (!above && dz > 1e-12) {
        const double tc = (1.0 - eye) / dz;
        if (tc >= t && tc < tExit && tc < best.t) best = planeHit(tc, dr, 1.0 - eye, true);
      }
      BoxSpec boxes[8];
      int nBoxes = 0;
      if ((here.tile == T::ELEVATOR || here.tile == T::TREASURE || here.tile == T::BOSS) && !(ca == 0 && cr == 0) && !(ca == ca0 && cr == 0))  // you stand in your own cell (and the eye is in its)
        nBoxes = boxesFor(here.tile, st_, boxes);
      for (int i = 0; i < nBoxes; ++i) { boxes[i].cA = ca + off; boxes[i].cR = cr; }
      if (cam_.heldObject != 0 && ca <= 1) {  // something set down in front of you (a chest you can open)
        BoxSpec held[4];
        const int nHeld = boxesForHeld(held);
        for (int i = 0; i < nHeld; ++i) { boxes[nBoxes] = held[i]; boxes[nBoxes++].cA += off; }
      }
      for (int i = 0; i < nBoxes; ++i) {
        if (above && boxes[i].kind != 4) continue;
        Hit o = boxHit(boxes[i], dr, dz, t, tExit, foot0);
        if (!o.hit) continue;
        const bool nearer = o.t < best.t;
        const bool tieOnPlane = o.surface == Surface::Shade && o.t < best.t + 1e-6;  // the cage floor lies in the corridor floor's plane: it wins that tie
        const bool seeThroughPlane = here.tile == T::ELEVATOR && best.surface == Surface::Background && best.albedo < 0.02;  // an empty stretch of ceiling: what hangs through it (the hoist cable) shows
        if (nearer || tieOnPlane || seeThroughPlane) best = o;
      }
      if (!above && st_.doorRelief > 0) {  // the dressing of a doorway stands out from the wall: it is in front of the face, in this cell
        const int na2 = crossA ? ca + 1 : ca, nr2 = crossA ? cr : cr + (dr > 0 ? 1 : -1);
        const TileInfo nx = lookup_(na2, nr2);
        if (nx.tile == T::C_DOOR || nx.tile == T::SECRET_FOUND || nx.tile == T::O_DOOR) {
          const FaceCtx fc = faceAt(crossA, dr > 0, tExit, dr, na2, nr2, nx);
          if (fc.alongPassage(doorAxisAhead(na2, nr2))) {
            const Hit rh = reliefHit(nx, fc, eye + tExit * dz, tExit * foot0, dr, dz, tExit, t);
            if (rh.hit && rh.t < best.t) best = rh;
          }
        }
      }
      if (best.hit) {
        const bool emptyCeiling = !above && here.tile == T::ELEVATOR && dz > 1e-12 && best.surface == Surface::Background && best.albedo < 0.02;
        if (!emptyCeiling) return finish(best);
        above = true;
        ceiling = best;
      }

      // cross into the next cell
      const int na = crossA ? ca + 1 : ca;
      const int nr = crossA ? cr : cr + (dr > 0 ? 1 : -1);
      const double z = eye + tExit * dz;
      if (tExit > cam_.drawDistance) return above ? finish(ceiling) : Fragment{};
      const TileInfo next = lookup_(na, nr);
      const double foot = tExit * foot0;
      const FaceCtx face = faceAt(crossA, dr > 0, tExit, dr, na, nr, next);
      if (above) {
        // (above the walls: nothing to cross but the cable's own cell)
      } else if (isSolid(next.tile)) {
        if ((next.tile == T::C_DOOR || next.tile == T::SECRET_FOUND) && face.alongPassage(doorAxisAhead(na, nr))) {
          const Hit h = doorwayHit(next, face, z, foot, dr, dz, tExit, /*opening=*/false);
          if (h.hit) return finish(h);
        } else {
          const Texel tx = solidFaceTexel(next, na, nr, ca, cr, face, z, foot, dr);
          if (tx.solid) return finish({true, tExit, Surface::Walls, tx.albedo, !crossA});
        }
      } else {
        // an open door's frame stands across its passage as you enter its cell (not around you while you stand in it)
        const bool nextDoor = next.tile == T::O_DOOR && face.alongPassage(doorAxisAhead(na, nr));
        if (nextDoor) {
          const Hit h = doorwayHit(next, face, z, foot, dr, dz, tExit, /*opening=*/true);
          if (h.hit) return finish(h);
        }
      }
      t = tExit;
      ca = na;
      cr = nr;
    }
    return above ? finish(ceiling) : Fragment{};
  }

 private:
  struct FaceCtx {
    bool crossA;       // the face is crossed moving ahead (its normal is along the ahead axis)
    double alongMap;   // the map coordinate along the face
    int dir;           // 0..3: which map direction the face looks
    bool alongPassage(bool passageAhead) const { return crossA == passageAhead; }
  };

  FaceCtx faceAt(bool crossA, bool movingRight, double tHit, double dr, int, int, const TileInfo&) const {
    const MapFrame& f = cam_.frame;
    const double A = tHit - cam_.backOff, R = tHit * dr;
    const double mx = f.px + A * f.ax + R * f.sx, my = f.py + A * f.ay + R * f.sy;
    FaceCtx c{};
    c.crossA = crossA;
    int nx, ny;  // the face's normal in map terms (towards the viewer)
    if (crossA) {
      c.alongMap = f.sx != 0 ? mx : my;
      nx = -f.ax;
      ny = -f.ay;
    } else {
      c.alongMap = f.ax != 0 ? mx : my;
      nx = movingRight ? -f.sx : f.sx;
      ny = movingRight ? -f.sy : f.sy;
    }
    c.dir = nx > 0 ? 1 : nx < 0 ? 3 : ny > 0 ? 2 : 0;
    return c;
  }

  // `foot`: the size of a pixel at the face's distance; along the face it is bigger when the face is seen at a slant
  WallPoint wallPoint(const FaceCtx& f, double z, double foot, int cx, int cy, double dr) const {
    const double along = f.crossA ? foot : foot / std::max(std::fabs(dr), 0.02);
    WallPoint w{f.alongMap, z, along, foot, cx, cy, f.dir};
    w.slanted = !f.crossA;
    return w;
  }

  // true if a door in this relative cell is passed along the ahead axis (its open neighbours are ahead / behind)
  bool doorAxisAhead(int a, int r) const {
    const bool openA = isOpen(lookup_(a - 1, r).tile) || isOpen(lookup_(a + 1, r).tile);
    const bool openR = isOpen(lookup_(a, r - 1).tile) || isOpen(lookup_(a, r + 1).tile);
    return openA || !openR;
  }

  // Where the wall face of solid cell (na, nr), seen from open cell (ca, cr), ends or turns at its low / high end.
  // Between flat neighbouring segments there is no line.
  void faceEdges(const FaceCtx& face, int na, int nr, int ca, int cr, bool& lo, bool& hi) const {
    const MapFrame& f = cam_.frame;
    int dA = 0, dR = 0;  // the relative direction in which the face's `along` map coordinate grows
    if (face.crossA) dR = (f.sx != 0 ? f.sx : f.sy) > 0 ? 1 : -1;
    else dA = (f.ax != 0 ? f.ax : f.ay) > 0 ? 1 : -1;
    // At each end: a flat continuation draws no line; an inside corner (the next cell along is solid, with the other
    // wall running away from us) is one line shared by both walls, so only the front-facing wall draws it; an outside
    // corner (the wall just ends) always draws its line.
    const auto line = [&](int sgn) {
      const bool nextSolid = isSolid(lookup_(na + sgn * dA, nr + sgn * dR).tile);
      const bool beside = isOpen(lookup_(ca + sgn * dA, cr + sgn * dR).tile);
      if (nextSolid && beside) return false;       // flat
      if (nextSolid && !beside) return face.crossA;  // inside corner
      return true;                                   // outside corner
    };
    lo = line(-1);
    hi = line(+1);
  }

  Texel solidFaceTexel(const TileInfo& cell, int na, int nr, int ca, int cr, const FaceCtx& face, double z, double foot, double dr) const {
    WallPoint p = wallPoint(face, z, foot, cell.mx, cell.my, dr);
    faceEdges(face, na, nr, ca, cr, p.edgeLo, p.edgeHi);
    switch (cell.tile) {
      case T::SECRET:
        if (cam_.secretTell) {  // the torch flickers: the shut door's outline shows faintly in the wall, like a doorway's
          const double dist = doorDist(p.along - (std::floor(p.along + 0.5) - 0.5), p.v, st_);
          if (dist >= 0 && dist < std::max(st_.doorOutline, p.footV)) return {true, 0.0};
        }
        return brick(p, st_);
      default: return brick(p, st_);
    }
  }

  // The doorway in the wall face crossed at `tExit`: the face's dressed stone, or, where the ray goes in through the opening,
  // the hewn recess behind it: the jambs, the arch's underside and, for a closed door, its planks at the back. An open door
  // (`opening`) has no leaf: a ray that crosses the recess without touching its stone goes on (no hit).
  Hit doorwayHit(const TileInfo& cell, const FaceCtx& face, double z, double foot, double dr, double dz, double tExit, bool opening) const {
    WallPoint p = wallPoint(face, z, foot, cell.mx, cell.my, dr);
    const bool side = !face.crossA;
    const Texel front = doorFront(p, st_);
    if (front.solid) return {true, tExit, Surface::Walls, front.albedo, side};

    // March the ray through the recess in the face's own terms: u across the doorway (0..1 over the cell), z up, s in.
    const MapFrame& f = cam_.frame;
    const double u0 = p.along - (std::floor(p.along + 0.5) - 0.5);
    const double du = face.crossA ? dr * (f.sx != 0 ? f.sx : f.sy) : (f.ax != 0 ? f.ax : f.ay);  // per unit of t
    const double ds = face.crossA ? 1.0 : std::fabs(dr);
    const double tBack = std::min(ds > 1e-9 ? st_.doorDepth / ds : 4.0, 4.0);  // the back of the recess
    const auto inside = [&](double tt) { return doorDist(u0 + du * tt, z + dz * tt, st_) >= 0; };
    if (inside(tBack)) {
      if (opening) return {};
      const Texel leaf = doorLeaf(u0 + du * tBack, z + dz * tBack, p, st_, cell.tile == T::SECRET_FOUND, cam_.secretSlide, cam_.fountainDry);
      return {true, tExit + tBack, leaf.object ? Surface::Objects : leaf.shade ? Surface::Shade : Surface::Walls, leaf.albedo, side,
              leaf.object ? Tint::Water : Tint::None};  // (the fountain in its niche)
    }
    double lo = 0, hi = tBack;  // inside at lo, outside at hi: the doorway's outline is convex, so it is crossed once
    for (int i = 0; i < 14; ++i) {
      const double mid = 0.5 * (lo + hi);
      (inside(mid) ? lo : hi) = mid;
    }
    const double uu = u0 + du * hi, zz = z + dz * hi, s = ds * hi, ft = foot * 1.0;
    if (zz < 0) {  // out through the floor of the recess: it is the corridor's floor, running in under the door
      if (opening) return {};
      return {true, tExit + hi, Surface::Background, 0.0, false};  // the threshold: plain stone, the floor's pattern stops at the door
    }
    if (zz > st_.doorSpring) {
      const Texel tx = doorSoffit(std::atan2(zz - st_.doorSpring, uu - 0.5), ft, st_, p);
      return {true, tExit + hi, Surface::Walls, tx.albedo * st_.soffitAlbedo, false};
    }
    const Texel tx = doorJamb(s, zz, ft, st_, p);
    return {true, tExit + hi, Surface::Walls, tx.albedo, true};
  }

  // The doorway's dressed stone stands `doorRelief` out from the wall: the part of the ray between `tStart` and the face crossed
  // at `tExit` is tested against that raised frame, so its base sits lower in the picture than the wall's (it stands on the
  // floor in front of the wall) and, seen at an angle, its sides show. Where it meets the ray head-on it is the dressed stone
  // itself; where the ray enters through a side it is plain stone.
  Hit reliefHit(const TileInfo& cell, const FaceCtx& face, double z, double foot, double dr, double dz, double tExit, double tStart) const {
    const WallPoint p = wallPoint(face, z, foot, cell.mx, cell.my, dr);
    const MapFrame& f = cam_.frame;
    const double u0 = p.along - (std::floor(p.along + 0.5) - 0.5);
    const double du = face.crossA ? dr * (f.sx != 0 ? f.sx : f.sy) : (f.ax != 0 ? f.ax : f.ay);
    const double ds = face.crossA ? 1.0 : std::fabs(dr);
    // How far (in t) before the face the frame reaches (the same all round: the whole ring of stones stands out from the wall)
    const auto depthAt = [&](double) { return st_.doorRelief / std::max(ds, 1e-9); };
    const double span = std::min(depthAt(0), tExit - tStart);
    if (span <= 0) return {};
    Texel tx;
    const auto inFrame = [&](double back) {  // `back`: how far before the face
      const double v = z - dz * back;
      if (v < 0 || v >= 1 || back > depthAt(v)) return false;
      const double uu = u0 - du * back;
      return doorSurround(uu, v, p, st_, tx, true);
    };
    constexpr int K = 12;
    for (int k = 0; k <= K; ++k) {
      const double back = span * (1.0 - static_cast<double>(k) / K);
      if (!inFrame(back)) continue;
      double in = back;
      if (k > 0) {
        double out = span * (1.0 - static_cast<double>(k - 1) / K);  // outside at `out`, inside at `in`
        for (int i = 0; i < 12; ++i) {
          const double mid = 0.5 * (out + in);
          (inFrame(mid) ? in : out) = mid;
        }
        inFrame(in);
      }
      const double sv = z - dz * in;
      if (k == 0 || in >= depthAt(sv) - span * 0.02)  // entered through the frame's front face
        return {true, tExit - in, Surface::Walls, tx.albedo, !face.crossA};
      // entered through a side (the inside of the frame): the same stones as the front face, so their
      // joints run on across it
      Texel side{true, 1.0};
      if (sv >= 0 && sv < 1) doorSurroundJoints(u0 - du * in, sv, p, st_, side);
      if (sv < std::max(st_.baseline, 2.0 * foot)) side.albedo = 0.0;  // a dark base line: the inside of the frame ends no lower than the front stones' base
      return {true, tExit - in, Surface::Walls, side.albedo * st_.reliefSideAlbedo, true};
    }
    return {};
  }

  // The floor point (ahead `tf`, right `tf * dr`) in cell (ca, cr), if it is within a few pixels of a closed door's face: the floor's
  // pattern stops short of the door and leaves a plain sill (a dark hit).
  Hit doorFloorEdge(int ca, int cr, double tf, double dr) const {
    const double sill = st_.doorSill, off = cam_.backOff;
    const double toAhead = ca + 0.5 + off - tf, toBehind = tf - (ca - 0.5 + off), R = tf * dr, toRight = cr + 0.5 - R, toLeft = R - (cr - 0.5);
    const auto check = [&](double dist, int a, int r, bool crossingAhead) -> Hit {
      if (dist >= sill) return {};
      const TileInfo n = lookup_(a, r);
      if ((n.tile != T::C_DOOR && n.tile != T::SECRET_FOUND) || crossingAhead != doorAxisAhead(a, r)) return {};
      return {true, tf, Surface::Background, 0.0, false};
    };
    Hit h = check(toAhead, ca + 1, cr, true);
    if (!h.hit) h = check(toBehind, ca - 1, cr, true);
    if (!h.hit) h = check(toRight, ca, cr + 1, false);
    if (!h.hit) h = check(toLeft, ca, cr - 1, false);
    return h;
  }

  Hit planeHit(double t, double dr, double dist, bool ceiling) const {
    const double fpAhead = cam_.supersample * cam_.lineScale * t * t / (dist * focal_), fpRight = cam_.supersample * cam_.lineScale * t / focal_;
    const MapFrame& f = cam_.frame;
    const double fpX = std::abs(f.ax) * fpAhead + std::abs(f.sx) * fpRight, fpY = std::abs(f.ay) * fpAhead + std::abs(f.sy) * fpRight;
    const Texel tx = tileTexel(mapX(t - cam_.backOff, t * dr), mapY(t - cam_.backOff, t * dr), fpX, fpY, st_, ceiling, cam_.supersample);
    return {true, t, Surface::Background, tx.albedo, false};
  }
  double mapX(double A, double R) const { return cam_.frame.px + A * cam_.frame.ax + R * cam_.frame.sx; }
  double mapY(double A, double R) const { return cam_.frame.py + A * cam_.frame.ay + R * cam_.frame.sy; }

  // The ray against an object box standing in cell (ca, cr), between t0 and t1.
  int boxesForHeld(BoxSpec out[4]) const {
    if (cam_.heldObject == 1) {  // the plain chest a monster leaves
      out[0] = {true, st_.chestHalfA, st_.chestHalfR, st_.chestHeight, 1, 0};
      out[0].cA = cam_.heldAhead;
      return 1;
    }
    const int n = boxesFor(T::TREASURE, st_, out);  // the level treasure, on its pedestal
    for (int i = 0; i < n; ++i) out[i].cA = cam_.heldAhead;
    return n;
  }

  // The ropes that hold the cage: four thick hoist ropes run from the cage's top corners up to one point over its middle,
  // where the single cable to the ceiling begins. Each is a real 3D rod of a fixed thickness (so the far two come out
  // thinner than the near two, by perspective), drawn as a lit rope with a thin dark rim and nothing between them.
  Hit ropeHit(const BoxSpec& b, double dr, double dz, double t0, double t1, double foot0) const {
    constexpr double kRadius = 0.013;
    // The ropes start inside the cage's corners by their own thickness (and rim), so nothing of them reaches past the
    // cage's sides: no overhang, however near or far the cage is.
    const double refFoot = std::max(b.cA, 0.5) * foot0;
    const double inset = std::max(kRadius, 0.5 * refFoot) + std::max(0.010, 0.8 * refFoot);
    const double S = std::min(t1, cam_.drawDistance) + 0.5;
    const double D[3] = {1.0, dr, dz};
    const double A[3] = {0.0, 0.0, cam_.eyeHeight};
    Hit best;
    for (int k = 0; k < 4; ++k) {
      const double C[3] = {b.cA + (k & 1 ? b.halfA - inset : -b.halfA + inset), b.cR + (k & 2 ? b.halfR - inset : -b.halfR + inset), b.zLo};
      const double E[3] = {b.cA - C[0], b.cR - C[1], b.height - b.zLo};
      const double r[3] = {A[0] - C[0], A[1] - C[1], A[2] - C[2]};
      const double d1[3] = {S * D[0], S * D[1], S * D[2]};
      const auto dot = [](const double* u, const double* v) { return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]; };
      const double a = dot(d1, d1), e = dot(E, E), f = dot(E, r), c = dot(d1, r), bb = dot(d1, E);
      const double denom = a * e - bb * bb;
      double sp = denom > 1e-12 ? std::clamp((bb * f - c * e) / denom, 0.0, 1.0) : 0.0;  // along the ray (0..1 of S)
      double up = (bb * sp + f) / e;                                                      // along the rope (0..1)
      if (up < 0) { up = 0; sp = std::clamp(-c / a, 0.0, 1.0); }
      else if (up > 1) { up = 1; sp = std::clamp((bb - c) / a, 0.0, 1.0); }
      const double th = sp * S;
      if (th < t0 - 1e-9 || th > t1 || th >= best.t) continue;
      double dist2 = 0;
      for (int i = 0; i < 3; ++i) {
        const double diff = (A[i] + sp * d1[i]) - (C[i] + up * E[i]);
        dist2 += diff * diff;
      }
      const double foot = th * foot0, dist = std::sqrt(dist2), rad = std::max(kRadius, 0.5 * foot);
      const Surface tone = k & 1 ? Surface::Shade : Surface::Objects;  // k & 1: the corners on the far side
      if (dist < rad) best = {true, th, tone, std::fmod(up * 9.0, 1.0) < 0.6 ? 1.0 : 0.35, false, Tint::Lift};  // the twist of the rope
      else if (dist < rad + std::max(0.010, 0.8 * foot)) best = {true, th, tone, 0.0, false, Tint::Lift};        // its dark rim
    }
    return best;
  }

  Hit boxHit(const BoxSpec& b, double dr, double dz, double t0, double t1, double foot0) const {
    if (b.kind == 5) return ropeHit(b, dr, dz, t0, t1, foot0);
    const double ca = b.cA, cr = b.cR;
    const double eye = cam_.eyeHeight;
    double tNear = -kInf, tFar = kInf;
    int faceNear = -1, faceFar = -1;
    const auto slab = [&](double origin, double dir, double lo, double hi, int faceLo, int faceHi) {
      if (std::fabs(dir) < 1e-12) return origin >= lo && origin <= hi;
      double a = (lo - origin) / dir, c = (hi - origin) / dir;
      int fa = faceLo, fc = faceHi;
      if (a > c) { std::swap(a, c); std::swap(fa, fc); }
      if (a > tNear) { tNear = a; faceNear = fa; }
      if (c < tFar) { tFar = c; faceFar = fc; }
      return true;
    };
    if (!slab(0, 1, ca - b.halfA, ca + b.halfA, 0, 1)) return {};
    if (!slab(0, dr, cr - b.halfR, cr + b.halfR, 2, 3)) return {};
    if (!slab(eye, dz, b.zLo, b.height, 4, 5)) return {};
    if (tNear > tFar || tFar < t0 || tNear > t1) return {};
    for (int pass = 0; pass < 2; ++pass) {  // near face, then the far one (a cage and a chest are drawn from inside too)
      const double th = pass == 0 ? tNear : tFar;
      const int face = pass == 0 ? faceNear : faceFar;
      if (th < t0 - 1e-9 || th > t1 || face < 0) continue;
      const double a = th, r = th * dr, z = eye + th * dz;
      double u, v;
      switch (face) {
        case 0: case 1: u = (r - (cr - b.halfR)) / (2 * b.halfR); v = (z - b.zLo) / (b.height - b.zLo); break;
        case 2: case 3: u = (a - (ca - b.halfA)) / (2 * b.halfA); v = (z - b.zLo) / (b.height - b.zLo); break;
        default: u = (a - (ca - b.halfA)) / (2 * b.halfA); v = (r - (cr - b.halfR)) / (2 * b.halfR); break;
      }
      u = std::clamp(u, 0.0, 1.0);
      v = std::clamp(v, 0.0, 1.0);
      const double foot = th * foot0;
      // the face's size in world units, for the line art
      const double W = face <= 1 ? 2 * b.halfR : 2 * b.halfA;                      // faces 0/1 span the right axis, 2/3 and the top the ahead axis
      const double H = face >= 4 ? 2 * b.halfR : b.height - b.zLo;
      Texel tx = b.kind == 0 ? cartFace(face, u, v, foot, W, H, pass == 1)
                 : b.kind == 1 ? chestFace(face, u, v, foot, W, H)
                 : b.kind == 3 ? regalFace(face, u, v, foot, W, H)
                 : b.kind >= 6 ? pedestalFace(b.kind, face, u, v, foot, W, H)
                 : b.kind == 4 ? cableFace(z)
                                : lairFace(cam_.lair, face, u, v, foot / W, foot / H, pass == 1);
      if ((b.kind == 3 || b.kind >= 6) && face == 2) tx.shade = true;  // light comes from the right: the left side of a solid object is in shade
      // the cage's shade: its floor and its back wall (the far end, face 0 / 1); its side walls keep the normal colour
      const bool shaded = tx.shade || (b.kind == 0 && (face == 4 || (pass == 1 && face <= 1)));
      // cart and cables: the elevator; chests, the regal chest and the pedestal pieces: treasure; the rest is the boss's lair
      const Tint tint = b.kind == 0 || b.kind == 4 ? Tint::Lift : b.kind == 1 || b.kind == 3 || b.kind >= 6 ? Tint::Treasure : Tint::Lair;
      if (tx.solid) return {true, th, shaded ? Surface::Shade : Surface::Objects, tx.albedo, face >= 2 && face <= 3, tint};
    }
    return {};
  }

  Fragment finish(const Hit& h) const {
    Fragment f;
    f.surface = h.surface;
    const double side = h.sideFace && h.surface == Surface::Walls ? st_.sideFaceAlbedo : 1.0;
    f.albedo = static_cast<std::uint8_t>(std::clamp(h.albedo * side, 0.0, 1.0) * 255.0 + 0.5);
    const double shade = 1.0 - cam_.fog * std::min(1.0, h.t / cam_.drawDistance);
    f.shade = static_cast<std::uint8_t>(std::clamp(shade, 0.0, 1.0) * 255.0 + 0.5);
    f.depth = static_cast<float>(h.t);
    f.tint = h.tint;
    return f;
  }

  const TileLookup& lookup_;
  const CorridorCamera& cam_;
  const CorridorStyle& st_;
  double focal_;
};

}  // namespace

ViewFrame renderCorridor(int w, int h, const TileLookup& lookup, const CorridorCamera& cam, const CorridorStyle& style) {
  const int ss = std::max(1, cam.supersample);
  ViewFrame f;
  f.ss = ss;
  f.w = w * ss;
  f.h = h * ss;
  f.px.resize(static_cast<size_t>(f.w) * static_cast<size_t>(f.h));
  const double focal = (f.w / 2.0) / std::tan(cam.fovDegrees * 3.14159265358979 / 360.0);
  // Every pixel is traced on its own from the const map, camera and style (no shared state), so rows can go to any thread.
  // Rows cost very differently (a row through the walls is cheaper than one across the floor), so threads take the next
  // free row rather than a fixed band.
  Fragment* const out = f.px.data();
  std::atomic<int> nextRow{0};
  const auto work = [&] {
    Tracer tracer(lookup, cam, style, focal);
    for (int y = nextRow.fetch_add(1); y < f.h; y = nextRow.fetch_add(1)) {
      const double dz = (f.h / 2.0 - (y + 0.5)) / focal;
      Fragment* row = out + static_cast<size_t>(y) * static_cast<size_t>(f.w);
      for (int x = 0; x < f.w; ++x) row[x] = tracer.trace((x + 0.5 - f.w / 2.0) / focal, dz);
    }
  };
  const int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, std::max(1, f.h / 16));
  std::vector<std::future<void>> jobs;
  for (int t = 1; t < threads; ++t) jobs.push_back(std::async(std::launch::async, work));
  work();  // this thread works too
  for (auto& j : jobs) j.get();
  return f;
}

ViewLayers toLayers(const ViewFrame& f) {
  static constexpr int kBayer[8][8] = {{0, 32, 8, 40, 2, 34, 10, 42},  {48, 16, 56, 24, 50, 18, 58, 26}, {12, 44, 4, 36, 14, 46, 6, 38},
                                       {60, 28, 52, 20, 62, 30, 54, 22}, {3, 35, 11, 43, 1, 33, 9, 41},   {51, 19, 59, 27, 49, 17, 57, 25},
                                       {15, 47, 7, 39, 13, 45, 5, 37},   {63, 31, 55, 23, 61, 29, 53, 21}};
  constexpr double kDitherSpan = 0.55;
  const int ss = f.ss, w = f.w / ss, h = f.h / ss;
  ViewLayers L{Bitmap1(w, h), Bitmap1(w, h), Bitmap1(w, h), Bitmap1(w, h)};
  L.native = true;
  for (auto& t : L.tints) t.lit = t.shade = Bitmap1(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      // filter the ss x ss samples of this pixel: the surface most of them show, and how much light they add up to
      double sum[5] = {0, 0, 0, 0, 0};
      bool speck = false;  // a floor/ceiling sample that is a speck (albedo exactly 1): it lights the whole pixel
      int count[5] = {0, 0, 0, 0, 0}, lineSamples[5] = {0, 0, 0, 0, 0}, lineBias[5] = {0, 0, 0, 0, 0};
      Tint tintOf[5] = {Tint::None, Tint::None, Tint::None, Tint::None, Tint::None};  // the tint of the (first) object sample of each surface
      for (int j = 0; j < ss; ++j)
        for (int i = 0; i < ss; ++i) {
          const Fragment& p = f.at(x * ss + i, y * ss + j);
          const int k = static_cast<int>(p.surface);
          if (count[k]++ == 0) tintOf[k] = p.tint;
          if (p.surface == Surface::Background && p.albedo == 255) speck = true;
          sum[k] += (p.albedo / 255.0) * (p.shade / 255.0);
          if (p.albedo < 8) {  // mortar / joint / outline samples
            ++lineSamples[k];
            lineBias[k] += (j >= ss / 2 ? 1 : -1) + (i >= ss / 2 ? 1 : -1);  // which side of the pixel they are on
          }
        }
      // The surface most of the pixel's samples show. A tie goes to the lit things (objects, then walls) over shade, so a thin lit
      // bar is not eaten at the end of a shade strip, then to the background.
      static constexpr int kRank[5] = {0, 1, 3, 4, 2};  // by Surface: void, background, walls, objects, shade
      int best = 0;
      for (int k = 1; k < 5; ++k)
        if (count[k] > count[best] || (count[k] == count[best] && count[k] > 0 && kRank[k] > kRank[best])) best = k;
      if (best == 0 || count[best] == 0) continue;
      const bool plane = best == static_cast<int>(Surface::Background);
      const bool shaded = best == static_cast<int>(Surface::Shade);
      const int lines = lineSamples[best], all = ss * ss;
      // Dark lines are a hard decision (dark when more than half of the pixel's samples are line), so a line is one or two
      // crisp pixels wide, never a dotted trail; the dither only handles the distance fade of what is left.
      // A line exactly half-covering a pixel is split between two neighbours; it goes to the lower / right one, so a
      // one-pixel line stays one pixel wide wherever it falls.
      const bool darkLine = !plane && (lines * 2 > all || (lines * 2 == all && lines > 0 && lineBias[best] > 0));
      const int others = count[best] - lines;
      const double value = others > 0 ? sum[best] / others * (static_cast<double>(others) / all > 0.5 ? 1.0 : static_cast<double>(others) / all * 2) : 0.0;
      const double bayer = (kBayer[y & 7][x & 7] + 0.5) / 64.0;
      if (plane) {
        // Floor and ceiling: how much of the pixel the seam or speck covers, dithered. A pixel mostly covered is lit, one barely
        // touched is clear, in between it is lit in proportion, so a line thins out gradually (solid, then dotted, then gone).
        const double cover = sum[best] / all;
        if (!speck && cover <= 0.12 + 0.43 * bayer) continue;
        L.background.px[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = Bitmap1::Light;
        continue;
      }
      const double threshold = bayer * kDitherSpan;
      const bool lit = !darkLine && value > threshold;
      const Tint tint = tintOf[best];
      Bitmap1& layer = tint != Tint::None && (shaded || best == static_cast<int>(Surface::Objects))
                           ? (shaded ? L.tints[static_cast<int>(tint) - 1].shade : L.tints[static_cast<int>(tint) - 1].lit)
                           : plane ? L.background : shaded ? L.shade : best == static_cast<int>(Surface::Walls) ? L.walls : L.objects;
      layer.px[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = lit ? Bitmap1::Light : Bitmap1::Dark;
    }
  return L;
}

}  // namespace laby
