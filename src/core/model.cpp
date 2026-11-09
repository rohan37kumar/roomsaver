#include "core/model.h"
#include "core/log.h"
#include "core/utf8.h"

#include <cgltf.h>
#include <stb_image.h>
#include <glm/gtc/type_ptr.hpp>

#include <cfloat>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace {

struct Vertex {
    glm::vec3 pos{0};
    glm::vec3 normal{0, 1, 0};
    glm::vec2 uv{0};
};

std::string dir_of(const std::string& path) {
    size_t s = path.find_last_of("/\\");
    return (s == std::string::npos) ? std::string() : path.substr(0, s + 1);
}

GLuint make_gl_texture(const unsigned char* rgba, int w, int h) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return tex;
}

} // namespace

// cgltf file callbacks: open paths as UTF-8 (cgltf's default uses narrow fopen,
// which breaks on non-ASCII install folders).
static cgltf_result gltf_read_utf8(const cgltf_memory_options*, const cgltf_file_options*,
                                   const char* path, cgltf_size* size, void** data) {
    std::vector<unsigned char> bytes;
    if (!read_file_utf8(path, bytes)) return cgltf_result_file_not_found;
    void* buf = std::malloc(bytes.size() ? bytes.size() : 1);
    if (!buf) return cgltf_result_out_of_memory;
    if (!bytes.empty()) std::memcpy(buf, bytes.data(), bytes.size());
    *size = bytes.size();
    *data = buf;
    return cgltf_result_success;
}

static void gltf_release(const cgltf_memory_options*, const cgltf_file_options*, void* data,
                         cgltf_size) {
    std::free(data);
}

