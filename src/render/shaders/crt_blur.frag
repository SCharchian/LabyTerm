#version 450
// One direction of a separable gaussian (edges clamp).
layout(set = 2, binding = 0) uniform sampler2D uTex;
layout(set = 3, binding = 0) uniform U {
  vec2 dir;      // (1, 0) or (0, 1)
  float sigma;
  int radius;
} u;
layout(location = 0) out vec4 outColor;
void main() {
  vec2 texel = 1.0 / vec2(textureSize(uTex, 0));
  vec2 uv = gl_FragCoord.xy * texel;
  vec3 sum = vec3(0.0);
  float wsum = 0.0;
  for (int i = -u.radius; i <= u.radius; ++i) {
    float w = exp(-0.5 * float(i * i) / (u.sigma * u.sigma));
    sum += w * texture(uTex, uv + u.dir * texel * float(i)).rgb;
    wsum += w;
  }
  outColor = vec4(sum / wsum, 1.0);
}
