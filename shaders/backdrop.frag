#version 330 core

// Warm gradient backdrop with a soft radial glow behind the room. Drawn first,
// full-screen, with depth writes off so the room composites on top. The glow
// colour (uGlowColor) follows the current album art.
in vec2 vUV;
out vec4 FragColor;

uniform vec3  uTopColor;
uniform vec3  uBottomColor;
uniform vec3  uGlowColor;
uniform vec2  uGlowCenter;   // in 0..1 screen space
uniform float uGlowRadius;
uniform float uAspect;       // width/height, to keep the glow circular

void main() {
    vec3 base = mix(uBottomColor, uTopColor, clamp(vUV.y, 0.0, 1.0));

    vec2 d = vUV - uGlowCenter;
    d.x *= uAspect;
    float dist = length(d);
    float g = smoothstep(uGlowRadius, 0.0, dist);   // 1 at centre -> 0 at radius

    FragColor = vec4(base + uGlowColor * g, 1.0);
}
