#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

// Minimal GLSL program wrapper with cached uniform lookups.
struct Shader {
    GLuint program = 0;
    std::unordered_map<std::string, GLint> locCache;

    bool loadFromFiles(const std::string& vsPath, const std::string& fsPath);
    bool loadFromSource(const char* vsSrc, const char* fsSrc);
    void use() const { glUseProgram(program); }
    void destroy();

    GLint loc(const char* name);
    void set(const char* n, int v)               { glUniform1i(loc(n), v); }
    void set(const char* n, float v)             { glUniform1f(loc(n), v); }
    void set(const char* n, const glm::vec2& v)  { glUniform2fv(loc(n), 1, &v.x); }
    void set(const char* n, const glm::vec3& v)  { glUniform3fv(loc(n), 1, &v.x); }
    void set(const char* n, const glm::vec4& v)  { glUniform4fv(loc(n), 1, &v.x); }
    void set(const char* n, const glm::mat2& v)  { glUniformMatrix2fv(loc(n), 1, GL_FALSE, &v[0][0]); }
    void set(const char* n, const glm::mat3& v)  { glUniformMatrix3fv(loc(n), 1, GL_FALSE, &v[0][0]); }
    void set(const char* n, const glm::mat4& v)  { glUniformMatrix4fv(loc(n), 1, GL_FALSE, &v[0][0]); }
};
