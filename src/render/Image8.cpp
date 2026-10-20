#include "render/Image8.hpp"

#include "stb_image.h"
#include "stb_image_write.h"

namespace laby {

Image8 Image8::load(const std::string& path) {
  Image8 im;
  int w = 0, h = 0, n = 0;
  unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
  if (!px) return im;
  im.w = w;
  im.h = h;
  im.tone.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
  im.alpha.resize(im.tone.size());
  for (size_t i = 0; i < im.tone.size(); ++i) {
    im.tone[i] = px[i * 4];
    im.alpha[i] = px[i * 4 + 3];
  }
  stbi_image_free(px);
  return im;
}

bool Image8::save(const std::string& path) const {
  std::vector<unsigned char> rgba(tone.size() * 4);
  for (size_t i = 0; i < tone.size(); ++i) {
    rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = tone[i];
    rgba[i * 4 + 3] = alpha[i];
  }
  return stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4) != 0;
}

}  // namespace laby
