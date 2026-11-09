#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct Material {
    glm::vec4 baseColor{1, 1, 1, 1};
    glm::vec3 emissive{0, 0, 0};
    float     emissiveStrength = 1.0f;
    GLuint    baseTex = 0;         // 0 = untextured
    bool      unlit = false;       // true = self-lit screen (TV / clock)
};

struct Primitive {
    GLuint  vao = 0, vbo = 0, ebo = 0;
    GLsizei indexCount = 0;
    int     material = -1;
};

// One drawable node from the glTF scene. `name` is the glTF node name — this is
// how later phases locate TV_Screen / Picture_Image / Clock_Display / TV_BiasLight.
struct ModelNode {
    std::string            name;
    glm::mat4              world{1.0f};
    glm::mat3              normalMat{1.0f};
    std::vector<Primitive> primitives;
    glm::mat2              uvTransform{1.0f};  // sampling rotation/flip (default identity)
    glm::vec3              boundsMin{0.0f};    // world-space AABB of this node
    glm::vec3              boundsMax{0.0f};
};

struct Model {
    std::vector<Material>  materials;
    std::vector<ModelNode> nodes;
    std::vector<GLuint>    textures;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};

    bool load(const std::string& path);   // .glb or .gltf
    void destroy();

    ModelNode* findNode(const std::string& name);
    glm::vec3  center() const { return (boundsMin + boundsMax) * 0.5f; }
    float      radius() const { return glm::length(boundsMax - boundsMin) * 0.5f; }
};