bool Model::load(const std::string& path) {
    cgltf_options options = {};
    options.file.read    = gltf_read_utf8;
    options.file.release = gltf_release;
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) {
        LOGI("model: parse failed: %s", path.c_str());
        return false;
    }
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
        LOGI("model: load_buffers failed: %s", path.c_str());
        cgltf_free(data);
        return false;
    }
    cgltf_validate(data);

    const std::string baseDir = dir_of(path);
    std::unordered_map<cgltf_image*, GLuint> texCache;

    auto load_image = [&](cgltf_image* img) -> GLuint {
        if (!img) return 0;
        auto it = texCache.find(img);
        if (it != texCache.end()) return it->second;

        int w = 0, h = 0, comp = 0;
        stbi_uc* px = nullptr;
        if (img->buffer_view) {
            const cgltf_buffer_view* bv = img->buffer_view;
            const unsigned char* src = (const unsigned char*)bv->buffer->data + bv->offset;
            px = stbi_load_from_memory(src, (int)bv->size, &w, &h, &comp, 4);
        } else if (img->uri) {
            std::string file = baseDir + img->uri;
            px = stbi_load(file.c_str(), &w, &h, &comp, 4);
        }
        GLuint tex = 0;
        if (px) { tex = make_gl_texture(px, w, h); stbi_image_free(px); textures.push_back(tex); }
        else    { LOGI("model: failed to decode an image"); }
        texCache[img] = tex;
        return tex;
    };

    // --- materials ---
    materials.reserve(data->materials_count);
    for (size_t i = 0; i < data->materials_count; ++i) {
        const cgltf_material& m = data->materials[i];
        Material mat;
        if (m.has_pbr_metallic_roughness) {
            const auto& pbr = m.pbr_metallic_roughness;
            mat.baseColor = glm::make_vec4(pbr.base_color_factor);
            if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
                mat.baseTex = load_image(pbr.base_color_texture.texture->image);
        }
        mat.emissive = glm::make_vec3(m.emissive_factor);
        mat.emissiveStrength = m.has_emissive_strength ? m.emissive_strength.emissive_strength : 1.0f;
        materials.push_back(mat);
    }

    // --- nodes / meshes ---
    glm::vec3 bmin(FLT_MAX), bmax(-FLT_MAX);

    for (size_t n = 0; n < data->nodes_count; ++n) {
        cgltf_node* node = &data->nodes[n];
        if (!node->mesh) continue;

        float wm[16];
        cgltf_node_transform_world(node, wm);
        glm::mat4 world = glm::make_mat4(wm);

        ModelNode mn;
        mn.name = node->name ? node->name : "";
        mn.world = world;
        mn.normalMat = glm::mat3(glm::transpose(glm::inverse(world)));

        glm::vec3 nbmin(FLT_MAX), nbmax(-FLT_MAX);

        for (size_t p = 0; p < node->mesh->primitives_count; ++p) {
            cgltf_primitive* prim = &node->mesh->primitives[p];
            if (prim->type != cgltf_primitive_type_triangles) continue;

            cgltf_accessor* posAcc = nullptr;
            cgltf_accessor* nrmAcc = nullptr;
            cgltf_accessor* uvAcc  = nullptr;
            for (size_t a = 0; a < prim->attributes_count; ++a) {
                cgltf_attribute* at = &prim->attributes[a];
                if (at->type == cgltf_attribute_type_position) posAcc = at->data;
                else if (at->type == cgltf_attribute_type_normal) nrmAcc = at->data;
                else if (at->type == cgltf_attribute_type_texcoord && at->index == 0) uvAcc = at->data;
            }
            if (!posAcc) continue;

            const size_t vcount = posAcc->count;
            std::vector<Vertex> verts(vcount);
            for (size_t i = 0; i < vcount; ++i) {
                cgltf_accessor_read_float(posAcc, i, &verts[i].pos.x, 3);
                if (nrmAcc) cgltf_accessor_read_float(nrmAcc, i, &verts[i].normal.x, 3);
                if (uvAcc)  cgltf_accessor_read_float(uvAcc,  i, &verts[i].uv.x, 2);

                glm::vec3 wp = glm::vec3(world * glm::vec4(verts[i].pos, 1.0f));
                bmin = glm::min(bmin, wp);
                bmax = glm::max(bmax, wp);
                nbmin = glm::min(nbmin, wp);
                nbmax = glm::max(nbmax, wp);
            }

            std::vector<unsigned int> indices;
            if (prim->indices) {
                indices.resize(prim->indices->count);
                for (size_t i = 0; i < indices.size(); ++i)
                    indices[i] = (unsigned int)cgltf_accessor_read_index(prim->indices, i);
            } else {
                indices.resize(vcount);
                for (size_t i = 0; i < vcount; ++i) indices[i] = (unsigned int)i;
            }

            Primitive gp;
            gp.indexCount = (GLsizei)indices.size();
            gp.material = prim->material ? (int)(prim->material - data->materials) : -1;

            glGenVertexArrays(1, &gp.vao);
            glGenBuffers(1, &gp.vbo);
            glGenBuffers(1, &gp.ebo);
            glBindVertexArray(gp.vao);

            glBindBuffer(GL_ARRAY_BUFFER, gp.vbo);
            glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gp.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));

            glBindVertexArray(0);
            mn.primitives.push_back(gp);
        }

        if (nbmin.x <= nbmax.x) { mn.boundsMin = nbmin; mn.boundsMax = nbmax; }
        if (!mn.primitives.empty()) nodes.push_back(std::move(mn));
    }

    if (bmin.x > bmax.x) { bmin = glm::vec3(-1); bmax = glm::vec3(1); } // empty guard
    boundsMin = bmin;
    boundsMax = bmax;

    LOGI("model: loaded '%s' -> %zu nodes, %zu materials, %zu textures",
         path.c_str(), nodes.size(), materials.size(), textures.size());
    LOGI("model: bounds [%.2f %.2f %.2f] .. [%.2f %.2f %.2f]",
         bmin.x, bmin.y, bmin.z, bmax.x, bmax.y, bmax.z);

    cgltf_free(data);
    return !nodes.empty();
}

ModelNode* Model::findNode(const std::string& name) {
    for (auto& n : nodes) if (n.name == name) return &n;
    return nullptr;
}

void Model::destroy() {
    for (auto& n : nodes)
        for (auto& p : n.primitives) {
            glDeleteVertexArrays(1, &p.vao);
            glDeleteBuffers(1, &p.vbo);
            glDeleteBuffers(1, &p.ebo);
        }
    if (!textures.empty()) glDeleteTextures((GLsizei)textures.size(), textures.data());
    nodes.clear(); materials.clear(); textures.clear();
}
