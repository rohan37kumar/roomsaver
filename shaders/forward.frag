#version 330 core

in vec3 vNormal;
in vec2 vUV;
in vec3 vWorldPos;

out vec4 FragColor;

uniform vec4  uBaseColor;
uniform int   uHasTex;
uniform sampler2D uTex;

uniform vec3  uEmissive;
uniform float uEmissiveStrength;
uniform float uEmissiveBoost;   // pushes emissive cores past the bloom threshold

uniform vec3  uLightDir;    // direction TO the key light (world space, normalized)
uniform vec3  uLightColor;
uniform vec3  uAmbient;

uniform vec3  uPointPos;    // wall-sconce point light (world space)
uniform vec3  uPointColor;  // 0 = disabled
uniform float uPointRadius;

uniform int   uUnlit;       // 1 = self-lit screen (show base at full, skip shading)

// Edge-feather for the TV backlight only: fades emissive toward the quad's edges
// (computed from world x/y extent, so mesh UVs don't matter). 0 = disabled.
uniform float uFeatherAmt;
uniform vec2  uFeatherCenter;
uniform vec2  uFeatherHalf;

float feather_fade() {
    if (uFeatherAmt <= 0.0) return 1.0;
    vec2 rel = abs(vWorldPos.xy - uFeatherCenter) / max(uFeatherHalf, vec2(1e-4));
    return smoothstep(1.0, 1.0 - uFeatherAmt, max(rel.x, rel.y));
}

void main() {
    vec3 base = uBaseColor.rgb;
    if (uHasTex == 1) base *= texture(uTex, vUV).rgb;

    // Screens (TV / clock) are self-lit: show the texture directly so they read
    // the same regardless of room lighting, and still carry an emissive term.
    if (uUnlit == 1) {
        FragColor = vec4(base + uEmissive * uEmissiveStrength * uEmissiveBoost,
                         uBaseColor.a);
        return;
    }

    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);
    float diff = max(dot(N, L), 0.0);

    vec3 color = base * (uAmbient + diff * uLightColor);

    // Wall-sconce point light: warm pool that falls off with distance so the room
    // around the lamp actually catches its light (the engine has no GI bounce).
    vec3  toP  = uPointPos - vWorldPos;
    float dP   = length(toP);
    float att  = clamp(1.0 - dP / uPointRadius, 0.0, 1.0);
    att *= att;
    float diffP = max(dot(N, toP / max(dP, 1e-4)), 0.0);
    color += base * uPointColor * diffP * att;

    color += uEmissive * uEmissiveStrength * uEmissiveBoost * feather_fade();

    FragColor = vec4(color, uBaseColor.a);
}
