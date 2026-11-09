#version 330 core

// Attribute-less fullscreen triangle (bind an empty VAO, draw 3 verts).
// gl_VertexID 0,1,2 -> positions covering the whole clip space; vUV is 0..1
// with (0,0) bottom-left, (1,1) top-right.
out vec2 vUV;

void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
