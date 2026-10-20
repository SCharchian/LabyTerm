#version 450
#extension GL_GOOGLE_include_directive : enable
#include "crt_common.glsl"
#include "torch_common.glsl"
// The finished frame: beam spread, glow added like a screen blend, raster-line gaps, vignette.
layout(set = 2, binding = 0) uniform sampler2D uSrc;
layout(set = 2, binding = 1) uniform sampler2D uGlowTight;  // half resolution
layout(set = 2, binding = 2) uniform sampler2D uGlowWide;   // quarter resolution
layout(set = 3, binding = 0) uniform U {
  vec4 look;   // x: beam spread s, y: scanline gap, z: vignette, w: glow on (1) / off (0)
  ivec4 size;  // xy: frame size, z: raster pitch in rows
} u;
layout(location = 0) out vec4 outColor;

void main() {
  ivec2 p = ivec2(gl_FragCoord.xy);
  vec3 c = beam(uSrc, p, u.size.xy, u.look.x);
  float dim = torchLevel(p);  // (the glow was worked out from the picture dimmed by the resting torch, and cached)
  c = mix(c, t.dark.rgb, dim);
  if (u.look.w > 0.5) {
    // bilinear upsample of the blurred copies, same sample positions as the CPU's (x + 0.5) / d - 0.5
    vec3 a1 = texture(uGlowTight, gl_FragCoord.xy / (2.0 * vec2(textureSize(uGlowTight, 0)))).rgb;
    vec3 a2 = texture(uGlowWide, gl_FragCoord.xy / (4.0 * vec2(textureSize(uGlowWide, 0)))).rgb;
    vec3 g = clamp(a1 * 2.4 + a2 * 2.0, 0.0, 1.0);
    c += (1.0 - min(c, vec3(1.0))) * g;
  }
  float line = (p.y % u.size.z == u.size.z - 1) ? 1.0 - u.look.y : 1.0;
  vec2 half_ = vec2(u.size.xy) * 0.5;
  vec2 d = (vec2(p) - half_) / half_;
  float v = 1.0 - u.look.z * 0.5 * dot(d, d);
  outColor = vec4(clamp(c * line * v, 0.0, 1.0), 1.0);
}
