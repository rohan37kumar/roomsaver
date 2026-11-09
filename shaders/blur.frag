#version 330 core

// Separable 9-tap gaussian. Run once horizontally then once vertically per
// iteration (uDir = one texel in the blur direction).
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uTex;
uniform vec2 uDir;

const float W[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main() {
    vec3 r = texture(uTex, vUV).rgb * W[0];
    for (int i = 1; i < 5; ++i) {
        vec2 o = uDir * float(i);
        r += texture(uTex, vUV + o).rgb * W[i];
        r += texture(uTex, vUV - o).rgb * W[i];
    }
    FragColor = vec4(r, 1.0);
}
