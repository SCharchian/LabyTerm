// The torch, shared by the CRT passes: uniform block T (see TorchGpu in GpuCrt.hpp) and torchLevel(). renderTorch in
// FrameRenderer.cpp is the CPU version.
layout(set = 3, binding = 1) uniform T {  // the torch (see TorchGpu in GpuCrt.hpp; renderTorch in FrameRenderer.cpp is the CPU version)
  vec4 rect;       // the picture in frame pixels: x0 y0 x1 y1
  vec4 a;          // x: bands, y: boxiness p, z: dither, w: darkness at the very edge
  vec4 b;          // x: radius where the dark starts, y: softness, z: sideways swing in pixels
  vec4 dark;       // rgb: the colour the edge fades to
  ivec4 blk;       // x, y: block size in pixels, z: number of holes, w: torch on (1) / off (0)
  vec4 holes[32];  // panel rectangles in frame pixels
} t;
const float kBayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);

// How far this pixel is darkened (0..1): chunky, dithered steps, constant over one sub-pixel block of the mosaic art.
float torchLevel(ivec2 p) {
  if (t.blk.w == 0) return 0.0;
  vec2 fp = vec2(p);
  if (fp.x < t.rect.x || fp.x >= t.rect.z || fp.y < t.rect.y || fp.y >= t.rect.w) return 0.0;
  for (int i = 0; i < t.blk.z; ++i)
    if (fp.x >= t.holes[i].x && fp.x < t.holes[i].z && fp.y >= t.holes[i].y && fp.y < t.holes[i].w) return 0.0;
  ivec2 bq = p / t.blk.xy;
  vec2 centre = vec2(bq * t.blk.xy) + 0.5 * vec2(t.blk.xy);
  vec2 half_ = 0.5 * vec2(t.rect.z - t.rect.x, t.rect.w - t.rect.y);
  vec2 mid = vec2(t.rect.x, t.rect.y) + half_;
  vec2 n = abs((centre - mid - vec2(t.b.z, 0.0)) / half_);
  float d = pow(pow(n.x, t.a.y) + pow(n.y, t.a.y), 1.0 / t.a.y);
  float k = clamp((d - t.b.x) / t.b.y, 0.0, 1.0);
  if (k <= 0.0) return 0.0;
  k = k * k * (3.0 - 2.0 * k);
  float v = k * t.a.x, lo = floor(v);
  float thr = 0.5 + ((kBayer[(bq.y & 3) * 4 + (bq.x & 3)] + 0.5) / 16.0 - 0.5) * t.a.z;
  return min(t.a.x, lo + (v - lo > thr ? 1.0 : 0.0)) / t.a.x * t.a.w;
}
