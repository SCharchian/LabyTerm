#version 450
#extension GL_GOOGLE_include_directive : enable
#include "crt_common.glsl"
#include "torch_common.glsl"
// Glow source at half resolution: what is bright, weighted and thresholded per region (UI vs viewport), averaged 2x2.
// 3D objects glow whatever their brightness: the mask holds, per cell, the cell's background (rgb) and a = 1 where the cell has
// object ink (a = 1) or a monster (a = 0.5; rgb is then its body color), and what lies above that color in such a cell is the glow source.
layout(set = 2, binding = 0) uniform sampler2D uSrc;
layout(set = 2, binding = 1) uniform sampler2D uMask;
layout(set = 3, binding = 0) uniform U {
  vec4 viewport;   // x0 y0 x1 y1 in frame pixels (end exclusive)
  vec4 bloom;      // x: UI bloom, y: viewport bloom, z: viewport threshold, w: beam spread s
  ivec4 size;      // xy: frame size, zw: cell size in pixels (0: no mask)
  vec4 obj;        // x: object bloom, y: how far above the cell color a pixel must be, z: glow floor (nothing at or below it glows), w: monster bloom
} u;
layout(location = 0) out vec4 outColor;
void main() {
  ivec2 q = ivec2(gl_FragCoord.xy);
  vec3 sum = vec3(0.0);
  for (int dy = 0; dy < 2; ++dy)
    for (int dx = 0; dx < 2; ++dx) {
      ivec2 p = q * 2 + ivec2(dx, dy);
      if (p.x >= u.size.x || p.y >= u.size.y) continue;
      bool inView = float(p.x) >= u.viewport.x && float(p.x) < u.viewport.z && float(p.y) >= u.viewport.y && float(p.y) < u.viewport.w;
      float weight = inView ? u.bloom.y : u.bloom.x;
      float threshold = max(inView ? u.bloom.z : 0.30, u.obj.z);
      vec3 c = mix(beam(uSrc, p, u.size.xy, u.bloom.w), t.dark.rgb, torchLevel(p));  // as lit by the resting torch
      vec3 s = max(c - threshold, 0.0) * weight;
      if ((u.obj.x > 0.0 || u.obj.w > 0.0) && u.size.z > 0) {
        vec4 m = texelFetch(uMask, min(p / u.size.zw, textureSize(uMask, 0) - 1), 0);
        if (m.a > 0.25) s = max(c - m.rgb - u.obj.y, 0.0) * (m.a > 0.9 ? u.obj.x : m.a > 0.6 ? u.obj.x * 3.0 : u.obj.w);  // 1 object, 0.75 object drawn as text (x kTextGlowBoost), 0.5 monster
      }
      sum += s;
    }
  outColor = vec4(sum * 0.25, 1.0);
}
