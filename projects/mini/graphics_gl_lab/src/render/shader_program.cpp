#include "render/shader_program.h"

#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <cstdio>

namespace gllab {

namespace {

std::string ReadFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("shader file not found: " + path);
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

ShaderProgram::ShaderProgram() = default;

ShaderProgram::~ShaderProgram() {
    if (program_) glDeleteProgram(program_);
}

GLuint ShaderProgram::Compile(GLenum type, const std::string& src) {
    GLuint sh = glCreateShader(type);
    const char* csrc = src.c_str();
    glShaderSource(sh, 1, &csrc, nullptr);
    glCompileShader(sh);

    GLint ok = GL_FALSE;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log((size_t)logLen + 1);
        glGetShaderInfoLog(sh, logLen, nullptr, log.data());
        glDeleteShader(sh);
        const char* kind = (type == GL_VERTEX_SHADER)   ? "vertex"
                         : (type == GL_FRAGMENT_SHADER) ? "fragment"
                         : "shader";
        throw std::runtime_error(std::string("[") + kind + " compile failed]\n" + log.data());
    }
    return sh;
}

void ShaderProgram::LoadFromSources(const std::string& vertSrc, const std::string& fragSrc) {
    GLuint vs = Compile(GL_VERTEX_SHADER,   vertSrc);
    GLuint fs = Compile(GL_FRAGMENT_SHADER, fragSrc);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log((size_t)logLen + 1);
        glGetProgramInfoLog(prog, logLen, nullptr, log.data());
        glDeleteShader(vs);
        glDeleteShader(fs);
        glDeleteProgram(prog);
        throw std::runtime_error(std::string("[program link failed]\n") + log.data());
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    if (program_) glDeleteProgram(program_);
    program_ = prog;
    uniformCache_.clear();
}

void ShaderProgram::LoadFromFiles(const std::string& vertPath, const std::string& fragPath) {
    LoadFromSources(ReadFile(vertPath), ReadFile(fragPath));
}

void ShaderProgram::Use() const {
    glUseProgram(program_);
}

GLint ShaderProgram::Uniform(const char* name) {
    auto it = uniformCache_.find(name);
    if (it != uniformCache_.end()) return it->second;
    GLint loc = glGetUniformLocation(program_, name);
    uniformCache_[name] = loc;
    return loc;
}

bool ShaderProgram::SetMat4(const char* name, const glm::mat4& m) {
    GLint loc = Uniform(name);
    if (loc < 0) return false;
    glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(m));
    return true;
}

bool ShaderProgram::SetVec3(const char* name, const glm::vec3& v) {
    GLint loc = Uniform(name);
    if (loc < 0) return false;
    glUniform3fv(loc, 1, glm::value_ptr(v));
    return true;
}

bool ShaderProgram::SetFloat(const char* name, float v) {
    GLint loc = Uniform(name);
    if (loc < 0) return false;
    glUniform1f(loc, v);
    return true;
}

bool ShaderProgram::SetInt(const char* name, int v) {
    GLint loc = Uniform(name);
    if (loc < 0) return false;
    glUniform1i(loc, v);
    return true;
}

}  // namespace gllab
