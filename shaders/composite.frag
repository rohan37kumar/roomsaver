#version 330 core

// Final present: resolved scene + additively-blended bloom. No tonemap so the
// lit surface colors are preserved exactly; emissive cores clip to white while
// their blurred halos add the cozy glow.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uBloomStrength;

void main() {
    vec3 s = texture(uScene, vUV).rgb;
    vec3 b = texture(uBloom, vUV).rgb;
    FragColor = vec4(s + b * uBloomStrength, 1.0);
}
