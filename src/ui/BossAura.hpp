#pragma once
#include <memory>

#include "render/Image8.hpp"

namespace laby {

// A Boss10's glow: two rings round the picture's silhouette (a solid one, then an outer one, dithered unless `dither` is false), as
// images the size of the picture plus a margin (`pad`, in the picture's own pixels). Built once per picture.
struct Aura {
  std::shared_ptr<const Image8> inner, outer;
  int pad = 0;
};
Aura auraFor(const std::shared_ptr<const Image8>& img, bool dither = true);

}  // namespace laby
