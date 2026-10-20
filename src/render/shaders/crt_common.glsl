// Shared by the CRT shaders. Mirrors the CPU filter in FrameRenderer.cpp (applyCrt), which stays as the fallback and
// the reference the parity test compares against.

// The frame pixel at p after the horizontal beam spread [s, 1-2s, s] (edge pixels clamp).
vec3 beam(sampler2D t, ivec2 p, ivec2 size, float s) {
  vec3 c = texelFetch(t, p, 0).rgb;
  if (s <= 0.0) return c;
  vec3 l = texelFetch(t, ivec2(max(p.x - 1, 0), p.y), 0).rgb;
  vec3 r = texelFetch(t, ivec2(min(p.x + 1, size.x - 1), p.y), 0).rgb;
  return c * (1.0 - 2.0 * s) + s * (l + r);
}
