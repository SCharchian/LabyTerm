#pragma once
#include "ui/ViewPainter.hpp"

namespace laby {

// The elevator shaft the cart rides through, drawn procedurally for the elevator ride scene: a rough-hewn rock back wall (cracked facets, ore veins) with timber
// cross-sets, guide rails with clamps, hanging lanterns and falling dust streaks. Everything hangs off one number, `scroll`
// (in the picture's own 2x4-per-cell sub-pixels, before density): positive moves the scenery DOWN the picture (the cart is
// rising), negative moves it UP (the cart is sinking). The parts scroll at different rates (back wall slowest, dust streaks
// fastest), so the scene has depth as well as direction.
// Paints into `L.background`, `L.walls` and `L.shade` (which must be w x h = the picture size, density already applied).
void paintShaft(ViewLayers& L, double scroll);

// The shaft's timbers and rails are behind the cart, but the cart's open panels show them at full brightness, so they look as though they pass
// through its back wall. This moves the lit parts of the shaft that fall within the cart's outline (per row, from its leftmost to its
// rightmost pixel on any layer of `cart`, the cart as it is drawn this frame) from the walls layer to the shade layer: shadowed.
void shadeBehindCart(ViewLayers& L, const ViewLayers& cart);

// How far the rattling cart is thrown off its rest position at time `ms` (picture pixels, at density 2). It is an old mine cage on
// rough guides: every half second or so a rail joint jolts it, mostly sideways; each jolt eases in over a few frames and rocks out over a second; between them it only sways.
struct CartShake { int dx = 0, dy = 0; };
CartShake cartShake(double ms);

}  // namespace laby
