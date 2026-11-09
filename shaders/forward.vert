#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat3 uNormalMat;
uniform mat2 uUV;           // UV transform (rotation/flip) about (0.5,0.5); identity by default

out vec3 vNormal;
out vec2 vUV;
out vec3 vWorldPos;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos  = world.xyz;
    vNormal    = uNormalMat * aNormal;
    vUV        = uUV * (aUV - vec2(0.5)) + vec2(0.5);
    gl_Position = uViewProj * world;
}
