#pragma once
#include <cstdint>
#include <functional>
#include <vector>

#include "core/Enums.hpp"
#include "ui/ViewPainter.hpp"

namespace laby {

// The procedural first-person view: for every pixel of the picture a ray is traced through the tile grid (a small
// grid ray caster), so walls, doors, floor, ceiling and objects are computed from the map at whatever size the
// picture is, instead of being pasted from pictures made for one screen size.
//
// Two stages, so the art can improve without touching the geometry:
//   1. renderCorridor():  tiles + camera  ->  ViewFrame   (per pixel: which surface, how bright its texture is, how far)
//   2. toLayers():        ViewFrame           ->  ViewLayers   (the 1-bit look: brightness becomes light/dark pixels)
// A future colour or higher-resolution look reads the ViewFrame directly (it has the surface kind, texture brightness and
// depth of every pixel) and skips stage 2.
//
// HOW TO IMPROVE THE ART: the textures are the small functions at the top of CorridorRenderer.cpp (brick, door, floor,
// ceiling, the cart, the chest, the boss lair). Each one takes a point on a surface (where along it, how high, how
// big a pixel is there) and returns whether it is solid and how bright. Change a function, or add a material, and
// everything else (perspective, doors, depth, shading) keeps working. Proportions live in CorridorStyle.

// Which tile is at a position relative to the player: `ahead` cells in front of them, `right` cells to their right
// (negative = behind / left). `mx`, `my` are that cell's map coordinates, used to seed the texture so a wall looks the
// same whichever way you look at it.
struct TileInfo {
  TileClass tile = TileClass::WALL;
  int mx = 0, my = 0;
};
using TileLookup = std::function<TileInfo(int ahead, int right)>;

// How the player's frame maps onto the map: map = (px, py) + ahead * (ax, ay) + right * (sx, sy).
struct MapFrame {
  int px = 0, py = 0;
  int ax = 0, ay = -1;  // facing north
  int sx = 1, sy = 0;
};

struct CorridorCamera {
  double fovDegrees = 110;    // horizontal field of view (the original's near walls imply about 125)
  double eyeHeight = 0.5;     // the eye's height as a fraction of wall height
  double drawDistance = 7;    // cells; beyond this it is dark
  double fog = 0.6;           // 0 = surfaces keep their brightness at any distance, 1 = fade to dark at the draw distance
  int heldObject = 0;         // something set down in front of you: 0 none, 1 a chest, 3 the grand level chest
  double heldAhead = 0.95;    // ... this many cells ahead
  double backOff = 0;         // the eye stands this many cells back from the middle of the player's cell (-1 .. 1): mid-step views, and the fight view, which shows the floor in front of a wall
  double secretSlide = 1.0;   // a found secret door: 0 = its stone slab still shuts the doorway, 1 = slid up out of sight (the niche and its fountain)
  bool secretTell = false;    // the torch is flickering: a shut secret door shows its outline in the wall
  bool fountainDry = false;   // ... and its fountain has been drunk from: no water
  double lineScale = 1;       // lines are never thinner than this many picture pixels (a finer picture, like the Text maze's, passes its density to keep the same line weight)
  int supersample = 1;        // trace this many samples per picture pixel in each direction, then filter down (toLayers)
  MapFrame frame;
  const LairTextures* lair = nullptr;  // the boss lair's art (see LairTextures)
};

// Proportions of the world, in cells (a cell is 1 x 1, the wall is 1 high).
struct CorridorStyle {
  int courses = 5;            // rows of bricks in a wall
  double brickLength = 1.0 / 3;  // cells
  double mortar = 0.012;      // brick joint thickness, cells; every line is at least one pixel wide whatever the distance
  double grout = 0.03;        // ceiling seam width, cells
  double floorGrout = 0.016;  // floor grout: a hairline
  double stone = 0.16;        // floor stone size (the spacing of the Voronoi seeds), cells
  double baseline = 0.006;    // the thin dark line where a wall meets the floor and the ceiling, cells
  double edgeLine = 0.006;    // dark line where a wall ends or turns
  double sideFaceAlbedo = 0.88;  // walls seen side-on are a little darker than those facing you (depth cue)
  double floorTone = 0.8;     // floor and ceiling are the same colour as the walls but sparser (darker by dither density), so no cell where they meet can show a stray strip of the wrong colour
  double ceilingTone = 0.7;
  double speckPitch = 0.12, speckSize = 0.02, speckDensity = 0.55;  // ceiling specks: one candidate per pitch square, this size, this share of squares
  double speckPerPixel = 0.08;  // far away, no more than about this many specks per picture pixel are kept
  double seamReach = 0.28;     // far seams are drawn as wide as this share of a pixel (dithered), so they stay a dotted line
  double seamFadeFrom = 0.06, seamEnd = 0.32;  // pixel size (cells) where a seam starts to thin out, and where it is gone
  double slab = 0.5;          // floor and ceiling tile size, cells. The seams sit a quarter cell from the cell borders and centres: none beside a wall base, none where you look from
  double cartHeight = 0.60, cartHalf = 0.38, roofHeight = 0.90;  // the elevator cart (its walls to cartHeight, its roof up to roofHeight)
  double chestHeight = 0.272, chestHalfA = 0.125, chestHalfR = 0.24;  // the plain chest a monster leaves
  double treasureHeight = 0.28, treasureHalfA = 0.125, treasureHalfR = 0.32;  // the level treasure's chest, which stands on its pedestal (a stepped plinth 0.24 high)
  double lairHeight = 0.85, lairHalfA = 0.02, lairHalfR = 0.5;  // the boss's lair: a cave-mouth sprite filling its cell, always square to you
  double doorDepth = 0.0;     // how far the door is set back behind the wall's face, cells: none, the door is flush with the inside edge of the raised frame (whose own thickness, doorRelief, is the doorway's depth)
  double doorRadius = 0.30, doorSpring = 0.5;  // the doorway: half its width (= the arch's radius) and the height of the arch's spring line
  double doorRing = 0.085;    // the width of the voussoir ring round the arch, and of the quoins beside the doorway
  double doorOutline = 0.014; // the dark line round the opening and round the outer edge of its dressed stone, cells (never under a pixel)
  double doorSill = 0.07;     // the floor's pattern stops this far (cells) before a closed door, leaving a plain threshold (a little more than the dressing's relief)
  double doorRelief = 0.035;  // how far the doorway's whole ring of dressed stone stands out from the wall, cells (a few inches): the doorway's depth, and why the stones' base is lower in the picture than the wall's
  double reliefSideAlbedo = 1.0;   // the sides of that raised frame: plain stone (a mid grey would dither into dots that hide the courses behind it)
  int doorVoussoirs = 9;      // stones in the arch (odd: one is the keystone at the top)
  double soffitAlbedo = 0.85;  // the underside of the arch is a little darker than the walls
  double keystoneGrow = 1.4;  // the keystone is this much longer than the other voussoirs
};

enum class Surface : std::uint8_t {
  Void = 0,        // nothing there (beyond the draw distance)
  Background = 1,  // floor and ceiling
  Walls = 2,       // walls, doors and their frames
  Objects = 3,     // the cart, the chest, the boss's lair
  Shade = 4        // parts of objects in shade (the cage's floor, far walls, rear ropes): drawn in the darker shade tone
};

struct Fragment {
  Surface surface = Surface::Void;
  Tint tint = Tint::None;
  std::uint8_t albedo = 0;  // the texture's brightness at this point: 0 = mortar / dark ... 255 = light stone
  std::uint8_t shade = 0;   // distance fade: 255 = near, 0 = fully faded
  float depth = 0;          // distance ahead of the eye, in cells
};

struct ViewFrame {
  int w = 0, h = 0;   // in samples: the picture's size times `ss`
  int ss = 1;
  std::vector<Fragment> px;
  const Fragment& at(int x, int y) const { return px[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]; }
};

// `w`, `h`: the picture's size in pixels (the frame holds w*ss x h*ss samples).
ViewFrame renderCorridor(int w, int h, const TileLookup& lookup, const CorridorCamera& cam = {}, const CorridorStyle& style = {});

// The 1-bit look: each pixel is light where its texture brightness times its fade beats an ordered-dither threshold,
// dark otherwise, on the layer of its surface. The result is `native` (already the picture's size).
ViewLayers toLayers(const ViewFrame& f);

}  // namespace laby
