#version 330 core

// Bright-pass: keep only what exceeds the threshold, with a soft knee so the
// bloom ramps in smoothly instead of popping. Feeds the blur chain.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScene;
uniform float uThreshold;
uniform float uKnee;

void main() {
    vec3 c = texture(uScene, vUV).rgb;
    float br = max(c.r, max(c.g, c.b));

    float k = max(uKnee, 1e-4);
    float soft = clamp(br - uThreshold + k, 0.0, 2.0 * k);
    soft = soft * soft / (4.0 * k);
    float contrib = max(soft, br - uThreshold) / max(br, 1e-4);

    FragColor = vec4(c * contrib, 1.0);
}
