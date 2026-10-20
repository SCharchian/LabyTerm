#version 450
// Half -> quarter resolution: the bilinear sample at the corner shared by four texels is their average.
layout(set = 2, binding = 0) uniform sampler2D uTex;
layout(location = 0) out vec4 outColor;
void main() { outColor = texture(uTex, gl_FragCoord.xy * 2.0 / vec2(textureSize(uTex, 0))); }
