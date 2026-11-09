#include "core/shader.h"
#include "core/log.h"

#include "core/utf8.h"

#include <vector>

static std::string read_text(const std::string& path) {
    std::vector<unsigned char> bytes;
    if (!read_file_utf8(path, bytes)) { LOGI("shader: cannot open %s", path.c_str()); return {}; }
    return std::string(bytes.begin(), bytes.end());
}

static GLuint compile(GLenum type, const char* src, const char* tag) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0; glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 1 ? len : 1);
        glGetShaderInfoLog(s, len, nullptr, log.data());
        LOGI("shader compile error (%s): %s", tag, log.data());
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool Shader::loadFromSource(const char* vsSrc, const char* fsSrc) {
    GLuint vs = compile(GL_VERTEX_SHADER, vsSrc, "vert");
    GLuint fs = compile(GL_FRAGMENT_SHADER, fsSrc, "frag");
    if (!vs || !fs) return false;

    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0; glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0; glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 1 ? len : 1);
        glGetProgramInfoLog(program, len, nullptr, log.data());
        LOGI("shader link error: %s", log.data());
        glDeleteProgram(program); program = 0;
        return false;
    }
    locCache.clear();
    return true;
}

bool Shader::loadFromFiles(const std::string& vsPath, const std::string& fsPath) {
    std::string vs = read_text(vsPath);
    std::string fs = read_text(fsPath);
    if (vs.empty() || fs.empty()) return false;
    return loadFromSource(vs.c_str(), fs.c_str());
}

void Shader::destroy() {
    if (program) { glDeleteProgram(program); program = 0; }
    locCache.clear();
}

GLint Shader::loc(const char* name) {
    auto it = locCache.find(name);
    if (it != locCache.end()) return it->second;
    GLint l = glGetUniformLocation(program, name);
    locCache.emplace(name, l);
    return l;
}
